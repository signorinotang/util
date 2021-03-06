//
// Created by hujianzhe
//

#ifndef	UTIL_C_SYSLIB_PROCESS_H
#define	UTIL_C_SYSLIB_PROCESS_H

#include "platform_define.h"

#if defined(_WIN32) || defined(_WIN64)
	#include <process.h>
/*
	#ifdef _MSC_VER
		#pragma warning(disable:4091)// avoid bug(dbghelp.h warning C4091: "typedef ")
	#endif
	#include <Dbghelp.h>
	#ifdef _MSC_VER
		#pragma warning(default:4091)
	#endif
*/
	typedef struct {
		HANDLE handle;
		DWORD id;
	} Process_t;
	#define	__dllexport				__declspec(dllexport)
	#define	__dllimport				__declspec(dllimport)
	#define	DLL_CALL				__stdcall
	typedef HANDLE					Thread_t;
	#define	THREAD_CALL				__stdcall
	typedef	DWORD					Tls_t;
	#define	__tls					__declspec(thread)
	#pragma comment(lib, "Dbghelp.lib")
#else
	#include <dlfcn.h>
	#include <pthread.h>
	#include <sys/select.h>
	#include <sys/time.h>
	#include <sys/wait.h>
/*
	#include <execinfo.h>
	#include <ucontext.h>
*/
	typedef struct {
		pid_t id;
	} Process_t;
	#define	__dllexport
	#define	__dllimport
	#define	DLL_CALL
	typedef pthread_t				Thread_t;
	#define	THREAD_CALL
	typedef	pthread_key_t			Tls_t;
	#define	__tls					__thread
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* process operator */
EXEC_RETURN process_Create(Process_t* p_process, const char* path, const char* cmdarg);
EXEC_RETURN process_Cancel(Process_t* process);
size_t process_Id(void);
EXEC_RETURN process_TryFreeZombie(Process_t* process, unsigned char* retcode);
void* process_LoadModule(const char* path);
void* process_GetModuleSymbolAddress(void* handle, const char* symbol_name);
EXEC_RETURN process_UnloadModule(void* handle);
/* thread operator */
EXEC_RETURN thread_Create(Thread_t* p_thread, unsigned int (THREAD_CALL *entry)(void*), void* arg);
EXEC_RETURN thread_Detach(Thread_t thread);
EXEC_RETURN thread_Join(Thread_t thread, unsigned int* retcode);
void thread_Exit(unsigned int retcode);
#if defined(_WIN32) || defined(_WIN64)
#define	thread_Self()		GetCurrentThread()
#define	thread_Pause()		SuspendThread(GetCurrentThread())
#else
#define	thread_Self()		pthread_self()
#define	thread_Pause()		pause()
#endif
void thread_Sleep(unsigned int msec);
void thread_Yield(void);
EXEC_RETURN thread_SetAffinity(Thread_t thread, unsigned int processor_index);
/* thread local operator */
EXEC_RETURN thread_AllocLocalKey(Tls_t* key);
EXEC_RETURN thread_SetLocalValue(Tls_t key, void* value);
void* thread_GetLocalValue(Tls_t key);
EXEC_RETURN thread_FreeLocalKey(Tls_t key);

#ifdef	__cplusplus
}
#endif

#endif
