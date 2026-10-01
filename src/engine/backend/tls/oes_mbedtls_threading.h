#ifndef __OES_MBEDTLS_THREADING_H__
#define __OES_MBEDTLS_THREADING_H__

/* The mutexes Mbed TLS locks with on Windows, handed to it when a module that uses it is loaded.
 *
 * Mbed TLS implements mutexes for pthreads only. Everywhere else it takes them from whoever builds it
 * (MBEDTLS_THREADING_ALT, oes_mbedtls_config.h says why they are needed at all), and until it is given them
 * every lock it takes ANSWERS AN ERROR - so a TLS context cannot even be created. That is why this is not
 * done on the way to the first connection: by then something else may already have tried.
 *
 * ⚠ ONCE PER MODULE, because Mbed TLS is a static library: the engine's copy and a test binary's copy each
 * have their own function pointers and their own global mutexes. Include this header in exactly one
 * translation unit of every module that uses Mbed TLS, after httplib.h (which is what says whether TLS is
 * compiled in at all). Two inclusions in one module would hand the mutexes over twice and leak the first set.
 *
 * Today that is valueHttp.cpp (the engine) and tests/test_valueHttpSecure.cpp (the test binary). The web
 * server compiles httplib.h too and links Mbed TLS through the engine, but builds only a plain Server and
 * never creates a TLS context; the day it wants one, this header belongs in it as well.
 */

#if defined(CPPHTTPLIB_SSL_ENABLED) && defined(_WIN32)

#include <mbedtls/threading.h>

#include <new>

namespace {

// Never throws: the library's init hands back no status, and a mutex that was not made is one whose lock
// answers an error (below). A pointer, because the type in threading_alt.h names no Windows type.
extern "C" inline void ibMbedMutexInit(mbedtls_threading_mutex_t* mutex)
{
	CRITICAL_SECTION* const section = new (std::nothrow) CRITICAL_SECTION();
	if (section != nullptr)
		InitializeCriticalSection(section);
	mutex->m_section = section;
}

extern "C" inline void ibMbedMutexFree(mbedtls_threading_mutex_t* mutex)
{
	CRITICAL_SECTION* const section = static_cast<CRITICAL_SECTION*>(mutex->m_section);
	if (section == nullptr)
		return;
	mutex->m_section = nullptr;
	DeleteCriticalSection(section);
	delete section;
}

extern "C" inline int ibMbedMutexLock(mbedtls_threading_mutex_t* mutex)
{
	if (mutex->m_section == nullptr)
		return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
	EnterCriticalSection(static_cast<CRITICAL_SECTION*>(mutex->m_section));
	return 0;
}

extern "C" inline int ibMbedMutexUnlock(mbedtls_threading_mutex_t* mutex)
{
	if (mutex->m_section == nullptr)
		return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
	LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(mutex->m_section));
	return 0;
}

// Given at load, taken back at unload. Not free: mbedtls_threading_set_alt initialises Mbed TLS's own
// global mutexes on the spot, so a handful of critical sections are made here, during static
// initialisation - which on Windows is inside the loader lock. Making a critical section takes no lock
// of its own, so that is safe; it is said here because it is not obvious from the call.
struct ibMbedThreading {
	ibMbedThreading()
	{
		mbedtls_threading_set_alt(&ibMbedMutexInit, &ibMbedMutexFree, &ibMbedMutexLock, &ibMbedMutexUnlock);
	}
	~ibMbedThreading() { mbedtls_threading_free_alt(); }
};

const ibMbedThreading s_mbedThreading;

} // namespace

#endif

#endif /* !__OES_MBEDTLS_THREADING_H__ */
