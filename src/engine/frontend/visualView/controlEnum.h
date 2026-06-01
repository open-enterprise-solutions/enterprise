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

#include "frontend/frontend.h"

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class FRONTEND_API ibValueEnumOrient :
	public ibValueEnumeration<wxOrientation> {
	public:
	ibValueEnumOrient() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxHORIZONTAL, wxT("Horizontal"), _("Horizontal"));
		AddEnumeration(wxVERTICAL, wxT("Vertical"), _("Vertical"));
	}
private:
};

class FRONTEND_API ibValueEnumStretch :
	public ibValueEnumeration<wxStretch> {
	public:
	ibValueEnumStretch() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxStretch::wxSHRINK, wxT("Shrink"), _("Shrink"));
		AddEnumeration(wxStretch::wxEXPAND, wxT("Expand"), _("Expand"));
	}
private:
};

#include <wx/aui/auibook.h>

class FRONTEND_API ibValueEnumOrientNotebookPage :
	public ibValueEnumeration<wxAuiNotebookOption> {
	public:
	ibValueEnumOrientNotebookPage() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxAuiNotebookOption::wxAUI_NB_TOP, wxT("Top"), _("Top"));
		AddEnumeration(wxAuiNotebookOption::wxAUI_NB_BOTTOM, wxT("Bottom"), _("Bottom"));
	}
private:
};

class FRONTEND_API ibValueEnumHorizontalAlignment :
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

class FRONTEND_API ibValueEnumVerticalAlignment :
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

class FRONTEND_API ibValueEnumTitleLocation :
	public ibValueEnumeration<ibTitleLocation> {
	public:
	ibValueEnumTitleLocation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibTitleLocation::eLeft, wxT("Left"), _("Left"));
		AddEnumeration(ibTitleLocation::eRight, wxT("Right"), _("Right"));
	}
private:
};

class FRONTEND_API ibValueEnumRepresentation :
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
