#include "HorizonDiagnostics.h"

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>

#include <switch.h>

extern "C" int getentropy(void* buffer, size_t length)
{
	// OpenBSD semantics that arc4random needs
	if (length > 256)
	{
		errno = EIO;
		return -1;
	}
	randomGet(buffer, length);
	return 0;
}

extern "C" void vsyslog(int priority, const char* format, va_list args)
{
	char message[512];
	if (std::vsnprintf(message, sizeof(message), format, args) >= 0)
		HorizonDiagnostics::Log("syslog(%d): %s", priority, message);
}

extern "C" void syslog(int priority, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vsyslog(priority, format, args);
	va_end(args);
}
