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

// How a form opens beside the others: on its own, or holding its owner's window - or every window - until it closes.
// The numbers are the ones a saved form holds.
enum ibWindowOpeningMode {
	ibWindowOpeningMode_Independent = 0,
	ibWindowOpeningMode_LockOwnerWindow = 1,
	ibWindowOpeningMode_LockWholeInterface = 2
};

// Whether a form's controls scroll up and down when they are taller than its window. The numbers are the ones a saved
// form holds and the ones the client reads (ibProtocol::ibForm::ibProperty::ibVerticalScroll).
enum ibVerticalScroll {
	ibVerticalScroll_UseIfNecessary = 0,
	ibVerticalScroll_Use = 1,
	ibVerticalScroll_DontUse = 2
};

// Where a notebook's tabs are (its OrientPage) - none: the notebook is a panel, the active page alone. The numbers are
// the ones a saved form holds (Top and Bottom were wx's flags) and the ones the client reads
// (ibProtocol::ibForm::ibNotebook::ibTabLocation).
enum ibTabLocation {
	ibTabLocation_None = 0,
	ibTabLocation_Top = 1,
	ibTabLocation_Bottom = 8
};

#include "frmserver/frmserver.h"

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class FRMSERVER_API ibValueEnumOrient :
	public ibValueEnumeration<wxOrientation> {
	public:
	ibValueEnumOrient() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxHORIZONTAL, ibT("Horizontal"), _("Horizontal"));
		AddEnumeration(wxVERTICAL, ibT("Vertical"), _("Vertical"));
	}
private:
};

class FRMSERVER_API ibValueEnumWindowOpeningMode :
	public ibValueEnumeration<ibWindowOpeningMode> {
	public:
	ibValueEnumWindowOpeningMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibWindowOpeningMode::ibWindowOpeningMode_Independent, ibT("Independent"), _("Independent"));
		AddEnumeration(ibWindowOpeningMode::ibWindowOpeningMode_LockOwnerWindow, ibT("LockOwnerWindow"), _("Lock owner window"));
		AddEnumeration(ibWindowOpeningMode::ibWindowOpeningMode_LockWholeInterface, ibT("LockWholeInterface"), _("Lock whole interface"));
	}
private:
};

class FRMSERVER_API ibValueEnumVerticalScroll :
	public ibValueEnumeration<ibVerticalScroll> {
	public:
	ibValueEnumVerticalScroll() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibVerticalScroll::ibVerticalScroll_UseIfNecessary, ibT("UseIfNecessary"), _("Use if necessary"));
		AddEnumeration(ibVerticalScroll::ibVerticalScroll_Use, ibT("Use"), _("Use"));
		AddEnumeration(ibVerticalScroll::ibVerticalScroll_DontUse, ibT("DontUse"), _("Don't use"));
	}
private:
};

class FRMSERVER_API ibValueEnumStretch :
	public ibValueEnumeration<wxStretch> {
	public:
	ibValueEnumStretch() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxStretch::wxSHRINK, ibT("Shrink"), ibGETTEXT_IN_CONTEXT("sizer flag", "Shrink"));
		AddEnumeration(wxStretch::wxEXPAND, ibT("Expand"), ibGETTEXT_IN_CONTEXT("sizer flag", "Expand"));
	}
private:
};

class FRMSERVER_API ibValueEnumOrientNotebookPage :
	public ibValueEnumeration<ibTabLocation> {
	public:
	ibValueEnumOrientNotebookPage() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibTabLocation::ibTabLocation_Top, ibT("Top"), _("Top"));
		AddEnumeration(ibTabLocation::ibTabLocation_Bottom, ibT("Bottom"), _("Bottom"));
		AddEnumeration(ibTabLocation::ibTabLocation_None, ibT("None"), _("None"));
	}
private:
};

class FRMSERVER_API ibValueEnumHorizontalAlignment :
	public ibValueEnumeration<wxAlignment> {
	public:
	ibValueEnumHorizontalAlignment() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxAlignment::wxALIGN_LEFT, ibT("Left"), _("Left"));
		AddEnumeration(wxAlignment::wxALIGN_CENTER, ibT("Center"), _("Center"));
		AddEnumeration(wxAlignment::wxALIGN_RIGHT, ibT("Right"), _("Right"));
	}
private:
};

class FRMSERVER_API ibValueEnumVerticalAlignment :
	public ibValueEnumeration<wxAlignment> {
	public:
	ibValueEnumVerticalAlignment() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxAlignment::wxALIGN_TOP, ibT("Top"), _("Top"));
		AddEnumeration(wxAlignment::wxALIGN_CENTER, ibT("Center"), _("Center"));
		AddEnumeration(wxAlignment::wxALIGN_BOTTOM, ibT("Bottom"), _("Bottom"));
	}
private:
};

class FRMSERVER_API ibValueEnumTitleLocation :
	public ibValueEnumeration<ibTitleLocation> {
	public:
	ibValueEnumTitleLocation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibTitleLocation::eLeft, ibT("Left"), _("Left"));
		AddEnumeration(ibTitleLocation::eRight, ibT("Right"), _("Right"));
	}
private:
};

class FRMSERVER_API ibValueEnumRepresentation :
	public ibValueEnumeration<ibRepresentation> {
	public:
	ibValueEnumRepresentation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibRepresentation::ibRepresentation_Auto, ibT("Auto"), _("Auto"));
		AddEnumeration(ibRepresentation::ibRepresentation_Text, ibT("Text"), _("Text"));
		AddEnumeration(ibRepresentation::ibRepresentation_Picture, ibT("Picture"), _("Picture"));
		AddEnumeration(ibRepresentation::ibRepresentation_PictureAndText, ibT("PictureAndText"), _("Picture and text"));
	}
private:
};

#pragma endregion 
#endif // !__CONTROL_ENUM_H__
