#include "httpStream.h"

#include <asio/post.hpp>
#include <asio/write.hpp>

#include <wx/log.h>   // wxSysErrorMsgStr — the system's text of an error, in Unicode

std::string ibErrorText(const std::error_code& code)
{
#ifdef __WINDOWS__
	// The standard library asks FormatMessageA: the ANSI code page, Cyrillic on a Russian Windows, which read as UTF-8
	// is nothing at all — "Assistant access did not start: ." (2026-10-23). wx asks in Unicode. So does asio, under a
	// category of its own ("asio.system") for the same codes — every socket's error is of it: "the server's socket
	// dropped: " said nothing more (2026-10-24, a cluster's server killed).
	if (code.category() == std::system_category() || code.category() == asio::system_category())
		return std::string(wxSysErrorMsgStr(static_cast<unsigned long>(code.value())).utf8_str());
#endif
	return code.message();
}

ibPlainStream::ibPlainStream(asio::ip::tcp::socket socket) :
	m_socket(std::move(socket))
{
}

void ibPlainStream::Handshake(std::function<void(const std::error_code& error)> done)
{
	// Posted, not called: the caller is not entered again from inside its own call.
	asio::post(m_socket.get_executor(), [done = std::move(done)]() { done(std::error_code()); });
}

void ibPlainStream::ReadSome(char* buffer, std::size_t size, ibDone done)
{
	m_socket.async_read_some(asio::buffer(buffer, size),
		[done = std::move(done)](const std::error_code& error, std::size_t bytes) { done(error, bytes); });
}

void ibPlainStream::Write(const char* data, std::size_t size, ibDone done)
{
	asio::async_write(m_socket, asio::buffer(data, size),
		[done = std::move(done)](const std::error_code& error, std::size_t bytes) { done(error, bytes); });
}

void ibPlainStream::Close()
{
	std::error_code ignored;
	m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
	m_socket.close(ignored);
}
