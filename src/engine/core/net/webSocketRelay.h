#ifndef __CORE_NET_WEBSOCKET_RELAY_H__
#define __CORE_NET_WEBSOCKET_RELAY_H__

// A SOCKET CARRIED ON TO A SERVER — a socket of the carrier's own to the server for each one that came in, the messages
// carried both ways as they are: a deflated answer stays deflated, if the peer asked for it as it connected (the target
// says so). The web server's: a browser's to an application server's base, over TLS when its address says oess://; the
// cluster's (appcluster): a client's to the server of its base, on this machine, in the clear.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/net/httpClient.h"
#include "core/net/httpServer.h"
#include "core/net/webSocketFrame.h"

class ibWebSocketRelay : public ibWebSocketHandler {
public:

	// The server not reached, or its socket dropped — `reason` for the carrier's journal; told once, before the peer's
	// socket is closed. A server that closed the socket itself is no loss and is not told.
	using ibLost = std::function<void(const ibString& reason)>;

	// `tls` — the carrier's TLS to the server (ibTlsClientContext), made once for all its sockets; null — in the clear.
	ibWebSocketRelay(asio::io_context& io, const std::string& host, std::uint16_t port,
		std::shared_ptr<const ibTlsContext> tls, const std::string& target, std::shared_ptr<ibWebSocket> socket, ibLost lost) :
		m_socket(std::move(socket))
	{
		const std::weak_ptr<ibWebSocket> told = m_socket;
		m_upstream = std::make_shared<ibWebSocketClient>(io, nullptr,
			[told](const ibString& text) {
				if (const std::shared_ptr<ibWebSocket> socket = told.lock())
					socket->Send(text);
			},
			[told](std::string packed) {
				if (const std::shared_ptr<ibWebSocket> socket = told.lock())
					socket->SendBinary(std::move(packed));
			},
			[told, lost](const std::string& reason) {
				if (!reason.empty() && lost)
					lost(ibString::FromUTF8(reason.data(), reason.size()));
				if (const std::shared_ptr<ibWebSocket> socket = told.lock())
					socket->Close(reason.empty() ? ibWebSocketClose::GoingAway : ibWebSocketClose::BadGateway,
						reason.empty() ? "the server closed the connection" : "the server is not reached");
			});
		if (tls)
			m_upstream->Secure(std::move(tls));
		m_upstream->Open(host, port, target);
	}

	virtual void OnMessage(const ibString& text) override
	{
		m_upstream->Send(text);
	}

	virtual void OnBinary(std::string) override
	{
		m_socket->Close(ibWebSocketClose::Unsupported, "the protocol speaks text");
	}

	virtual void OnClose() override
	{
		m_upstream->Close();
	}

private:
	std::shared_ptr<ibWebSocket>       m_socket;
	std::shared_ptr<ibWebSocketClient> m_upstream;
};

#endif
