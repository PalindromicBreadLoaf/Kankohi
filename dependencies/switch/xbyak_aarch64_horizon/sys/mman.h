#pragma once
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void*)-1)

static inline void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	errno = ENOSYS;
	return MAP_FAILED;
}

static inline int munmap(void* addr, size_t length)
{
	errno = ENOSYS;
	return -1;
}

static inline int mprotect(void* addr, size_t length, int prot)
{
	errno = ENOSYS;
	return -1;
}
