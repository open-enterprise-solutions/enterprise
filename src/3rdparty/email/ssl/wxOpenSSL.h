/////////////////////////////////////////////////////////////////////////////
// Name:        wxOpenSSL.h
// Purpose:     This header defines all openssl stuff that will be used by this library
/////////////////////////////////////////////////////////////////////////////

/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#ifdef  __cplusplus
extern "C" {
#endif

#define STACK_OF(type) struct stack_st_##type

	struct crypto_ex_data_st
	{
		STACK_OF(void) *sk;
		int dummy; /* gcc is screwing up this data structure :-( */
	};

	typedef struct crypto_ex_data_st CRYPTO_EX_DATA;

	struct bio_method_st;

	struct bio_st
	{
		struct bio_method_st *method;
		/* bio, mode, argp, argi, argl, ret */
		long(*callback)(struct bio_st *, int, const char *, int, long, long);
		char *cb_arg; /* first argument for the callback */

		int init;
		int shutdown;
		int flags;  /* extra storage */
		int retry_reason;
		int num;
		void *ptr;
		struct bio_st *next_bio;   /* used by filter BIOs */
		struct bio_st *prev_bio;   /* used by filter BIOs */
		int references;
		unsigned long num_read;
		unsigned long num_write;

		CRYPTO_EX_DATA ex_data;
	};

	typedef struct bio_st BIO;

	typedef void bio_info_cb(struct bio_st *, int, const char *, int, long, long);

	typedef struct bio_method_st
	{
		int type;
		const char *name;
		int(*bwrite)(BIO *, const char *, int);
		int(*bread)(BIO *, char *, int);
		int(*bputs)(BIO *, const char *);
		int(*bgets)(BIO *, char *, int);
		long(*ctrl)(BIO *, int, long, void *);
		int(*create)(BIO *);
		int(*destroy)(BIO *);
		long(*callback_ctrl)(BIO *, int, bio_info_cb *);
	} BIO_METHOD;


	struct SSL_CTX;
	struct SSL;
	struct SSL_CIPHER;

	typedef struct ssl_method_st
	{
		int version;
		int(*ssl_new)(SSL *s);
		void(*ssl_clear)(SSL *s);
		void(*ssl_free)(SSL *s);
		int(*ssl_accept)(SSL *s);
		int(*ssl_connect)(SSL *s);
		int(*ssl_read)(SSL *s, void *buf, int len);
		int(*ssl_peek)(SSL *s, void *buf, int len);
		int(*ssl_write)(SSL *s, const void *buf, int len);
		int(*ssl_shutdown)(SSL *s);
		int(*ssl_renegotiate)(SSL *s);
		int(*ssl_renegotiate_check)(SSL *s);
		long(*ssl_get_message)(SSL *s, int st1, int stn, int mt, long
			max, int *ok);
		int(*ssl_read_bytes)(SSL *s, int type, unsigned char *buf, int len,
			int peek);
		int(*ssl_write_bytes)(SSL *s, int type, const void *buf_, int len);
		int(*ssl_dispatch_alert)(SSL *s);
		long(*ssl_ctrl)(SSL *s, int cmd, long larg, void *parg);
		long(*ssl_ctx_ctrl)(SSL_CTX *ctx, int cmd, long larg, void *parg);
		const SSL_CIPHER *(*get_cipher_by_char)(const unsigned char *ptr);
		int(*put_cipher_by_char)(const SSL_CIPHER *cipher, unsigned char *ptr);
		int(*ssl_pending)(const SSL *s);
		int(*num_ciphers)(void);
		const SSL_CIPHER *(*get_cipher)(unsigned ncipher);
		const struct ssl_method_st *(*get_ssl_method)(int version);
		long(*get_timeout)(void);
		struct ssl3_enc_method *ssl3_enc; /* Extra SSLv3/TLS stuff */
		int(*ssl_version)(void);
		long(*ssl_callback_ctrl)(SSL *s, int cb_id, void(*fp)(void));
		long(*ssl_ctx_callback_ctrl)(SSL_CTX *s, int cb_id, void(*fp)(void));
	} SSL_METHOD;

#define BIO_TYPE_SOCKET    (5|0x0400|0x0100)

#define SSL_ERROR_WANT_READ      2
#define SSL_ERROR_WANT_WRITE     3

#ifdef __cplusplus
}
#endif
