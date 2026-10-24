#include "tlsStream.h"

#include "httpClient.h"   // ibHttpClientError — which part of a client's TLS is not readable

#include <cstring>
#include <initializer_list>

#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/write.hpp>

#if defined(_WIN32)
#include <windows.h>   // the critical sections Mbed TLS's mutexes are, below
#include <wincrypt.h>  // the system's certificate stores, a client's authorities
#elif defined(__APPLE__)
#include <Security/Security.h>   // the keychain's trust settings, a client's authorities
#endif

#include <mbedtls/error.h>
#include <psa/crypto.h>

// This module's copy of Mbed TLS takes its mutexes here, once (the header says why it is per module).
#include "oes_mbedtls_threading.h"

namespace {

std::string Describe(int code)
{
	char text[200] = {};
	mbedtls_strerror(code, text, sizeof(text));
	return text;
}

// A TLS failure as the connection sees one: the stream is not to be used again.
std::error_code TlsFailure()
{
	return std::make_error_code(std::errc::protocol_error);
}

#if defined(MBEDTLS_SSL_ALPN)
// HTTP/1.1, said: a browser offers h2 first, and a server that names nothing leaves it guessing.
const char* s_protocols[] = { "http/1.1", nullptr };
#endif

// THE PLATFORM'S AUTHORITIES, into `chain` — the ones a browser on this machine trusts: the Windows stores (the roots
// and the intermediates), the macOS keychain's trust settings, a Linux distribution's bundle. False — none was found.
bool LoadSystemTrust(mbedtls_x509_crt& chain)
{
	bool loaded = false;
#if defined(_WIN32)
	for (const wchar_t* const name : { L"ROOT", L"CA" }) {
		const HCERTSTORE store = CertOpenSystemStoreW(0, name);
		if (store == nullptr)
			continue;
		PCCERT_CONTEXT certificate = nullptr;
		while ((certificate = CertEnumCertificatesInStore(store, certificate)) != nullptr) {
			if (mbedtls_x509_crt_parse_der(&chain, certificate->pbCertEncoded, certificate->cbCertEncoded) == 0)
				loaded = true;
		}
		CertCloseStore(store, 0);
	}
#elif defined(__APPLE__)
	for (const SecTrustSettingsDomain domain : { kSecTrustSettingsDomainSystem, kSecTrustSettingsDomainAdmin,
		kSecTrustSettingsDomainUser }) {
		CFArrayRef certificates = nullptr;
		if (SecTrustSettingsCopyCertificates(domain, &certificates) != errSecSuccess || certificates == nullptr) {
			if (certificates != nullptr)
				CFRelease(certificates);
			continue;
		}
		const CFIndex count = CFArrayGetCount(certificates);
		for (CFIndex i = 0; i < count; ++i) {
			const SecCertificateRef certificate = (SecCertificateRef)CFArrayGetValueAtIndex(certificates, i);
			const CFDataRef data = SecCertificateCopyData(certificate);
			if (data == nullptr)
				continue;
			if (mbedtls_x509_crt_parse_der(&chain, CFDataGetBytePtr(data), static_cast<std::size_t>(CFDataGetLength(data))) == 0)
				loaded = true;
			CFRelease(data);
		}
		CFRelease(certificates);
	}
#else
	// A distribution's bundle where it keeps one, else its folder of them.
	for (const char* const file : { "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/ca-bundle.pem", "/etc/pki/tls/cacert.pem", "/etc/ssl/cert.pem" }) {
		if (mbedtls_x509_crt_parse_file(&chain, file) >= 0) {
			loaded = true;
			break;
		}
	}
	if (!loaded) {
		for (const char* const folder : { "/etc/ssl/certs", "/etc/pki/tls/certs", "/usr/share/ca-certificates" }) {
			if (mbedtls_x509_crt_parse_path(&chain, folder) >= 0) {
				loaded = true;
				break;
			}
		}
	}
#endif
	return loaded;
}

} // namespace

//*******************************************************************************************
//*                                     The context                                         *
//*******************************************************************************************

ibTlsContext::ibTlsContext()
{
	mbedtls_entropy_init(&m_entropy);
	mbedtls_ctr_drbg_init(&m_random);
	mbedtls_x509_crt_init(&m_certificate);
	mbedtls_pk_init(&m_key);
	mbedtls_x509_crt_init(&m_trusted);
	mbedtls_ssl_config_init(&m_config);
}

ibTlsContext::~ibTlsContext()
{
	mbedtls_ssl_config_free(&m_config);
	mbedtls_x509_crt_free(&m_trusted);
	mbedtls_pk_free(&m_key);
	mbedtls_x509_crt_free(&m_certificate);
	mbedtls_ctr_drbg_free(&m_random);
	mbedtls_entropy_free(&m_entropy);
}

bool ibTlsContext::Prepare(const char* personal, std::string& error)
{
	// PSA's state is the process's: made before the first handshake.
	const psa_status_t psa = psa_crypto_init();
	if (psa != PSA_SUCCESS) {
		error = "the crypto library did not start (PSA " + std::to_string(psa) + ")";
		return false;
	}
	const int code = mbedtls_ctr_drbg_seed(&m_random, mbedtls_entropy_func, &m_entropy,
		reinterpret_cast<const unsigned char*>(personal), std::strlen(personal));
	if (code != 0) {
		error = "no randomness for TLS: " + Describe(code);
		return false;
	}
	return true;
}

bool ibTlsContext::Load(const std::string& certificate, const std::string& privateKey, std::string& error)
{
	if (!Prepare("oes-webserver", error))
		return false;

	// PEM is read with its terminating zero counted.
	int code = mbedtls_x509_crt_parse(&m_certificate,
		reinterpret_cast<const unsigned char*>(certificate.c_str()), certificate.size() + 1);
	if (code != 0) {
		error = "the certificate is not readable: " + Describe(code);
		return false;
	}
	code = mbedtls_pk_parse_key(&m_key, reinterpret_cast<const unsigned char*>(privateKey.c_str()),
		privateKey.size() + 1, nullptr, 0, mbedtls_ctr_drbg_random, &m_random);
	if (code == MBEDTLS_ERR_PK_PASSWORD_REQUIRED) {
		error = "the private key is under a password, and there is no word for it";
		return false;
	}
	if (code != 0) {
		error = "the private key is not readable: " + Describe(code);
		return false;
	}
	code = mbedtls_pk_check_pair(&m_certificate.pk, &m_key, mbedtls_ctr_drbg_random, &m_random);
	if (code != 0) {
		error = "the private key is not the certificate's: " + Describe(code);
		return false;
	}

	code = mbedtls_ssl_config_defaults(&m_config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
		MBEDTLS_SSL_PRESET_DEFAULT);
	if (code != 0) {
		error = "TLS could not be configured: " + Describe(code);
		return false;
	}
	mbedtls_ssl_conf_rng(&m_config, mbedtls_ctr_drbg_random, &m_random);
	mbedtls_ssl_conf_min_tls_version(&m_config, MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_ALPN)
	mbedtls_ssl_conf_alpn_protocols(&m_config, s_protocols);
#endif
	code = mbedtls_ssl_conf_own_cert(&m_config, &m_certificate, &m_key);
	if (code != 0) {
		error = "the certificate could not be taken: " + Describe(code);
		return false;
	}
	return true;
}

ibHttpClientError ibTlsContext::LoadClient(const std::string& trusted, const std::string& certificate,
	const std::string& password, bool verify, std::string& error)
{
	// What fails before either file is read is the TLS's itself: said with the trusted certificates, the first asked.
	if (!Prepare("oes-httpclient", error))
		return ibHttpClientError::Trusted;

	int code = mbedtls_ssl_config_defaults(&m_config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
		MBEDTLS_SSL_PRESET_DEFAULT);
	if (code != 0) {
		error = "TLS could not be configured: " + Describe(code);
		return ibHttpClientError::Trusted;
	}
	mbedtls_ssl_conf_rng(&m_config, mbedtls_ctr_drbg_random, &m_random);
	mbedtls_ssl_conf_min_tls_version(&m_config, MBEDTLS_SSL_VERSION_TLS1_2);

	// Whom to trust: the authorities named, else the platform's. A file that holds none is said, by what is in it.
	if (!trusted.empty()) {
		code = mbedtls_x509_crt_parse(&m_trusted, reinterpret_cast<const unsigned char*>(trusted.c_str()), trusted.size() + 1);
		if (code < 0) {
			error = "the trusted certificates are not readable: " + Describe(code);
			return ibHttpClientError::Trusted;
		}
	}
	else if (verify && !LoadSystemTrust(m_trusted)) {
		error = "the platform's trusted certificates could not be read";
		return ibHttpClientError::Trusted;
	}
	mbedtls_ssl_conf_ca_chain(&m_config, &m_trusted, nullptr);
	mbedtls_ssl_conf_authmode(&m_config, verify ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);

	// What it shows, when a server asks: the certificate and its key, one PEM text — each read from it by its own
	// header; the key opened with the password.
	if (!certificate.empty()) {
		code = mbedtls_x509_crt_parse(&m_certificate, reinterpret_cast<const unsigned char*>(certificate.c_str()),
			certificate.size() + 1);
		if (code != 0) {
			error = "the client's certificate is not readable: " + Describe(code);
			return ibHttpClientError::Certificate;
		}
		code = mbedtls_pk_parse_key(&m_key, reinterpret_cast<const unsigned char*>(certificate.c_str()), certificate.size() + 1,
			password.empty() ? nullptr : reinterpret_cast<const unsigned char*>(password.data()), password.size(),
			mbedtls_ctr_drbg_random, &m_random);
		if (code == MBEDTLS_ERR_PK_PASSWORD_REQUIRED || code == MBEDTLS_ERR_PK_PASSWORD_MISMATCH) {
			error = "the client's key wants its password, and the one given does not open it";
			return ibHttpClientError::Certificate;
		}
		if (code != 0) {
			error = "the client's key is not readable: " + Describe(code);
			return ibHttpClientError::Certificate;
		}
		code = mbedtls_ssl_conf_own_cert(&m_config, &m_certificate, &m_key);
		if (code != 0) {
			error = "the client's certificate could not be taken: " + Describe(code);
			return ibHttpClientError::Certificate;
		}
	}
	return ibHttpClientError::None;
}

//*******************************************************************************************
//*                                      The stream                                         *
//*******************************************************************************************

ibTlsStream::ibTlsStream(asio::ip::tcp::socket socket, std::shared_ptr<const ibTlsContext> context) :
	m_socket(std::move(socket)), m_context(std::move(context))
{
	mbedtls_ssl_init(&m_ssl);
	m_setup = mbedtls_ssl_setup(&m_ssl, m_context->GetConfig());
	mbedtls_ssl_set_bio(&m_ssl, this, &ibTlsStream::Send, &ibTlsStream::Receive, nullptr);
}

ibTlsStream::ibTlsStream(asio::ip::tcp::socket socket, std::shared_ptr<const ibTlsContext> context, const std::string& host) :
	ibTlsStream(std::move(socket), std::move(context))
{
	if (m_setup == 0)
		m_setup = mbedtls_ssl_set_hostname(&m_ssl, host.c_str());
}

ibTlsStream::~ibTlsStream()
{
	mbedtls_ssl_free(&m_ssl);
}

std::uint32_t ibTlsStream::GetVerification() const
{
	const std::uint32_t flags = mbedtls_ssl_get_verify_result(&m_ssl);
	// Not checked (the client said not to) or not reached: nothing to say about the certificate.
	if (flags == static_cast<std::uint32_t>(-1) || flags == MBEDTLS_X509_BADCERT_SKIP_VERIFY)
		return 0;
	return flags;
}

int ibTlsStream::Send(void* stream, const unsigned char* data, std::size_t size)
{
	static_cast<ibTlsStream*>(stream)->m_cipherOut.append(reinterpret_cast<const char*>(data), size);
	return static_cast<int>(size);
}

int ibTlsStream::Receive(void* stream, unsigned char* data, std::size_t size)
{
	ibTlsStream* const self = static_cast<ibTlsStream*>(stream);
	const std::size_t held = self->m_cipherIn.size() - self->m_cipherInAt;
	if (held == 0)
		return MBEDTLS_ERR_SSL_WANT_READ;
	const std::size_t given = held < size ? held : size;
	std::memcpy(data, self->m_cipherIn.data() + self->m_cipherInAt, given);
	self->m_cipherInAt += given;
	if (self->m_cipherInAt == self->m_cipherIn.size()) {
		self->m_cipherIn.clear();
		self->m_cipherInAt = 0;
	}
	return static_cast<int>(given);
}

void ibTlsStream::Flush(std::function<void(const std::error_code& error)> done)
{
	// A write on the socket: told with the others waiting for it.
	if (const auto waiting = m_flushed.lock()) {
		waiting->push_back(std::move(done));
		return;
	}
	if (m_cipherOut.empty()) {
		asio::post(m_socket.get_executor(), [done = std::move(done)]() { done(std::error_code()); });
		return;
	}
	const auto waiting = std::make_shared<std::vector<std::function<void(const std::error_code& error)>>>();
	waiting->push_back(std::move(done));
	m_flushed = waiting;
	Pump(waiting);
}

void ibTlsStream::Pump(std::shared_ptr<std::vector<std::function<void(const std::error_code& error)>>> waiting)
{
	// `this` lives while a waiter does: every waiter holds the connection, which holds the stream. The waiters live in
	// the handler, so a context stopped before it runs destroys them with it, and the connection goes.
	m_sending.swap(m_cipherOut);
	m_cipherOut.clear();
	asio::async_write(m_socket, asio::buffer(m_sending),
		[this, waiting = std::move(waiting)](const std::error_code& error, std::size_t) {
			m_sending.clear();
			if (!error && !m_cipherOut.empty()) {
				Pump(waiting);
				return;
			}
			m_flushed.reset();
			for (const auto& done : *waiting)
				done(error);
		});
}

void ibTlsStream::Fill(std::function<void(const std::error_code& error)> done)
{
	m_socket.async_read_some(asio::buffer(m_raw, sizeof(m_raw)),
		[this, done = std::move(done)](const std::error_code& error, std::size_t bytes) {
			if (!error)
				m_cipherIn.append(m_raw, bytes);
			done(error);
		});
}

void ibTlsStream::Handshake(std::function<void(const std::error_code& error)> done)
{
	if (m_setup != 0) {
		asio::post(m_socket.get_executor(), [done = std::move(done)]() { done(TlsFailure()); });
		return;
	}
	Handshaking(std::move(done));
}

void ibTlsStream::Handshaking(std::function<void(const std::error_code& error)> done)
{
	const int code = mbedtls_ssl_handshake(&m_ssl);
	// What the step wrote goes first — the peer answers nothing it has not been sent.
	Flush([this, code, done = std::move(done)](const std::error_code& error) {
		if (error) {
			done(error);
			return;
		}
		if (code == 0) {
			done(std::error_code());
			return;
		}
		if (code == MBEDTLS_ERR_SSL_WANT_READ) {
			Fill([this, done](const std::error_code& filled) {
				if (filled)
					done(filled);
				else
					Handshaking(done);
			});
			return;
		}
		if (code == MBEDTLS_ERR_SSL_WANT_WRITE) {
			Handshaking(done);
			return;
		}
		done(TlsFailure());   // a scanner, a client of another age — routine, and not the journal's
	});
}

void ibTlsStream::ReadSome(char* buffer, std::size_t size, ibDone done)
{
	const int code = mbedtls_ssl_read(&m_ssl, reinterpret_cast<unsigned char*>(buffer), size);
	if (code > 0) {
		// Whatever reading made the library say (an alert, a key update's answer) goes out before the bytes are given.
		Flush([code, done = std::move(done)](const std::error_code& error) {
			done(error, error ? 0 : static_cast<std::size_t>(code));
		});
		return;
	}
	if (code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE) {
		Flush([this, buffer, size, code, done = std::move(done)](const std::error_code& error) {
			if (error) {
				done(error, 0);
				return;
			}
			if (code == MBEDTLS_ERR_SSL_WANT_WRITE) {
				ReadSome(buffer, size, done);
				return;
			}
			Fill([this, buffer, size, done](const std::error_code& filled) {
				if (filled)
					done(filled, 0);
				else
					ReadSome(buffer, size, done);
			});
		});
		return;
	}
	const std::error_code ended = code == 0 || code == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY
		? std::error_code(asio::error::eof) : TlsFailure();
	asio::post(m_socket.get_executor(), [ended, done = std::move(done)]() { done(ended, 0); });
}

void ibTlsStream::Write(const char* data, std::size_t size, ibDone done)
{
	// The library takes what the socket will be given — never "want write": what it sends is only collected.
	std::size_t written = 0;
	while (written < size) {
		const int code = mbedtls_ssl_write(&m_ssl, reinterpret_cast<const unsigned char*>(data) + written,
			size - written);
		if (code <= 0) {
			asio::post(m_socket.get_executor(), [done = std::move(done)]() { done(TlsFailure(), 0); });
			return;
		}
		written += static_cast<std::size_t>(code);
	}
	Flush([size, done = std::move(done)](const std::error_code& error) { done(error, error ? 0 : size); });
}

void ibTlsStream::Close()
{
	std::error_code ignored;
	m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
	m_socket.close(ignored);
}
