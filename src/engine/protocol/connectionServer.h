#ifndef __PROTOCOL_CONNECTION_SERVER_H__
#define __PROTOCOL_CONNECTION_SERVER_H__

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

#include "connection.h"

class ibIoThreads;
class ibTlsContext;
class ibWebSocketClient;

// AN APPLICATION SERVER — over its port: one WebSocket to `oes://server[:port]/<base>`, one packet per request (a
// binary message), its answer the next answer on it; `oess://` — the same over TLS, the server's certificate checked
// against the name in the address. Asked for compressed, as the protocol offers it: an answer of some size comes raw
// deflate, told from a packet by its first bytes. Between the answers the server may say something unasked — a
// notification, a packet with a method and no id — which goes to whoever listens. A dropped socket is not itself the end of a thin client's session: the
// communicator opens this socket again and logs in by token (docs/public/client-protocol.md).
class PROTOCOL_API ibProtocolConnectionServer : public ibProtocolConnection {
public:

	// `oes://server[:port]/<base>` or `oess://…` — the address the cluster prints for every base it serves — read;
	// false when it is not one. The port left out is the cluster's own (ibPort::Clients); `secure` — oess://, TLS.
	static bool ParseAddress(const ibString& address, ibString& host, int& port, ibString& base, bool& secure);
	// Where a base's clients come in on its server — /<base>/client, the name escaped.
	static std::string ClientPath(const ibString& base);
	// …and whom they trust there, over oess:// — `trusted`, a PEM file of the authorities, else the platform's store;
	// the server's chain checked, and its name against the address's host. Made once for every socket opened with it
	// (the web server's: one for all its browsers'). False, with why in `error`.
	static bool ClientTls(const ibString& trusted, std::shared_ptr<const ibTlsContext>& tls, ibString& error);

	// Made where the socket's type is whole (the .cpp) — so is its destruction.
	ibProtocolConnectionServer();

	// `address` — `oes://server[:port]/<base>` or `oess://…`; `trusted` — for oess://, a PEM file of the authorities
	// the server's certificate must end in, empty — the platform's store. False with why it was not reached in `error`.
	bool Open(const ibString& address, const ibString& trusted, ibString& error);
	void Close();

	virtual ~ibProtocolConnectionServer();

	virtual bool Exchange(const ibMemoryBuffer& request, ibProtocolNode& answer,
		ibProtocol::ibRefusal& refusal, ibString& error) override;
	// The same address, opened again. False when Open has never succeeded.
	virtual bool Reconnect(ibString& error) override;
	// …and a second socket to it, the debugger's (protocol 6).
	virtual std::unique_ptr<ibProtocolConnection> OpenChannel(ibString& error) override;
	// What the server says unasked — on the thread that reads the socket.
	virtual void Listen(std::function<void(const ibProtocolNode& said)> notified) override;

private:

	// A packet the socket hands over — an answer, or a notification.
	void Receive(const ibMemoryBuffer& packet);
	// …and the socket gone, or no longer understood — why.
	void Closed(const std::string& reason);

	// THE SOCKET AND THE THREAD IT GOES BY — made by Open, gone in Close: once the thread has stopped, nothing of the
	// socket is called. Its messages come as they arrive, for as long as it is open — an answer handed to the request
	// waiting for it (Exchange), a notification to whoever listens: the server says one between the answers, unasked.
	std::unique_ptr<ibIoThreads>       m_ioThreads;
	std::shared_ptr<ibWebSocketClient> m_socketClient;

	// One request on the socket at a time.
	std::mutex m_exchanging;

	// What the socket hands over, under m_mutex: its opening, the answer to the request waiting, its going and why,
	// whoever listens.
	std::mutex                                 m_mutex;
	std::condition_variable                    m_answered;
	std::optional<ibProtocolNode>              m_answer;
	bool                                       m_opened = false;
	bool                                       m_closed = true;
	std::string                                m_reason;
	std::function<void(const ibProtocolNode&)> m_notified;

	// The address Open succeeded with, and whom it trusted — what Reconnect opens again.
	ibString                                   m_address;
	ibString                                   m_trusted;
};

#endif
