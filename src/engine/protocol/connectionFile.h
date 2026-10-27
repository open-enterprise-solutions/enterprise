#ifndef __PROTOCOL_CONNECTION_FILE_H__
#define __PROTOCOL_CONNECTION_FILE_H__

#include "connection.h"

#include "fileserver/fileBase.h"   // the file base's five doors — declared there, LOADED here, never linked

// A FILE BASE OPENED IN THIS PROCESS — the library's doors and the base behind them. The program links nothing of the
// server: fileserver (and with it frmserver and the backend) is loaded only now, when a file base is opened, and its
// doors are taken by name. No library beside the program — no file base; a server is reached all the same.
//
// Served as a server serves it: every call names the connection it came by — a thin client is one, the web server
// one per browser — and a connection that goes takes its clients with it (Disconnect). Safe from any thread, as the
// base's clients' host is.
class PROTOCOL_API ibProtocolFileBase {
public:

	// `request` — a JSON object saying where the base lives (ibFileBaseOpen says its fields); false with why it was
	// not opened in `error`.
	bool Open(const ibString& request, ibString& error);
	// Its clients go — each its own session's teardown — and then the base. The library stays loaded.
	void Close();

	ibProtocolFileBase() = default;
	ibProtocolFileBase(const ibProtocolFileBase&) = delete;
	ibProtocolFileBase& operator=(const ibProtocolFileBase&) = delete;
	~ibProtocolFileBase();

	// ibFileBaseCall, ibFileBaseListen and ibFileBaseDisconnect — their words, on the base that is open.
	bool Call(const ibMemoryBuffer& request, const void* connection, const ibString& address,
		ibMemoryBuffer& answer, ibString& error) const;
	void Listen(const void* connection, std::function<void(const ibMemoryBuffer& packet)> notified) const;
	void Disconnect(const void* connection) const;

private:
	ibFileBase*                     m_base = nullptr;
	decltype(&ibFileBaseCall)       m_call = nullptr;
	decltype(&ibFileBaseListen)     m_listen = nullptr;
	decltype(&ibFileBaseDisconnect) m_disconnect = nullptr;
	decltype(&ibFileBaseClose)      m_close = nullptr;
};

// A FILE BASE AS THE THIN CLIENT'S CONNECTION — the client and the server in one process: the base is opened here
// and every request goes to its clients' host through the door the port's does, this connection the one they came by.
class PROTOCOL_API ibProtocolConnectionFile : public ibProtocolConnection {
public:

	// ibProtocolFileBase::Open and Close — the base this connection is the only one to.
	bool Open(const ibString& request, ibString& error);
	void Close();

	virtual ~ibProtocolConnectionFile();

	virtual bool Exchange(const ibMemoryBuffer& request, ibProtocolNode& answer,
		ibProtocol::ibRefusal& refusal, ibString& error) override;
	// What the base says unasked — on the base's thread that says it (a session's).
	virtual void Listen(std::function<void(const ibProtocolNode& said)> notified) override;
	// …and a second connection to the same base, the debugger's (protocol 6): a connection of its own to the base's
	// clients, which goes (Disconnect) when it is dropped. The base stays this one's to close.
	virtual std::unique_ptr<ibProtocolConnection> OpenChannel(ibString& error) override;

private:
	// Held with the second connections it opened: one dropped after the base was closed finds it closed, not freed.
	std::shared_ptr<ibProtocolFileBase> m_base = std::make_shared<ibProtocolFileBase>();
};

#endif
