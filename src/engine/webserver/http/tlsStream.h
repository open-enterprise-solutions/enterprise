#ifndef __WEBSERVER_TLS_STREAM_H__
#define __WEBSERVER_TLS_STREAM_H__

// TLS OVER A CONNECTION — Mbed TLS, the build's own (backend/tls), driven without a socket of its own: what it
// writes is collected and handed to the socket, what the socket reads is handed to it, so a connection waiting for
// its peer holds no thread here either. TLS 1.2 and 1.3; the server's side only — the web server is reached by
// browsers, and reaches its application servers in the clear, on their own network, as a thin client does
// (protocol/connectionServer.h).

#include <memory>
#include <string>
#include <vector>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "httpStream.h"

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

	const mbedtls_ssl_config* GetConfig() const { return &m_config; }

private:
	mbedtls_entropy_context  m_entropy;
	mbedtls_ctr_drbg_context m_random;
	mbedtls_x509_crt         m_certificate;
	mbedtls_pk_context       m_key;
	mbedtls_ssl_config       m_config;
};

class ibTlsStream : public ibHttpStream {
public:

	ibTlsStream(asio::ip::tcp::socket socket, std::shared_ptr<const ibTlsContext> context);
	virtual ~ibTlsStream();

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
	void Pump();
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
	bool        m_flushing = false;
	std::vector<std::function<void(const std::error_code& error)>> m_flushed;   // waiting for the socket to take it
	char        m_raw[16 * 1024];
};

#endif
