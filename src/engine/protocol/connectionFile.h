#ifndef __PROTOCOL_CONNECTION_FILE_H__
#define __PROTOCOL_CONNECTION_FILE_H__

#include "connection.h"

#include "fileserver/fileBase.h"   // the file base's four doors — declared there, LOADED here, never linked

// A FILE BASE — the client and the server in one process. The thin client links nothing of the server: fileserver
// (and with it frmserver and the backend) is loaded only now, when the base opened is a file base, and its four
// doors are taken by name; the base is opened there, and every request goes to its clients' host through the door
// the port's does. No library beside the program — no file base; a server is reached all the same.
class PROTOCOL_API ibProtocolConnectionFile : public ibProtocolConnection {
public:

	// `request` — a JSON object saying where the base lives (ibFileBaseOpen says its fields); false with why it was
	// not opened in `error`.
	bool Open(const std::string& request, wxString& error);
	// Its clients go — each its own session's teardown — and then the base. The library stays loaded.
	void Close();

	virtual ~ibProtocolConnectionFile();

	virtual bool Exchange(const std::string& request, std::string& answer,
		ibProtocolRefusal& refusal, wxString& error) override;
	// What the base says unasked — on the base's thread that says it (a session's).
	virtual void Listen(std::function<void(const std::string& text)> notified) override;

private:
	ibFileBase*                 m_base = nullptr;
	decltype(&ibFileBaseCall)   m_call = nullptr;
	decltype(&ibFileBaseListen) m_listen = nullptr;
	decltype(&ibFileBaseClose)  m_close = nullptr;
};

#endif
