#ifndef _DESIGNER_MANAGER_H__
#define _DESIGNER_MANAGER_H__

#include "mainFrame/mainFrameDesigner.h"
#include "frontend/docView/docManager.h"

class CDesignerDocManager : public CMetaDocManager {
public:
	CDesignerDocManager();
protected:

	void OnUpdateSaveMetadata(wxUpdateUIEvent& event);

	wxDECLARE_DYNAMIC_CLASS(CDesignerDocManager);
	wxDECLARE_NO_COPY_CLASS(CDesignerDocManager);

	wxDECLARE_EVENT_TABLE();
};

#endif
