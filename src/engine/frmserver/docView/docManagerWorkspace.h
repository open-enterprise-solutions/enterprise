#ifndef __DOC_MANAGER_WORKSPACE_H__
#define __DOC_MANAGER_WORKSPACE_H__

#include "frmserver/docView/docView.h"

// THE RUNTIME'S DOC MANAGER — the application, as the runtime runs it (ibDocManagerEnterprise on the old desktop). The
// base set is the base's (ibDocManager's ctor); this ctor adds what only the runtime has. The runtime client's frame
// makes it (ibClientFrameWorkspace).
class FRMSERVER_API ibDocManagerWorkspace : public ibDocManager {
public:
	ibDocManagerWorkspace();

protected:
	ibDocManagerWorkspace(const ibDocManagerWorkspace&) = delete;
	ibDocManagerWorkspace& operator=(const ibDocManagerWorkspace&) = delete;
};

#endif
