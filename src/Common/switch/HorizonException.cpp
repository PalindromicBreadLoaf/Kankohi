// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonException.h"
#include "HorizonDiagnostics.h"
#include "HorizonExceptionEntry.h"

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

extern "C" char __end__[];

static_assert(sizeof(CpuRegister) == 8);
static_assert(sizeof(FpuRegister) == 16);
static_assert(offsetof(ThreadExceptionDump, error_desc) == DUMP_ERROR_DESC);
static_assert(offsetof(ThreadExceptionDump, cpu_gprs) == DUMP_GPRS);
static_assert(offsetof(ThreadExceptionDump, fp) == DUMP_FP);
static_assert(offsetof(ThreadExceptionDump, lr) == DUMP_LR);
static_assert(offsetof(ThreadExceptionDump, sp) == DUMP_SP);
static_assert(offsetof(ThreadExceptionDump, pc) == DUMP_PC);
static_assert(offsetof(ThreadExceptionDump, padding) == DUMP_PADDING);
static_assert(offsetof(ThreadExceptionDump, fpu_gprs) == DUMP_FPU);
static_assert(offsetof(ThreadExceptionDump, pstate) == DUMP_PSTATE);
static_assert(offsetof(ThreadExceptionDump, far) == DUMP_FAR);
static_assert(sizeof(ThreadExceptionDump) <= SLOT_FRAME);
static_assert(SLOT_INDEX + sizeof(u64) <= SLOT_SIZE);
static_assert(offsetof(ThreadExceptionDump, esr) == DUMP_PSTATE + 12);
static_assert(offsetof(ThreadExceptionFrameA64, esr) == FRAME_PSTATE + 12);
static_assert(offsetof(ThreadExceptionFrameA64, lr) == FRAME_LR);
static_assert(offsetof(ThreadExceptionFrameA64, sp) == FRAME_SP);
static_assert(offsetof(ThreadExceptionFrameA64, elr_el1) == FRAME_PC);
static_assert(offsetof(ThreadExceptionFrameA64, pstate) == FRAME_PSTATE);
static_assert(offsetof(ThreadExceptionFrameA64, far) == FRAME_FAR);

namespace
{
	constexpr u32 ESR_EC_DATA_ABORT_LOWER = 0x24;
	constexpr u32 ESR_EC_DATA_ABORT_SAME = 0x25;
	constexpr u32 ESR_EC_BRK_AARCH64 = 0x3C;
	constexpr u32 ESR_ISS_WNR = 1u << 6;
	constexpr u32 BRK_BUILTIN_TRAP = 1000;
	constexpr int MAX_HANDLERS = 8;

	std::atomic<HorizonException::Handler> s_handlers[MAX_HANDLERS];
	int s_crashFd = -1;

	u32 ExceptionClass(u32 esr)
	{
		return (esr >> 26) & 0x3F;
	}

	class DumpWriter
	{
	public:
		void Printf(const char* format, ...) __attribute__((format(printf, 2, 3)))
		{
			va_list args;
			va_start(args, format);
			const int length = std::vsnprintf(m_buffer, sizeof(m_buffer), format, args);
			va_end(args);
			if (length <= 0)
				return;
			const size_t size = (size_t)length < sizeof(m_buffer) ? (size_t)length : sizeof(m_buffer) - 1;
			if (s_crashFd >= 0)
				(void)write(s_crashFd, m_buffer, size);
			(void)write(STDOUT_FILENO, m_buffer, size);
		}

	private:
		char m_buffer[256];
	};

	const char* DescribeError(u32 errorDesc)
	{
		switch (errorDesc)
		{
		case ThreadExceptionDesc_InstructionAbort:
			return "instruction abort";
		case ThreadExceptionDesc_MisalignedPC:
			return "misaligned PC";
		case ThreadExceptionDesc_MisalignedSP:
			return "misaligned SP";
		case ThreadExceptionDesc_SError:
			return "SError";
		case ThreadExceptionDesc_BadSVC:
			return "bad SVC";
		case ThreadExceptionDesc_Trap:
			return "trap";
		case ThreadExceptionDesc_Other:
			return "data abort or other";
		default:
			return "unknown";
		}
	}

	size_t GetReadableWordCount(u64 address, size_t maximum)
	{
		if ((address & 7) != 0)
			return 0;
		MemoryInfo info{};
		u32 pageInfo;
		if (R_FAILED(svcQueryMemory(&info, &pageInfo, address)) || (info.perm & Perm_R) == 0 || address < info.addr)
			return 0;
		const u64 count = (info.addr + info.size - address) / sizeof(u64);
		return count < maximum ? (size_t)count : maximum;
	}

	void PrintAddress(DumpWriter& out, const char* label, u64 value)
	{
		const u64 base = HorizonDiagnostics::GetModuleBase();
		const u64 end = (u64)&__end__;
		if (value >= base && value < end)
			out.Printf("%s %016lx (+%lx)\n", label, value, value - base);
		else
			out.Printf("%s %016lx\n", label, value);
	}

	void WriteHeader(DumpWriter& out)
	{
		out.Printf("build id %s\n", HorizonDiagnostics::GetBuildId());
		out.Printf("module base %016lx\n", HorizonDiagnostics::GetModuleBase());
	}

	void FinishReport()
	{
		if (s_crashFd >= 0)
			(void)fsync(s_crashFd);
	}
}

extern "C" bool HorizonException_Dispatch(ThreadExceptionDump* ctx)
{
	if (!threadExceptionIsAArch64(ctx))
		return false;
	const u32 exceptionClass = ExceptionClass(ctx->esr);
	if (exceptionClass != ESR_EC_DATA_ABORT_LOWER && exceptionClass != ESR_EC_DATA_ABORT_SAME)
		return false;

	HorizonException::Fault fault{ctx->far.x, ctx->pc.x, (ctx->esr & ESR_ISS_WNR) != 0, ctx};
	for (auto& slot : s_handlers)
	{
		HorizonException::Handler handler = slot.load(std::memory_order_acquire);
		if (handler && handler(fault))
			return true;
	}
	return false;
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx)
{
	DumpWriter out;
	if (ExceptionClass(ctx->esr) == ESR_EC_BRK_AARCH64)
	{
		const u32 comment = ctx->esr & 0xFFFF;
		out.Printf("stopped at brk #%u%s\n", comment, comment == BRK_BUILTIN_TRAP ? " (__builtin_trap)" : "");
	}
	else
		out.Printf("crashed: %s (error_desc 0x%x)\n", DescribeError(ctx->error_desc), ctx->error_desc);
	WriteHeader(out);

	if (!threadExceptionIsAArch64(ctx))
		out.Printf("AArch32 exception frame sans registers\n");
	else
	{
		PrintAddress(out, "pc ", ctx->pc.x);
		PrintAddress(out, "lr ", ctx->lr.x);
		out.Printf("sp  %016lx  fp  %016lx\n", ctx->sp.x, ctx->fp.x);
		out.Printf("far %016lx  esr %08x  pstate %08x\n", ctx->far.x, ctx->esr, ctx->pstate);
		for (int i = 0; i < 29; i += 2)
		{
			if (i + 1 < 29)
				out.Printf("x%-2d %016lx  x%-2d %016lx\n", i, ctx->cpu_gprs[i].x, i + 1, ctx->cpu_gprs[i + 1].x);
			else
				out.Printf("x%-2d %016lx\n", i, ctx->cpu_gprs[i].x);
		}

		if (GetReadableWordCount(ctx->fp.x, 2) == 2)
		{
			const volatile u64* frame = (const volatile u64*)ctx->fp.x;
			PrintAddress(out, "caller", frame[1]);
		}
		const size_t wordCount = GetReadableWordCount(ctx->sp.x, 64);
		out.Printf("stack (%zu words):\n", wordCount);
		const volatile u64* stack = (const volatile u64*)ctx->sp.x;
		for (size_t i = 0; i < wordCount; i++)
		{
			char label[16];
			std::snprintf(label, sizeof(label), "  +%03zx", i * sizeof(u64));
			PrintAddress(out, label, stack[i]);
		}
	}
	FinishReport();
	svcExitProcess();
}

extern "C" void abort()
{
	DumpWriter out;
	out.Printf("abort() called\n");
	WriteHeader(out);
	PrintAddress(out, "caller", (u64)__builtin_return_address(0));
	out.Printf("below is the system crash report\n");
	FinishReport();
	svcBreak(BreakReason_Panic, 0, 0);
	__builtin_unreachable();
}

namespace HorizonException
{
	bool AddDataAbortHandler(Handler handler)
	{
		for (auto& slot : s_handlers)
		{
			Handler expected = nullptr;
			if (slot.compare_exchange_strong(expected, handler, std::memory_order_acq_rel))
				return true;
		}
		return false;
	}

	void RemoveDataAbortHandler(Handler handler)
	{
		for (auto& slot : s_handlers)
		{
			Handler expected = handler;
			slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
		}
	}

	void OpenCrashLog(const char* path)
	{
		CloseCrashLog();
		char previous[FS_MAX_PATH];
		std::snprintf(previous, sizeof(previous), "%s.prev", path);
		struct stat st;
		if (stat(path, &st) == 0 && st.st_size > 0)
		{
			unlink(previous);
			rename(path, previous);
		}
		s_crashFd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	}

	void CloseCrashLog()
	{
		if (s_crashFd < 0)
			return;
		close(s_crashFd);
		s_crashFd = -1;
	}

	static ThreadExceptionDump* Dump(const Fault& fault)
	{
		return (ThreadExceptionDump*)fault.dump;
	}

	uint64_t GetRegister(const Fault& fault, int index)
	{
		if (index < 29)
			return Dump(fault)->cpu_gprs[index].x;
		return index == 29 ? Dump(fault)->fp.x : Dump(fault)->lr.x;
	}

	void SetRegister(Fault& fault, int index, uint64_t value)
	{
		if (index < 29)
			Dump(fault)->cpu_gprs[index].x = value;
		else if (index == 29)
			Dump(fault)->fp.x = value;
		else
			Dump(fault)->lr.x = value;
	}

	void SetPc(Fault& fault, uintptr_t pc)
	{
		Dump(fault)->pc.x = pc;
	}

	void SetSp(Fault& fault, uintptr_t sp)
	{
		Dump(fault)->sp.x = sp;
	}
}
