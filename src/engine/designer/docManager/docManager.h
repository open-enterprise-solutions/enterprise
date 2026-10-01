#ifndef _DESIGNER_MANAGER_H__
#define _DESIGNER_MANAGER_H__

#include "mainFrame/mainFrameDesigner.h"
#include "frontend/docView/docView.h"

// Designer-side doc manager. Inherits from the collapsed ibDocManager (which
// holds both the wx file-template path and the OES meta-template path) and
// adds Designer-specific template registrations + OnUpdateSaveMetadata
// (enables Save when the active configuration has unsaved metadata changes).
// The handler is bound in the event table against wxID_DESIGNER_CONFIGURATION_*
// menu IDs that exist only in the Designer binary, so the method belongs on
// this subclass rather than the cross-build base.
class ibDocManagerDesigner : public ibDocManager {
public:
	ibDocManagerDesigner();

protected:
	void OnUpdateSaveMetadata(wxUpdateUIEvent& event);

	wxDECLARE_DYNAMIC_CLASS(ibDocManagerDesigner);
	wxDECLARE_NO_COPY_CLASS(ibDocManagerDesigner);

	wxDECLARE_EVENT_TABLE();
};

#endif
