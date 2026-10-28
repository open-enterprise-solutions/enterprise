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
