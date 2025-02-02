#ifndef _DESIGNER_MANAGER_H__
#define _DESIGNER_MANAGER_H__

#include "frontend/docView/docManager.h"

class CEnterpriseDocManager : public CMetaDocManager {
public:
	CEnterpriseDocManager();

protected:
	wxDECLARE_DYNAMIC_CLASS(CEnterpriseDocManager);
	wxDECLARE_NO_COPY_CLASS(CEnterpriseDocManager);
};

#endif
