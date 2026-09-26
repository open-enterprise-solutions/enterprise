#ifndef __THREADING_ALT_H__
#define __THREADING_ALT_H__

/* The mutex Mbed TLS locks with on Windows, where its own implementation is pthreads and there are none.
 * Included by Mbed TLS's threading.h from C, so it says its type here and nothing else; the four functions
 * that go with it are handed over by mbedtls_threading_set_alt (backend/system/value/valueHttp.cpp).
 *
 * ⚠ A POINTER AND NOT THE LOCK ITSELF, so that this header names no Windows type and includes no Windows
 * header: <windows.h> here reaches every C file of the library, and the winsock it carries is the OLD one -
 * the one whose sockaddr the winsock2.h of its own net_sockets.c then redefines. What the pointer points at
 * is a critical section, made and destroyed by those four functions; null means the mutex was never made,
 * and a lock on it answers an error rather than entering nothing, which is what the library asks of an
 * alternative. */

typedef struct mbedtls_threading_mutex_t {
	void* m_section;
} mbedtls_threading_mutex_t;

#endif /* !__THREADING_ALT_H__ */
