#ifndef __CONTROL_ENUM_H__
#define __CONTROL_ENUM_H__

enum ibTitleLocation {
	eLeft = 1,
	eRight
};

enum ibRepresentation {
	ibRepresentation_Auto,
	ibRepresentation_Text,
	ibRepresentation_Picture,
	ibRepresentation_PictureAndText
};

#include "frmclient/frmclient.h"

#pragma region enumeration
#include "frmclient/backend/compiler/enumUnit.h"
class FRMCLIENT_API ibValueEnumOrient :
	public ibValueEnumeration<wxOrientation> {
	public:
	ibValueEnumOrient() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxHORIZONTAL, wxT("Horizontal"), _("Horizontal"));
		AddEnumeration(wxVERTICAL, wxT("Vertical"), _("Vertical"));
	}
private:
};

class FRMCLIENT_API ibValueEnumStretch :
	public ibValueEnumeration<wxStretch> {
	public:
	ibValueEnumStretch() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxStretch::wxSHRINK, wxT("Shrink"), wxGETTEXT_IN_CONTEXT("sizer flag", "Shrink"));
		AddEnumeration(wxStretch::wxEXPAND, wxT("Expand"), wxGETTEXT_IN_CONTEXT("sizer flag", "Expand"));
	}
private:
};

#include <wx/aui/auibook.h>

class FRMCLIENT_API ibValueEnumOrientNotebookPage :
	public ibValueEnumeration<wxAuiNotebookOption> {
	public:
	ibValueEnumOrientNotebookPage() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxAuiNotebookOption::wxAUI_NB_TOP, wxT("Top"), _("Top"));
		AddEnumeration(wxAuiNotebookOption::wxAUI_NB_BOTTOM, wxT("Bottom"), _("Bottom"));
	}
private:
};

class FRMCLIENT_API ibValueEnumHorizontalAlignment :
	public ibValueEnumeration<wxAlignment> {
	public:
	ibValueEnumHorizontalAlignment() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxAlignment::wxALIGN_LEFT, wxT("Left"), _("Left"));
		AddEnumeration(wxAlignment::wxALIGN_CENTER, wxT("Center"), _("Center"));
		AddEnumeration(wxAlignment::wxALIGN_RIGHT, wxT("Right"), _("Right"));
	}
private:
};

class FRMCLIENT_API ibValueEnumVerticalAlignment :
	public ibValueEnumeration<wxAlignment> {
	public:
	ibValueEnumVerticalAlignment() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxAlignment::wxALIGN_TOP, wxT("Top"), _("Top"));
		AddEnumeration(wxAlignment::wxALIGN_CENTER, wxT("Center"), _("Center"));
		AddEnumeration(wxAlignment::wxALIGN_BOTTOM, wxT("Bottom"), _("Bottom"));
	}
private:
};

class FRMCLIENT_API ibValueEnumTitleLocation :
	public ibValueEnumeration<ibTitleLocation> {
	public:
	ibValueEnumTitleLocation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibTitleLocation::eLeft, wxT("Left"), _("Left"));
		AddEnumeration(ibTitleLocation::eRight, wxT("Right"), _("Right"));
	}
private:
};

enum ibWindowOpeningMode {
	Independent = 0,
	LockOwnerWindow,
	LockWholeInterface
};

class FRMCLIENT_API ibValueEnumWindowOpeningMode :
	public ibValueEnumeration<ibWindowOpeningMode> {
	public:
	ibValueEnumWindowOpeningMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibWindowOpeningMode::Independent, wxT("Independent"), _("Independent"));
		AddEnumeration(ibWindowOpeningMode::LockOwnerWindow, wxT("LockOwnerWindow"), _("Lock owner window"));
		AddEnumeration(ibWindowOpeningMode::LockWholeInterface, wxT("LockWholeInterface"), _("Lock whole interface"));
	}
private:
};

enum ibCellAlign {
	Auto = 0,
	Left,
	Center,
	Right
};

class FRMCLIENT_API ibValueEnumCellAlign :
	public ibValueEnumeration<ibCellAlign> {
	public:
	ibValueEnumCellAlign() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibCellAlign::Auto, wxT("Auto"), _("Auto"));
		AddEnumeration(ibCellAlign::Left, wxT("Left"), _("Left"));
		AddEnumeration(ibCellAlign::Center, wxT("Center"), _("Center"));
		AddEnumeration(ibCellAlign::Right, wxT("Right"), _("Right"));
	}
private:
};

enum ibQuickChoice {
	ibQuickChoice_Auto = 0,
	ibQuickChoice_Use,
	ibQuickChoice_DontUse
};

class FRMCLIENT_API ibValueEnumQuickChoice :
	public ibValueEnumeration<ibQuickChoice> {
	public:
	ibValueEnumQuickChoice() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibQuickChoice_Auto, wxT("Auto"), _("Auto"));
		AddEnumeration(ibQuickChoice_Use, wxT("Use"), _("Use"));
		AddEnumeration(ibQuickChoice_DontUse, wxT("DontUse"), _("Don't use"));
	}
private:
};

class FRMCLIENT_API ibValueEnumRepresentation :
	public ibValueEnumeration<ibRepresentation> {
	public:
	ibValueEnumRepresentation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibRepresentation::ibRepresentation_Auto, wxT("Auto"), _("Auto"));
		AddEnumeration(ibRepresentation::ibRepresentation_Text, wxT("Text"), _("Text"));
		AddEnumeration(ibRepresentation::ibRepresentation_Picture, wxT("Picture"), _("Picture"));
		AddEnumeration(ibRepresentation::ibRepresentation_PictureAndText, wxT("PictureAndText"), _("Picture and text"));
	}
private:
};

#pragma endregion 
#endif // !__CONTROL_ENUM_H__
