#ifndef __CLIENT_FRAME_DESIGNER_H__
#define __CLIENT_FRAME_DESIGNER_H__

#include "clientFrame.h"

// THE DESIGNER'S MAIN WINDOW — the configuration, as designer.exe edited it (ibFrontendMainFrameDesigner): its doc
// manager is the designer's (ibDocManagerDesigner), and it starts no runtime.
class FRMSERVER_API ibClientFrameDesigner : public ibClientFrame {
public:

	ibClientFrameDesigner(ibSessionHolder&& holder, ibClientInstance* clientInstance);

	// The right to edit the configuration — its administration right, asked of the person who logged in.
	virtual bool AllowRun() override;

private:

	// The menu, as the desktop's designer built its menu bar — what the server does of it so far.
	void InitializeDefaultMenu();
};

#endif
