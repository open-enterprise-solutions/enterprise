#ifndef __CLIENT_FRAME_STUDIO_H__
#define __CLIENT_FRAME_STUDIO_H__

#include "clientFrame.h"

#include "frmserver/metaTree/treeApplication.h"

// THE STUDIO'S MAIN WINDOW — the application, as the old desktop designer edited it (ibFrontendMainFrameStudio): its doc
// manager is the studio's (ibDocManagerStudio), and it starts no runtime. Its application tree is the server's
// half of the desktop's navigator (ibApplicationTree): the rows, drawn into the frame.
class FRMSERVER_API ibClientFrameStudio : public ibClientFrame {
public:

	ibClientFrameStudio(ibSessionHolder&& holder, ibClientInstance* clientInstance);
	// Its debugger taken off the engine's listeners — it goes with them.
	virtual ~ibClientFrameStudio() override;

	// The menu, and the debugger — the engine's, which the login made with the application, heard over the wire.
	virtual void OnCreate() override;

	// The right to edit the application — its administration right, asked of the person who logged in; let in, the
	// tree is loaded with it, as the desktop's AllowRun loaded its navigator.
	virtual bool AllowRun() override;

	// The application tree, as the frame's MetaData.
	virtual void OnDraw(ibDataNode& state) override;

	// The engine's listener this frame put in — null where the application has no debugger.
	virtual ibClientDebugger* GetDebugger() const override { return m_debugger; }

	// A script's failure — a module checked, compiled: the desktop designer's (ibFrontendMainFrameStudio::BackendError),
	// a line in the messages; not the runtime's window that asks what becomes of it.
	virtual void BackendError(const ibString& strFileName,
		const ibString& strDocPath, const long line,
		const ibString& strErrorMessage) override;

private:

	// The menu, as the desktop's designer built its menu bar — what the server does of it: its Application and Debug
	// are the client's own, beside File and Edit.
	void InitializeDefaultMenu();

	ibApplicationTree m_metaTree;

	// ⭐ THE STUDIO LISTENS AS THE ASSISTANT DOES — one more listener of the session's engine client (debugClient), put
	// in as the frame is made and taken out as it goes. Owned by the engine's adapter meanwhile: held, not owned.
	ibClientDebugger* m_debugger = nullptr;
};

#endif
