// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonDiagnostics.h"

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <ctime>

#include <dirent.h>
#include <malloc.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

extern "C" time_t timegm(struct tm* tm)
{
	long long year = tm->tm_year + 1900LL + tm->tm_mon / 12;
	int month = tm->tm_mon % 12;
	if (month < 0)
	{
		month += 12;
		year--;
	}
	year -= month < 2;
	const long long era = (year >= 0 ? year : year - 399) / 400;
	const long long yearOfEra = year - era * 400;
	const long long dayOfYear = (153 * (month + (month < 2 ? 10 : -2)) + 2) / 5 + tm->tm_mday - 1;
	const long long dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
	const long long days = era * 146097 + dayOfEra - 719468;
	return (time_t)(days * 86400 + tm->tm_hour * 3600LL + tm->tm_min * 60LL + tm->tm_sec);
}

extern "C" ssize_t getrandom(void* buffer, size_t length, unsigned int flags)
{
	randomGet(buffer, length);
	return (ssize_t)length;
}

extern "C" int posix_memalign(void** memptr, size_t alignment, size_t size)
{
	if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0)
		return EINVAL;
	void* pointer = memalign(alignment, size);
	if (!pointer)
		return ENOMEM;
	*memptr = pointer;
	return 0;
}

extern "C" uid_t getuid()
{
	return 0;
}

extern "C" uid_t geteuid()
{
	return 0;
}

extern "C" gid_t getgid()
{
	return 0;
}

extern "C" gid_t getegid()
{
	return 0;
}

extern "C" long sysconf(int name)
{
	switch (name)
	{
	case _SC_PAGESIZE:
		return 0x1000;
	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN:
		return 4;
	case _SC_PHYS_PAGES:
	{
		u64 totalMemory = 0;
		if (R_FAILED(svcGetInfo(&totalMemory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
			return -1;
		return (long)(totalMemory / 0x1000);
	}
	default:
		return -1;
	}
}

extern "C" int regcomp(regex_t* preg, const char* regex, int cflags)
{
	if (preg)
		preg->re_nsub = 0;
	return 0;
}

extern "C" int regexec(const regex_t* preg, const char* string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
	return REG_NOMATCH;
}

extern "C" void regfree(regex_t* preg)
{
}

extern "C" int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset)
{
	if (oldset)
		*oldset = 0;
	return 0;
}

extern "C" int getpwuid_r(uid_t uid, struct passwd* pwd, char* buf, size_t buflen, struct passwd** result)
{
	*result = nullptr;
	return ENOENT;
}

extern "C" int dirfd(DIR* dirp)
{
	errno = ENOTSUP;
	return -1;
}

extern "C" int fstatat(int fd, const char* __restrict path, struct stat* __restrict buf, int flag)
{
	errno = ENOSYS;
	return -1;
}

extern "C" int flock(int fd, int operation)
{
	errno = ENOSYS;
	return -1;
}
