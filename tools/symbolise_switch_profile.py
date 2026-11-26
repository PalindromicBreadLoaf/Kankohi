#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
# SPDX-License-Identifier: GPL-3.0-or-later

"""Turn profile.txt into usable ranked C++ functions, wait sites, and flame graphs.

Examples:

    tools/symbolise_switch_profile.py profile.txt build/switch-release/kankohi.elf
    tools/symbolise_switch_profile.py profile.txt kankohi.elf --thread LatteThread
    tools/symbolise_switch_profile.py profile.txt kankohi.elf --folded out.folded
    tools/symbolise_switch_profile.py profile.txt kankohi.elf --disasm
"""

import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile

BUILD_ID_LINE = re.compile(r"^build id ([0-9a-f]+)$")
ELF_BUILD_ID = re.compile(r"Build ID: ([0-9a-f]+)")
MODULE_LINE = re.compile(r"^module base 0x[0-9a-f]+ size 0x([0-9a-f]+)")
THREAD_HEADER = re.compile(r"^== Thread (T\d+): (.*) ==$")
STACK_LINE = re.compile(r"^  (\d+) (run|svc:[0-9a-f]+) (.*)$")

FUNCTION_LINE = re.compile(r"^\s+\d+\s+\d+\s+([0-9a-f]{8})  (\S+)$")
BLOCK_HEADER = re.compile(r"^Block ([0-9a-f]{8}), (\d+) host instructions")
BLOCK_LINE = re.compile(r"^\s*\+0x([0-9a-f]+)\s+(\d+)\s+([0-9a-f]{8})\s*$")

SVC_NAMES = {
    0x0B: "SleepThread",
    0x18: "WaitSynchronization",
    0x1A: "ArbitrateLock",
    0x1C: "WaitProcessWideKeyAtomic",
    0x21: "SendSyncRequest",
    0x22: "SendSyncRequestWithUserBuffer",
    0x34: "WaitForAddress",
    0x43: "ReplyAndReceive",
}


class Stack:
    def __init__(self, count, state, tokens):
        self.count = count
        self.blocked = state != "run"
        self.svc = int(state[4:], 16) if self.blocked else None
        self.pc = tokens[0]
        self.lr = None
        self.frames = []
        for token in tokens[1:]:
            if token.startswith("L"):
                self.lr = token[1:]
            else:
                self.frames.append(token)


def read_report(path):
    threads = []
    current = None
    with open(path, encoding="utf-8") as report:
        for line in report:
            line = line.rstrip("\n")
            header = THREAD_HEADER.match(line)
            if header:
                current = (header.group(1), header.group(2), [])
                threads.append(current)
                continue
            if current is None:
                continue
            match = STACK_LINE.match(line)
            if match:
                current[2].append(Stack(int(match.group(1)), match.group(2),
                                        match.group(3).split()))
    return threads


def read_preamble(path):
    lines = []
    with open(path, encoding="utf-8") as report:
        for line in report:
            if BLOCK_HEADER.match(line) or THREAD_HEADER.match(line.rstrip("\n")):
                break
            lines.append(line.rstrip("\n"))
    return lines


def read_guest_names(path):
    return {m.group(1): m.group(2) for m in map(FUNCTION_LINE.match, read_preamble(path)) if m}


def check_build_matches(report, elf, nm, readelf):
    with open(report, encoding="utf-8") as handle:
        header = [next(handle, "") for _ in range(8)]

    reported_ids = [m.group(1) for m in map(BUILD_ID_LINE.match, header) if m]
    if reported_ids:
        output = subprocess.run([readelf, "-n", elf], capture_output=True, text=True,
                                check=True).stdout
        actual_id = ELF_BUILD_ID.search(output)
        if actual_id and actual_id.group(1) != reported_ids[0]:
            sys.exit(f"{elf} has build id {actual_id.group(1)} but {report} was captured from "
                     f"build {reported_ids[0]}.")
        if actual_id:
            return

    sizes = [MODULE_LINE.match(line) for line in header]
    sizes = [m for m in sizes if m]
    if not sizes:
        return
    reported = int(sizes[0].group(1), 16)

    output = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    symbols = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in ("_start", "__end__"):
            symbols[parts[2]] = int(parts[0], 16)
    if len(symbols) != 2:
        return

    actual = symbols["__end__"] - symbols["_start"]
    if actual != reported:
        sys.exit(f"{elf} is {actual:#x} bytes of module but {report} was captured from a "
                 f"{reported:#x}-byte build.")


def find_tool(name):
    found = shutil.which(name)
    if found is not None:
        return found
    devkitpro = os.environ.get("DEVKITPRO", "/opt/devkitpro")
    candidate = os.path.join(devkitpro, "devkitA64", "bin", name)
    if os.access(candidate, os.X_OK):
        return candidate
    sys.exit(f"{name} is not on PATH and is not under $DEVKITPRO/devkitA64/bin. "
             "Please pass its full path explicitly.")


class Symboliser:
    def __init__(self, addr2line, elf, guest_names):
        self.guest_names = guest_names
        self.addr2line = addr2line
        self.elf = elf
        self.cache = {}

    def prime(self, offsets):
        wanted = sorted(set(offsets) - self.cache.keys())
        if not wanted:
            return
        command = [self.addr2line, "-f", "-C", "-e", self.elf]
        text = "\n".join(f"0x{offset:x}" for offset in wanted) + "\n"
        output = subprocess.run(command, input=text, capture_output=True, text=True,
                                check=True).stdout
        lines = output.splitlines()
        for index, offset in enumerate(wanted):
            function = lines[index * 2] if index * 2 < len(lines) else "??"
            self.cache[offset] = function if function != "??" else f"?? (+0x{offset:x})"

    def function(self, offset):
        return self.cache.get(offset, f"+0x{offset:x}")


def module_offset(token, is_return):
    offset = int(token[2:], 16)
    return offset - 4 if is_return and offset >= 4 else offset


def describe(token, is_return, symboliser):
    kind, _, value = token.partition(":")
    if kind == "m":
        return symboliser.function(module_offset(token, is_return))
    if kind == "b":
        name = symboliser.guest_names.get(value)
        return f"[PPC {value} {name}]" if name else f"[PPC {value}]"
    if kind == "j":
        return "[recompiler interface code]"
    return "[unknown code]"


def symbolise_stack(stack, symboliser):
    pc = describe(stack.pc, False, symboliser)
    frames = [describe(token, True, symboliser) for token in stack.frames]
    if stack.lr is not None:
        lr = describe(stack.lr, True, symboliser)
        if lr != pc and (not frames or lr != frames[0]):
            frames.insert(0, lr)
    return [pc] + frames


def all_module_offsets(threads):
    for _, _, stacks in threads:
        for stack in stacks:
            if stack.pc.startswith("m:"):
                yield module_offset(stack.pc, False)
            if stack.lr is not None and stack.lr.startswith("m:"):
                yield module_offset(stack.lr, True)
            for token in stack.frames:
                if token.startswith("m:"):
                    yield module_offset(token, True)


def print_thread(column, name, stacks, symboliser, count):
    total = sum(stack.count for stack in stacks)
    if total == 0:
        return
    blocked_total = sum(stack.count for stack in stacks if stack.blocked)

    self_time = collections.Counter()
    inclusive = collections.Counter()
    waits = collections.Counter()
    for stack in stacks:
        frames = symbolise_stack(stack, symboliser)
        if stack.blocked:
            svc = SVC_NAMES.get(stack.svc, f"svc 0x{stack.svc:x}")
            waits[(svc,) + tuple(frames[1:7])] += stack.count
            continue
        self_time[frames[0]] += stack.count
        for function in set(frames):
            inclusive[function] += stack.count

    running = total - blocked_total
    print(f"\n{'=' * 100}\n{column}: {name} — {total} samples, "
          f"{100.0 * running / total:.1f}% running, {100.0 * blocked_total / total:.1f}% blocked")

    if running:
        print("\n  Self time while running (percent of all this thread's samples):")
        for function, samples in self_time.most_common(count):
            print(f"    {100.0 * samples / total:5.1f}%  {samples:7}  {function}")

        print("\n  Inclusive time while running (the function and everything it called):")
        for function, samples in inclusive.most_common(count):
            print(f"    {100.0 * samples / total:5.1f}%  {samples:7}  {function}")

    if waits:
        print("\n  Where it waited (the syscall and its leadup):")
        for frames, samples in waits.most_common(max(5, count // 3)):
            print(f"    {100.0 * samples / total:5.1f}%  {samples:7}  {frames[0]}")
            for frame in frames[1:]:
                print(f"                      <- {frame}")


def write_folded(path, threads, symboliser):
    folded = collections.Counter()
    for column, name, stacks in threads:
        root = f"{column} {name}".replace(";", ":")
        for stack in stacks:
            frames = symbolise_stack(stack, symboliser)
            if stack.blocked:
                frames = [f"[blocked: {SVC_NAMES.get(stack.svc, hex(stack.svc))}]"] + frames
            path_frames = [root] + [frame.replace(";", ":") for frame in reversed(frames)]
            folded[";".join(path_frames)] += stack.count
    with open(path, "w", encoding="utf-8") as out:
        for line, samples in folded.most_common():
            out.write(f"{line} {samples}\n")
    print(f"Wrote {len(folded)} stacks to {path}.")


def read_blocks(path):
    blocks = []
    current = None
    with open(path, encoding="utf-8") as report:
        for line in report:
            header = BLOCK_HEADER.match(line)
            if header:
                current = (f"Block {header.group(1)}", [])
                blocks.append(current)
                continue
            if current is None:
                continue
            match = BLOCK_LINE.match(line)
            if match:
                current[1].append((int(match.group(2)), int(match.group(3), 16)))
            elif line.strip():
                current = None
    return blocks


def disassemble(objdump, encodings):
    with tempfile.NamedTemporaryFile(suffix=".bin") as raw:
        raw.write(b"".join(word.to_bytes(4, "little") for word in encodings))
        raw.flush()
        command = [objdump, "-D", "-b", "binary", "-m", "aarch64", raw.name]
        output = subprocess.run(command, capture_output=True, text=True, check=True).stdout

    text = {}
    for line in output.splitlines():
        parts = line.split("\t")
        if len(parts) < 3 or ":" not in parts[0]:
            continue
        try:
            address = int(parts[0].strip().rstrip(":"), 16)
        except ValueError:
            continue
        text[address] = "\t".join(parts[2:]).strip()
    return text


def print_blocks(objdump, blocks, threshold):
    for name, instructions in blocks:
        total = sum(count for count, _ in instructions)
        print(f"\n{name} — {len(instructions)} instructions, {total} samples")
        text = disassemble(objdump, [word for _, word in instructions])
        for index, (count, _) in enumerate(instructions):
            if count < threshold:
                continue
            share = count * 100.0 / total if total else 0.0
            mnemonic = text.get(index * 4, "?")
            print(f"  {share:5.1f}%  {count:6}  +0x{index * 4:04x}  {mnemonic}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("report", help="profile-*.txt from the kankohi logs directory")
    parser.add_argument("elf", help="the kankohi.elf the running NRO was built from")
    parser.add_argument("--addr2line", default="aarch64-none-elf-addr2line")
    parser.add_argument("--objdump", default="aarch64-none-elf-objdump")
    parser.add_argument("--nm", default="aarch64-none-elf-nm")
    parser.add_argument("--readelf", default="aarch64-none-elf-readelf")
    parser.add_argument("-n", "--count", type=int, default=25,
                        help="how many functions to print per table")
    parser.add_argument("--thread", help="only threads whose name contains this")
    parser.add_argument("--folded", metavar="FILE",
                        help="write symbolised stacks for a flame graph instead of the tables")
    parser.add_argument("--disasm", action="store_true",
                        help="disassemble the hot recompiled functions instead of the module "
                             "samples")
    parser.add_argument("--threshold", type=int, default=1,
                        help="with --disasm, hide instructions below this sample count")
    args = parser.parse_args()

    check_build_matches(args.report, args.elf, find_tool(args.nm), find_tool(args.readelf))

    if args.disasm:
        objdump = find_tool(args.objdump)
        blocks = read_blocks(args.report)
        if not blocks:
            sys.exit(f"No disassembled blocks in {args.report}. Was any recompiled "
                     "code sampled?")
        print_blocks(objdump, blocks, args.threshold)
        return

    threads = read_report(args.report)
    if not threads:
        sys.exit(f"No thread sections in {args.report}. Is it from an older build of the "
                 "profiler?")
    if args.thread:
        threads = [t for t in threads if args.thread.lower() in t[1].lower()]
        if not threads:
            sys.exit(f"No thread matching {args.thread!r}.")

    symboliser = Symboliser(find_tool(args.addr2line), args.elf, read_guest_names(args.report))
    symboliser.prime(all_module_offsets(threads))

    if args.folded:
        write_folded(args.folded, threads, symboliser)
        return

    for line in read_preamble(args.report):
        print(line)
    for column, name, stacks in threads:
        print_thread(column, name, stacks, symboliser, args.count)


if __name__ == "__main__":
    main()
