#ifndef __CLUSTER_TRAY_H__
#define __CLUSTER_TRAY_H__

// THE CLUSTER IN THE TRAY — what it holds: its port, and the servers of each base with their state, process, falls and
// why one fell; a server started, stopped or started again from its own menu, the server folder opened, the
// cluster stopped — or, not running, started. The icon says at a glance how the servers are: all serving, some on the
// way, some fallen or stopped — or no cluster reached.

#include <vector>

#include <wx/taskbar.h>

#include "core/server/clusterControl.h"   // ibClusterServer

class ibAppClusterTray;

class ibClusterTray : public wxTaskBarIcon {
public:

	explicit ibClusterTray(ibAppClusterTray& app);

	// The icon and its tip, as the servers are now.
	void Draw();

protected:

	virtual wxMenu* CreatePopupMenu() override;

private:

	void OnMenu(wxCommandEvent& event);

	ibAppClusterTray&            m_app;
	std::vector<ibClusterServer> m_shown;   // the servers of the menu last shown, in its order: what its verbs are for
};

#endif
