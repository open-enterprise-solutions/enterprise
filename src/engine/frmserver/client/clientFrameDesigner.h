#ifndef __CLIENT_FRAME_DESIGNER_H__
#define __CLIENT_FRAME_DESIGNER_H__

#include "clientFrame.h"

#include "frmserver/metaTree/treeConfiguration.h"

// THE DESIGNER'S MAIN WINDOW — the configuration, as the old desktop designer edited it (ibFrontendMainFrameDesigner): its doc
// manager is the designer's (ibDocManagerDesigner), and it starts no runtime. Its configuration tree is the server's
// half of the desktop's navigator (ibConfigurationTree): the rows, drawn into the frame.
class FRMSERVER_API ibClientFrameDesigner : public ibClientFrame {
public:

	ibClientFrameDesigner(ibSessionHolder&& holder, ibClientInstance* clientInstance);
	// Its debugger taken off the engine's listeners — it goes with them.
	virtual ~ibClientFrameDesigner() override;

	// The right to edit the configuration — its administration right, asked of the person who logged in; let in, the
	// tree is loaded with it, as the desktop's AllowRun loaded its navigator.
	virtual bool AllowRun() override;
	// ⭐ THE ASSISTANT COMES UP WITH THE DESIGNER, when its settings switch it on — the desktop's main window started it
	// as it showed (ibFrontendMainFrameDesigner::Show); here, as the designer's session starts, so the client asks
	// nothing for it (five exchanges at every start, 2026-10-15). A refusal is not fatal and not silent — the port may
	// be another designer's: said in the messages, and the designer carries on.
	virtual void CreateStartupPage() override;

	// The configuration tree, as the frame's MetaData.
	virtual void OnDraw(ibDataNode& state) override;

	// The engine's listener this frame put in — null where the configuration has no debugger.
	virtual ibClientDebugger* GetDebugger() const override { return m_debugger; }

	// A script's failure — a module checked, compiled: the desktop designer's (ibFrontendMainFrameDesigner::BackendError),
	// a line in the messages; not the runtime's window that asks what becomes of it.
	virtual void BackendError(const ibString& strFileName,
		const ibString& strDocPath, const long line,
		const ibString& strErrorMessage) override;

private:

	// The menu, as the desktop's designer built its menu bar — what the server does of it: its Configuration and Debug
	// are the client's own, beside File and Edit.
	void InitializeDefaultMenu();

	ibConfigurationTree m_metaTree;

	// ⭐ THE DESIGNER LISTENS AS THE ASSISTANT DOES — one more listener of the session's engine client (debugClient), put
	// in as the frame is made and taken out as it goes. Owned by the engine's adapter meanwhile: held, not owned.
	ibClientDebugger* m_debugger = nullptr;
};

#endif
