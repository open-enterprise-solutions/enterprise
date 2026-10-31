#include "httpStream.h"

#include <asio/post.hpp>
#include <asio/write.hpp>


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
