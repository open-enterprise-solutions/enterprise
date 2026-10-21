#ifndef __OES_MBEDTLS_CONFIG_H__
#define __OES_MBEDTLS_CONFIG_H__

/* What this build needs on top of Mbed TLS's own configuration (MBEDTLS_USER_CONFIG_FILE, read after it).
 *
 * THREADS. The engine runs script on a worker pool, in background jobs and inside two servers, so two
 * sessions can be in a TLS handshake at the same moment. Mbed TLS says it plainly beside its TLS 1.3 switch:
 * "In multithreaded applications, you must also enable MBEDTLS_THREADING_C, even if individual TLS contexts
 * are not shared between threads" - the protocol's key schedule goes through PSA, whose state is one per
 * process. Without this the second handshake corrupts the first, rarely and unreproducibly.
 *
 * Mbed TLS implements mutexes for pthreads only, so Windows takes the other road its threading.h offers:
 * the mutex type is ours (threading_alt.h, beside this file) and the four functions are handed over with
 * mbedtls_threading_set_alt, which the HTTP client calls once before it makes its first connection. */

#define MBEDTLS_THREADING_C

#if defined(_WIN32)
#define MBEDTLS_THREADING_ALT
#else
#define MBEDTLS_THREADING_PTHREAD
#endif

#endif /* !__OES_MBEDTLS_CONFIG_H__ */
