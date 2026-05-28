#ifndef _ENTERPRISE_MANAGER_H__
#define _ENTERPRISE_MANAGER_H__

#include "frontend/docView/docView.h"

// Enterprise-runtime doc manager. Inherits from the collapsed ibDocManager
// and only registers the file-template entries the Enterprise binary needs
// (external data processor / external report); everything else lives in
// the base.
class ibDocManagerEnterprise : public ibDocManager {
public:
	ibDocManagerEnterprise();

protected:
	wxDECLARE_DYNAMIC_CLASS(ibDocManagerEnterprise);
	wxDECLARE_NO_COPY_CLASS(ibDocManagerEnterprise);
};

#endif
