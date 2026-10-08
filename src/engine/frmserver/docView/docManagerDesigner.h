#ifndef __DOC_MANAGER_DESIGNER_H__
#define __DOC_MANAGER_DESIGNER_H__

#include "frmserver/docView/docView.h"

// THE DESIGNER'S DOC MANAGER — the configuration, as designer.exe edited it (ibDocManagerDesigner on the desktop).
// The base set is the base's (ibDocManager's ctor); this ctor adds what only the designer has. It loads no runtime,
// so nothing that opens what the runtime runs is here. The designer client's frame makes it (ibClientFrameDesigner).
class FRMSERVER_API ibDocManagerDesigner : public ibDocManager {
public:
	ibDocManagerDesigner();

protected:
	wxDECLARE_DYNAMIC_CLASS(ibDocManagerDesigner);
	wxDECLARE_NO_COPY_CLASS(ibDocManagerDesigner);
};

#endif
