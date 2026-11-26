#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
# SPDX-License-Identifier: GPL-3.0-or-later

"""Drives the Kankohi benchmark feature

Requires sys-botbase, sys-ftpd, and nxlink.

Set
  KANKOHI_HOST      console IP address
  KANKOHI_FTP_PORT  FTP port (default 5000)
  KANKOHI_FTP_USER  FTP user
  KANKOHI_FTP_PASS  FTP password

For example:
  kankohi_bench.py hbmenu                 # launch hbmenu
  kankohi_bench.py run build/switch-release/kankohi.nro "games/Some Game.wua" \\
      --duration 60 --warmup 30 --label baseline --replay-input inputs/some-game.txt
  kankohi_bench.py results
"""

import argparse
import binascii
import csv
import ftplib
import io
import os
import socket
import statistics
import subprocess
import sys
import time

SD_ROOT = "sdmc:/switch/kankohi"
FTP_LOGS = "/switch/kankohi/logs"


def env(name, default=None):
    value = os.environ.get(name, default)
    if value is None:
        sys.exit(f"set {name}")
    return value


class Bot:
    def __init__(self, host):
        self.sock = socket.create_connection((host, 6000), timeout=15)

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode())

    def query(self, command):
        self.send(command)
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = self.sock.recv(1 << 16)
            if not chunk:
                break
            buf += chunk
        return buf.strip()

    def click(self, button, settle=0.4):
        self.send(f"click {button}")
        time.sleep(settle)

    def screenshot(self, path):
        data = binascii.unhexlify(self.query("pixelPeek"))
        with open(path, "wb") as f:
            f.write(data)

    def title_id(self):
        return self.query("getTitleID").decode()


def ftp_connect():
    ftp = ftplib.FTP()
    ftp.connect(env("KANKOHI_HOST"), int(env("KANKOHI_FTP_PORT", "5000")), timeout=30)
    ftp.login(env("KANKOHI_FTP_USER"), env("KANKOHI_FTP_PASS"))
    return ftp


def ftp_read(ftp, path):
    out = io.BytesIO()
    try:
        ftp.retrbinary(f"RETR {path}", out.write)
    except (ftplib.error_perm, ftplib.error_temp):
        return None
    return out.getvalue().decode(errors="replace")


def read_rows(ftp):
    text = ftp_read(ftp, f"{FTP_LOGS}/bench.csv")
    return list(csv.DictReader(io.StringIO(text))) if text else []


def cmd_hbmenu(args):
    bot = Bot(env("KANKOHI_HOST"))
    bot.send("press R")
    time.sleep(1.5)
    bot.send("press A")
    time.sleep(0.15)
    bot.send("release A")
    time.sleep(5.0)
    bot.send("release R")
    time.sleep(1.0)
    if args.screenshot:
        bot.screenshot(args.screenshot)


def cmd_screenshot(args):
    Bot(env("KANKOHI_HOST")).screenshot(args.path)


def cmd_menu(args):
    bot = Bot(env("KANKOHI_HOST"))
    bot.send("press PLUS")
    bot.send("press MINUS")
    time.sleep(0.3)
    bot.send("release MINUS")
    bot.send("release PLUS")
    time.sleep(1.0)
    if args.screenshot:
        bot.screenshot(args.screenshot)


def cmd_click(args):
    bot = Bot(env("KANKOHI_HOST"))
    for button in args.buttons:
        bot.click(button, settle=args.delay)


def sd_path(path):
    return path if path.startswith("sdmc:") else f"{SD_ROOT}/{path}"


def cmd_run(args):
    host = env("KANKOHI_HOST")
    with ftp_connect() as ftp:
        seen = {r["run_id"] for r in read_rows(ftp)}

    if not args.netloader_open:
        Bot(host).click("Y", settle=1.5)

    app_args = [f"--game={sd_path(args.image)}", f"--bench={args.duration:g}", f"--warmup={args.warmup:g}"]
    if args.replay_input:
        app_args.append(f"--replay-input={sd_path(args.replay_input)}")
    if args.label:
        app_args.append(f"--label={args.label}")
    app_args += [f"--set={setting}" for setting in args.set]

    print("sending", args.nro, app_args, flush=True)
    subprocess.run(["nxlink", "-a", host, "-r", "10", args.nro, "--", *app_args], check=True)

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        time.sleep(15)
        try:
            with ftp_connect() as ftp:
                rows = read_rows(ftp)
        except (OSError, ftplib.Error) as e:
            print("ftp:", e, flush=True)
            continue
        new_rows = [r for r in rows if r["run_id"] not in seen]
        if new_rows:
            print_rows(new_rows)
            return
        print("waiting for result...", flush=True)
    sys.exit("timed out; inspect log.txt and take a screenshot")


def print_rows(rows):
    cols = ["run_id", "label", "docked", "cpu_mhz", "gpu_mhz", "emc_mhz", "cpu_mode", "fps", "speed_pct", "low1_fps",
            "ft_p99_ms", "ft_max_ms", "guest0_pct", "guest1_pct", "guest2_pct", "latte_pct", "recompiler_pct",
            "compilers_pct", "host_gpu_pct", "host_gpu_ms", "idle_ms", "fence_ms", "host_gpu_wait_ms", "present_ms",
            "async_ms", "shader_ms", "settings"]
    print("\t".join(cols))
    for row in rows:
        print("\t".join(row.get(c, "") for c in cols))


def cmd_results(args):
    with ftp_connect() as ftp:
        rows = read_rows(ftp)
        if args.label:
            rows = [r for r in rows if r["label"] in args.label]
        print_rows(rows[-args.last:])
        if args.frames:
            os.makedirs(args.frames, exist_ok=True)
            for row in rows[-args.last:]:
                name = f"bench-{row['run_id']}.frames.txt"
                text = ftp_read(ftp, f"{FTP_LOGS}/{name}")
                if text is not None:
                    with open(os.path.join(args.frames, name), "w") as f:
                        f.write(text)
        if args.summary:
            by_label = {}
            for row in rows:
                by_label.setdefault(row["label"], []).append(float(row["speed_pct"]))
            for label, speeds in by_label.items():
                spread = statistics.stdev(speeds) if len(speeds) > 1 else 0.0
                print(f"{label}: n={len(speeds)} speed {statistics.mean(speeds):.2f}% +/- {spread:.2f}")


def cmd_fetch(args):
    with ftp_connect() as ftp:
        text = ftp_read(ftp, f"{FTP_LOGS}/{args.name}")
    if text is None:
        sys.exit(f"{args.name} not found")
    sys.stdout.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("hbmenu", help="launch hbmenu")
    p.add_argument("--screenshot")
    p.set_defaults(func=cmd_hbmenu)

    p = sub.add_parser("screenshot", help="save the current screen as a JPEG")
    p.add_argument("path")
    p.set_defaults(func=cmd_screenshot)

    p = sub.add_parser("menu", help="open Kankohi's pause menu")
    p.add_argument("--screenshot")
    p.set_defaults(func=cmd_menu)

    p = sub.add_parser("click", help="press buttons in order")
    p.add_argument("buttons", nargs="+")
    p.add_argument("--delay", type=float, default=0.4)
    p.set_defaults(func=cmd_click)

    p = sub.add_parser("run", help="start hbmenu's netloader, send the NRO in benchmark mode, and wait for the result")
    p.add_argument("nro")
    p.add_argument("image", help="path relative to sdmc:/switch/kankohi, or an sdmc: path")
    p.add_argument("--duration", type=float, default=60)
    p.add_argument("--warmup", type=float, default=15)
    p.add_argument("--replay-input", help="input recording to play back (create with --record-input)")
    p.add_argument("--label", default="")
    p.add_argument("--set", action="append", default=[], metavar="SECTION/KEY=VALUE",
                   help="setting override for this run only such as Graphic/VSync=1 or GameProfile/CPU/cpuMode=3")
    p.add_argument("--timeout", type=float, default=600)
    p.add_argument("--netloader-open", action="store_true", help="hbmenu's netloader is already listening")
    p.set_defaults(func=cmd_run)

    p = sub.add_parser("results", help="print bench.csv rows")
    p.add_argument("--last", type=int, default=20)
    p.add_argument("--label", nargs="*")
    p.add_argument("--frames", help="also download frame-time files into this directory")
    p.add_argument("--summary", action="store_true", help="mean and spread of speed per label")
    p.set_defaults(func=cmd_results)

    p = sub.add_parser("fetch", help="print a file from the logs directory")
    p.add_argument("name")
    p.set_defaults(func=cmd_fetch)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
