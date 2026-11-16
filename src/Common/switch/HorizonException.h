#pragma once

#include <cstdint>

namespace HorizonException
{
	struct Fault
	{
		uintptr_t address;
		uintptr_t pc;
		bool isWrite;
		void* dump;
	};

	using Handler = bool (*)(Fault& fault);

	bool AddDataAbortHandler(Handler handler);
	void RemoveDataAbortHandler(Handler handler);

	void OpenCrashLog(const char* path);
	void CloseCrashLog();

	uint64_t GetRegister(const Fault& fault, int index);
	void SetRegister(Fault& fault, int index, uint64_t value);
	void SetPc(Fault& fault, uintptr_t pc);
	void SetSp(Fault& fault, uintptr_t sp);
}
