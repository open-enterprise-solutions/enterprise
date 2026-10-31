#ifndef __CORE_NET_TLS_STREAM_H__
#define __CORE_NET_TLS_STREAM_H__

// TLS OVER A CONNECTION — Mbed TLS, the build's own (core/tls), driven without a socket of its own: what it
// writes is collected and handed to the socket, what the socket reads is handed to it, so a connection waiting for
// its peer holds no thread here either. TLS 1.2 and 1.3. The server's side — the web server reached by browsers, the
// application server's clients, the assistant's access — and the client's (httpClient.h): a script's HTTPS, and an
// application server's oess:// address, a thin client's and the web server's alike (protocol/connectionServer.h).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "httpStream.h"

enum class ibHttpClientError;   // httpClient.h — why a client was not prepared

// THE SERVER'S CERTIFICATE AND KEY, and the configuration every connection's TLS is made from — read once, shared
// by all of them (Mbed TLS reads a configuration from many contexts at once; it is not changed after Load).
class ibTlsContext {
public:

	ibTlsContext();
	~ibTlsContext();

	ibTlsContext(const ibTlsContext&) = delete;
	ibTlsContext& operator=(const ibTlsContext&) = delete;

	// PEM text — the certificate (its chain after it) and its key; false with why in `error`. A key under a password
	// is refused: there is no word for the password.
	bool Load(const std::string& certificate, const std::string& privateKey, std::string& error);

	// THE CLIENT'S — whom it trusts and what it shows. `trusted`: PEM text of the authorities a server's chain must
	// end in; empty — the platform's own store (the Windows store, the macOS keychain, /etc/ssl/certs). `certificate`:
	// PEM text holding the client's certificate and its key, one file, the key opened with `password`; empty — none is
	// shown. `verify` — the server's chain and its name checked; off, any server is spoken to. ::Trusted or
	// ::Certificate — which of them is not readable, with why in `error`.
	ibHttpClientError LoadClient(const std::string& trusted, const std::string& certificate, const std::string& password,
		bool verify, std::string& error);

	const mbedtls_ssl_config* GetConfig() const { return &m_config; }

private:
	// TLS 1.3's key schedule goes through PSA, and every handshake draws on the random generator: both made first.
	bool Prepare(const char* personal, std::string& error);

	mbedtls_entropy_context  m_entropy;
	mbedtls_ctr_drbg_context m_random;
	mbedtls_x509_crt         m_certificate;
	mbedtls_pk_context       m_key;
	mbedtls_x509_crt         m_trusted;   // the client's authorities
	mbedtls_ssl_config       m_config;
};

class ibTlsStream : public ibHttpStream {
public:

	ibTlsStream(asio::ip::tcp::socket socket, std::shared_ptr<const ibTlsContext> context);
	// …the client's, to `host`: the name sent (SNI) and the name the server's certificate is checked against.
	ibTlsStream(asio::ip::tcp::socket socket, std::shared_ptr<const ibTlsContext> context, const std::string& host);
	virtual ~ibTlsStream();

	// After a handshake that failed: why the server's certificate was not taken (MBEDTLS_X509_BADCERT_*), 0 — it was,
	// or the failure was not about it.
	std::uint32_t GetVerification() const;

	virtual void Handshake(std::function<void(const std::error_code& error)> done) override;
	virtual void ReadSome(char* buffer, std::size_t size, ibDone done) override;
	virtual void Write(const char* data, std::size_t size, ibDone done) override;
	virtual void Close() override;

	virtual asio::ip::tcp::socket& GetSocket() override { return m_socket; }

private:

	// Mbed TLS's two ends of the socket: what it sends is collected (m_cipherOut), what it receives is what the
	// socket has read (m_cipherIn) — none yet is "want read".
	static int Send(void* stream, const unsigned char* data, std::size_t size);
	static int Receive(void* stream, unsigned char* data, std::size_t size);

	// What Mbed TLS has written, handed to the socket; `done` once all of it is there. Pump — one write on the socket,
	// and the next when more has been written meanwhile.
	void Flush(std::function<void(const std::error_code& error)> done);
	void Pump(std::shared_ptr<std::vector<std::function<void(const std::error_code& error)>>> waiting);
	// More from the socket for Mbed TLS to read.
	void Fill(std::function<void(const std::error_code& error)> done);

	void Handshaking(std::function<void(const std::error_code& error)> done);

	asio::ip::tcp::socket                m_socket;
	std::shared_ptr<const ibTlsContext>  m_context;
	mbedtls_ssl_context                  m_ssl;
	int                                  m_setup = 0;   // mbedtls_ssl_setup's answer: not 0 — no handshake is begun

	std::string m_cipherIn;
	std::size_t m_cipherInAt = 0;
	std::string m_cipherOut;
	std::string m_sending;      // on the socket now
	// Waiting for the socket to take it, while a write is on it — HELD BY THAT WRITE, the stream only finds it. A waiter
	// holds the stream's owner: held here, it would hold the stream itself, and a context stopped with a write on the
	// socket (its handler destroyed, never run) would leave the owner and the stream holding each other for good.
	std::weak_ptr<std::vector<std::function<void(const std::error_code& error)>>> m_flushed;
	char        m_raw[16 * 1024];
};

#endif
