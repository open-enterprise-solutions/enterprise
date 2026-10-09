#ifndef __CLIENT_FRAME_RUNTIME_H__
#define __CLIENT_FRAME_RUNTIME_H__

#include "clientFrame.h"

// THE RUNTIME'S MAIN WINDOW — the application, as enterprise.exe ran it (ibFrontendMainFrameEnterprise): its doc
// manager is the runtime's (ibDocManagerRuntime), its start is the configuration's, and its tabs open with
// the start page.
class FRMSERVER_API ibClientFrameRuntime : public ibClientFrame {
public:

	ibClientFrameRuntime(ibSessionHolder&& holder, ibClientInstance* clientInstance);

	// The configuration's start — BeforeStart may refuse the client, OnStart may open forms and ask the person.
	virtual bool AllowRun() override;

	// The start page — after the start-up script, as the desktop window makes it: a script that vetoes the start
	// never gets a home page built for nothing, and being first does not depend on being created first (the
	// page's tab is locked).
	virtual void CreateStartupPage() override;

private:

	// The menu, as the desktop's enterprise window built its menu bar — what the server does of it so far: the
	// application's menus beside File and Edit, which are the client's doc manager's.
	void InitializeDefaultMenu();
};

#endif
