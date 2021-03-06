//
// Created by hujianzhe
//

#include "memory.h"
#if !defined(_WIN32) && !defined(_WIN64)
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* memory align alloc */
void* crt_AlignMalloc(size_t nbytes, int alignment) { /* alignment must signed integer type ! */
	if (alignment <= 0) {
		return NULL;
	}
	size_t padsize = alignment > sizeof(size_t) ? alignment : sizeof(size_t);
	size_t ptr = (size_t)malloc(nbytes + padsize);
	if (NULL == (void*)ptr) {
		return NULL;
	}
	size_t new_ptr = (ptr + sizeof(size_t) + alignment - 1) & ~(alignment - 1);
	*(((size_t*)new_ptr) - 1) = new_ptr - ptr;
	return (void*)new_ptr;
}
void crt_AlignFree(void* ptr) {
	if (ptr) {
		size_t off = *(((size_t*)ptr) - 1);
		free((char*)ptr - off);
	}
}

/* mmap */
static long __granularity(void) {
#if defined(_WIN32) || defined(_WIN64)
	SYSTEM_INFO sinfo = {0};
	GetSystemInfo(&sinfo);
	return sinfo.dwAllocationGranularity;
#else
	long granularity = sysconf(_SC_PAGESIZE);
	if (granularity == -1)
		granularity = 0;
	return granularity;
#endif
}

EXEC_RETURN mmap_Create(MemoryMapping_t* mm, FD_t fd, const char* name, size_t nbytes) {
	mm->granularity = __granularity();
	if (fd != INVALID_FD_HANDLE) {
#if defined(_WIN32) || defined(_WIN64)
		mm->__handle = CreateFileMappingA((HANDLE)fd, NULL, PAGE_READWRITE, 0, 0, NULL);
		return mm->__handle ? EXEC_SUCCESS : EXEC_ERROR;
#else
		mm->__fd = fd;
#endif
	}
	else {
#if defined(_WIN32) || defined(_WIN64)
		HANDLE handle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, ((long long)nbytes) >> 32, nbytes, name);
		if (GetLastError() == ERROR_ALREADY_EXISTS) {
			assert_true(CloseHandle(handle));
			return EXEC_ERROR;
		}
		mm->__handle = handle;
#else
		int fd = open(name, O_CREAT|O_EXCL|O_TRUNC|O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
		if (-1 == fd) {
			return EXEC_ERROR;
		}
		if (ftruncate(fd, nbytes)) {
			assert_true(close(fd) == 0);
			return EXEC_ERROR;
		}
		mm->__fd = fd;
#endif
	}
	return EXEC_SUCCESS;
}

EXEC_RETURN mmap_Open(MemoryMapping_t* mm, const char* name) {
#if defined(_WIN32) || defined(_WIN64)
	mm->__handle = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
	return mm->__handle ? EXEC_SUCCESS : EXEC_ERROR;
#else
	mm->__fd = open(name, O_RDWR);
	return mm->__fd != -1 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

EXEC_RETURN mmap_Close(MemoryMapping_t* mm) {
#if defined(_WIN32) || defined(_WIN64)
	return CloseHandle((HANDLE)(mm->__handle)) ? EXEC_SUCCESS : EXEC_ERROR;
#else
	return close(mm->__fd) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

void* mmap_Map(MemoryMapping_t* mm, void* va_base, long long offset, size_t nbytes) {
#if defined(_WIN32) || defined(_WIN64)
	return MapViewOfFileEx((HANDLE)(mm->__handle), FILE_MAP_READ | FILE_MAP_WRITE, offset >> 32, (DWORD)offset, nbytes, va_base);
#else
	return mmap(va_base, nbytes, PROT_READ | PROT_WRITE, MAP_SHARED, mm->__fd, offset);
#endif
}

EXEC_RETURN mmap_Sync(void* addr, size_t nbytes) {
#if defined(_WIN32) || defined(_WIN64)
	return FlushViewOfFile(addr, nbytes) ? EXEC_SUCCESS : EXEC_ERROR;
#else
	return msync(addr, nbytes, MS_SYNC) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

EXEC_RETURN mmap_Unmap(void* addr, size_t nbytes) {
#if defined(_WIN32) || defined(_WIN64)
	return UnmapViewOfFile(addr) ? EXEC_SUCCESS : EXEC_ERROR;
#else
	return munmap(addr, nbytes) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

#ifdef	__cplusplus
}
#endif
