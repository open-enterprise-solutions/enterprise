#ifndef __DOC_MANAGER_STUDIO_H__
#define __DOC_MANAGER_STUDIO_H__

#include "frmserver/docView/docView.h"

// THE STUDIO'S DOC MANAGER — the application, as the studio edits it (ibDocManagerStudio on the old desktop).
// The base set is the base's (ibDocManager's ctor); this ctor adds what only the studio has. It loads no runtime,
// so nothing that opens what the runtime runs is here. The studio client's frame makes it (ibClientFrameStudio).
class FRMSERVER_API ibDocManagerStudio : public ibDocManager {
public:
	ibDocManagerStudio();

protected:
	ibDocManagerStudio(const ibDocManagerStudio&) = delete;
	ibDocManagerStudio& operator=(const ibDocManagerStudio&) = delete;
};

#endif
