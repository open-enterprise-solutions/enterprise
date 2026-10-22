#ifndef __CORE_NET_HTTP_STREAM_H__
#define __CORE_NET_HTTP_STREAM_H__

// THE BYTES OF ONE CONNECTION — plain TCP, or TLS over it (tlsStream.h): what the connection reads and writes, the
// same either way. Every call is made on the connection's strand, the socket's executor, and every `done` is called
// there; a read and a write may be outstanding together, never two of one kind.

#include <cstddef>
#include <functional>
#include <string>
#include <system_error>

#include <asio/ip/tcp.hpp>

// WHY A SOCKET FAILED, as a refusal here says it — UTF-8, as every one of them is read (wxString::FromUTF8). Not
// error_code::message(): on Windows that is the system's text in the ANSI code page.
std::string ibErrorText(const std::error_code& code);

class ibHttpStream {
public:

	using ibDone = std::function<void(const std::error_code& error, std::size_t bytes)>;

	virtual ~ibHttpStream() = default;

	// TLS — the handshake; plain — done at once.
	virtual void Handshake(std::function<void(const std::error_code& error)> done) = 0;
	// Some bytes, at most `size`, into `buffer` — which stays alive until `done`.
	virtual void ReadSome(char* buffer, std::size_t size, ibDone done) = 0;
	// All of `data`, which stays alive until `done`.
	virtual void Write(const char* data, std::size_t size, ibDone done) = 0;
	// The socket closed: what is outstanding completes with an error.
	virtual void Close() = 0;

	virtual asio::ip::tcp::socket& GetSocket() = 0;
};

class ibPlainStream : public ibHttpStream {
public:

	explicit ibPlainStream(asio::ip::tcp::socket socket);

	virtual void Handshake(std::function<void(const std::error_code& error)> done) override;
	virtual void ReadSome(char* buffer, std::size_t size, ibDone done) override;
	virtual void Write(const char* data, std::size_t size, ibDone done) override;
	virtual void Close() override;

	virtual asio::ip::tcp::socket& GetSocket() override { return m_socket; }

private:
	asio::ip::tcp::socket m_socket;
};

#endif
