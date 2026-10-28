#ifndef __CLIENT_DEBUGGER_H__
#define __CLIENT_DEBUGGER_H__

// THE DESIGNER'S DEBUGGER, ON THE SERVER (protocol 6) — a listener of the session's engine client (backend/debugger), as
// the assistant's is (ibMcpDebugBridge): the engine is the one connected to the runtimes, and a client's windows hear it
// over the wire. What the engine says is sent as it is, `debug {Event, …}`, to the connection the client's debugger
// listens on (ibClientHost::NotifyDebug); what the client asks — a step, a value, the runtimes — is done at once, on the
// caller's thread, as the assistant's tools do it (Command). Made with the designer's frame and gone with it
// (ibClientFrameDesigner) — the engine's adapter owns it meanwhile.

#include "backend/debugger/debugClientBridge.h"

#include "frmserver/frmserver.h"

#include "protocol/protocol.h"   // ibProtocol::ibDebug — the commands and the news

class ibApplicationInstance;
class ibClientInstance;
class ibDataNode;

class FRMSERVER_API ibClientDebugger : public ibDebuggerClientBridge {
public:

	// The client it tells, and the base whose clients' host tells it.
	ibClientDebugger(const ibApplicationInstance* applicationInstance, const ibClientInstance* clientInstance);

	// THE CLIENT'S COMMAND (ibProtocol::ibDebug::ibCommand; Channel and Listen are the host's) — `params` its fields, done
	// and answered into `result` at once; false with why. What the runtime answers comes later, as news.
	bool Command(ibProtocol::ibDebug::ibCommand command, const ibDataNode& params, ibDataNode& result,
		ibProtocol::ibRefusal& refusal, ibString& error) const;

	// The engine's news, each an event of its own (ibProtocol::ibDebug::ibEvent). SessionStart and SessionEnd say whether
	// a runtime is still attached (Connected) — the engine's HasConnections, asked once it holds.
	virtual void OnSessionStart() override;
	virtual void OnSessionEnd() override;

	virtual void OnEnterLoop(const ibDebugLineData& data) override;
	virtual void OnLeaveLoop(const ibDebugLineData& data) override;

	// ⚠ Not sent: the thin designer's editor completes from the server's module (ibModuleTextView::Fetch), and has no door
	// for the runtime's answer.
	virtual void OnAutoComplete(const ibDebugAutoCompleteData& data) override;
	virtual void OnMessageFromServer(const ibDebugLineData& data, const ibString& message) override;
	virtual void OnSetToolTip(const ibDebugExpressionData& data, const ibString& resultStr) override;

	virtual void OnSetStack(const ibStackData& stackData) override;

	virtual void OnSetLocalVariable(const ibLocalWindowData& watchData) override;

	virtual void OnSetVariable(const ibWatchWindowData& watchData) override;
	virtual void OnSetExpanded(const ibWatchWindowData& watchData) override;

private:

	// `event`, its fields written, sent as the news of that kind.
	void Notify(ibProtocol::ibDebug::ibEvent kind, ibDataNode& event) const;

	const ibApplicationInstance* const m_applicationInstance;
	const ibClientInstance* const      m_clientInstance;
};

#endif
