#ifndef __DOC_MANAGER_RUNTIME_H__
#define __DOC_MANAGER_RUNTIME_H__

#include "sfrontend/docView/docView.h"

// THE RUNTIME'S DOC MANAGER — the application, as enterprise.exe ran it (ibDocManagerEnterprise on the desktop). The
// base set is the base's (ibDocManager's ctor); this ctor adds what only the runtime has. The runtime client's frame
// makes it (ibClientFrameRuntime).
class SFRONTEND_API ibDocManagerRuntime : public ibDocManager {
public:
	ibDocManagerRuntime();

protected:
	wxDECLARE_DYNAMIC_CLASS(ibDocManagerRuntime);
	wxDECLARE_NO_COPY_CLASS(ibDocManagerRuntime);
};

#endif
