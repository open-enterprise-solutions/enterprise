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

#include "sfrontend/sfrontend.h"

#pragma region enumeration
#include "backend/compiler/enumUnit.h"
class SFRONTEND_API ibValueEnumOrient :
	public ibValueEnumeration<wxOrientation> {
	public:
	ibValueEnumOrient() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxHORIZONTAL, wxT("Horizontal"), _("Horizontal"));
		AddEnumeration(wxVERTICAL, wxT("Vertical"), _("Vertical"));
	}
private:
};

class SFRONTEND_API ibValueEnumStretch :
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

class SFRONTEND_API ibValueEnumOrientNotebookPage :
	public ibValueEnumeration<wxAuiNotebookOption> {
	public:
	ibValueEnumOrientNotebookPage() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(wxAuiNotebookOption::wxAUI_NB_TOP, wxT("Top"), _("Top"));
		AddEnumeration(wxAuiNotebookOption::wxAUI_NB_BOTTOM, wxT("Bottom"), _("Bottom"));
	}
private:
};

class SFRONTEND_API ibValueEnumHorizontalAlignment :
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

class SFRONTEND_API ibValueEnumVerticalAlignment :
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

class SFRONTEND_API ibValueEnumTitleLocation :
	public ibValueEnumeration<ibTitleLocation> {
	public:
	ibValueEnumTitleLocation() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibTitleLocation::eLeft, wxT("Left"), _("Left"));
		AddEnumeration(ibTitleLocation::eRight, wxT("Right"), _("Right"));
	}
private:
};

class SFRONTEND_API ibValueEnumRepresentation :
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
