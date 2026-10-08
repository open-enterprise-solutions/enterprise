#ifndef __PROTOCOL_CONNECTION_SERVER_H__
#define __PROTOCOL_CONNECTION_SERVER_H__

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "connection.h"

namespace httplib { namespace ws { class WebSocketClient; } }

// AN APPLICATION SERVER — over its port: one WebSocket to `oes://server[:port]/<base>`, one text message per request,
// its answer the next answer on it. Asked for compressed, as the protocol offers it: an answer of some size comes as a
// binary message, raw deflate. Between the answers the server may say something unasked — a notification, a message
// with a method and no id — which goes to whoever listens. The clients logged in through the socket go when it
// closes — a dropped socket ends them (no `login {Resume}` yet).
class PROTOCOL_API ibProtocolConnectionServer : public ibProtocolConnection {
public:

	// The port the protocol takes when an address names none — the server's own default (appserver/clientListener).
	static constexpr int kDefaultPort = 7373;

	// Made where the socket's type is whole (the .cpp) — so is its destruction.
	ibProtocolConnectionServer();

	// `address` — `oes://server[:port]/<base>`; false with why it was not reached in `error`.
	bool Open(const wxString& address, wxString& error);
	void Close();

	virtual ~ibProtocolConnectionServer();

	virtual bool Exchange(const std::string& request, std::string& answer,
		ibProtocolRefusal& refusal, wxString& error) override;
	// What the server says unasked — on the thread that reads the socket.
	virtual void Listen(std::function<void(const std::string& text)> notified) override;

private:

	// THE SOCKET'S MESSAGES, read on a thread of their own for as long as it is open — an answer handed to the request
	// waiting for it (Exchange), a notification to whoever listens: the server says one between the answers, unasked,
	// and a socket read only while a request waits would keep it there until the next one.
	void ReadMessages();

	std::unique_ptr<httplib::ws::WebSocketClient> m_socket;
	std::thread                                   m_reader;
	std::atomic<bool>                             m_stopping{ false };

	// One request on the socket at a time.
	std::mutex m_exchanging;

	// What the reader hands over, under m_mutex: the answer to the request waiting, the socket gone, whoever listens.
	std::mutex                                 m_mutex;
	std::condition_variable                    m_answered;
	std::optional<std::string>                 m_answer;
	bool                                       m_closed = true;
	std::function<void(const std::string&)>    m_notified;
};

#endif
