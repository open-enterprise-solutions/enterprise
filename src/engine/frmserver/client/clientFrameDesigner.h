#ifndef __CLIENT_FRAME_DESIGNER_H__
#define __CLIENT_FRAME_DESIGNER_H__

#include "clientFrame.h"
#include "propertyInspector.h"

#include "frmserver/metaTree/treeConfiguration.h"

// THE DESIGNER'S MAIN WINDOW — the configuration, as designer.exe edited it (ibFrontendMainFrameDesigner): its doc
// manager is the designer's (ibDocManagerDesigner), and it starts no runtime. Its configuration tree is the server's
// half of the desktop's navigator (ibConfigurationTree): the rows, drawn into the frame.
class FRMSERVER_API ibClientFrameDesigner : public ibClientFrame {
public:

	ibClientFrameDesigner(ibSessionHolder&& holder, ibClientInstance* clientInstance);

	// The right to edit the configuration — its administration right, asked of the person who logged in; let in, the
	// tree is loaded with it, as the desktop's AllowRun loaded its navigator.
	virtual bool AllowRun() override;

	// The configuration tree, as the frame's MetaData; the inspector's object, as its Properties.
	virtual void OnDraw(ibDataNode& state) override;

	// A script's failure — a module checked, compiled: the desktop designer's (ibFrontendMainFrameDesigner::BackendError),
	// a line in the messages; not the runtime's window that asks what becomes of it.
	virtual void BackendError(const wxString& strFileName,
		const wxString& strDocPath, const long line,
		const wxString& strErrorMessage) override;

	// The inspector's half the frame holds — what it shows (schema MetaData's Select).
	ibPropertyInspector& GetPropertyInspector() { return m_propertyInspector; }

private:

	// The menu, as the desktop's designer built its menu bar — what the server does of it: its Configuration and Debug
	// are the client's own, beside File and Edit.
	void InitializeDefaultMenu();

	ibConfigurationTree m_metaTree;
	ibPropertyInspector m_propertyInspector;
};

#endif
