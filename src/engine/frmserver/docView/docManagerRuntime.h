#ifndef __DOC_MANAGER_RUNTIME_H__
#define __DOC_MANAGER_RUNTIME_H__

#include "frmserver/docView/docView.h"

// THE RUNTIME'S DOC MANAGER — the application, as the runtime runs it (ibDocManagerEnterprise on the old desktop). The
// base set is the base's (ibDocManager's ctor); this ctor adds what only the runtime has. The runtime client's frame
// makes it (ibClientFrameRuntime).
class FRMSERVER_API ibDocManagerRuntime : public ibDocManager {
public:
	ibDocManagerRuntime();

protected:
	wxDECLARE_DYNAMIC_CLASS(ibDocManagerRuntime);
	ibDocManagerRuntime(const ibDocManagerRuntime&) = delete;
	ibDocManagerRuntime& operator=(const ibDocManagerRuntime&) = delete;
};

#endif
