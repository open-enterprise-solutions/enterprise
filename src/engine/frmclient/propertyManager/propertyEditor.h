#ifndef __PROPERTY_EDITOR_H__
#define __PROPERTY_EDITOR_H__

#include <wx/bmpbndl.h>

#include "deskgui/univ/propgrid/propgriddefs.h"
#include "deskgui/univ/propgrid/editors.h"

class ibPGComboBoxAndButtonEditor : public ibPGComboBoxEditor {
	wxDECLARE_DYNAMIC_CLASS(ibPGComboBoxAndButtonEditor);
public:
	ibPGComboBoxAndButtonEditor() {}
	virtual ~ibPGComboBoxAndButtonEditor();
	virtual wxString GetName() const wxOVERRIDE;

	virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
		ibPGProperty* property,
		const wxPoint& pos,
		const wxSize& size) const wxOVERRIDE;

	virtual void UpdateControl(ibPGProperty* property, wxWindow* wnd) const wxOVERRIDE;
	virtual bool OnEvent(ibPropertyGrid* propgrid, ibPGProperty* property,
		wxWindow* wnd, wxEvent& event) const;

	virtual bool GetValueFromControl(ibVariant& variant, ibPGProperty* property, wxWindow* ctrl) const wxOVERRIDE;
};

class ibPGHyperLinkEditor : public ibPGEditor {
	wxDECLARE_DYNAMIC_CLASS(ibPGHyperLinkEditor);
public:
	ibPGHyperLinkEditor() : m_clicked(false) {}
	virtual ~ibPGHyperLinkEditor();
	virtual wxString GetName() const wxOVERRIDE;

	virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
		ibPGProperty* property,
		const wxPoint& pos,
		const wxSize& size) const wxOVERRIDE;

	virtual void UpdateControl(ibPGProperty* property, wxWindow* wnd) const wxOVERRIDE;
	virtual bool OnEvent(ibPropertyGrid* propgrid, ibPGProperty* property,
		wxWindow* wnd, wxEvent& event) const;

	virtual bool GetValueFromControl(ibVariant& variant, ibPGProperty* property, wxWindow* ctrl) const wxOVERRIDE;
private:
	mutable bool m_clicked;
};

IB_PG_DECLARE_EDITOR(ComboBoxAndButton);
IB_PG_DECLARE_EDITOR(HyperLink);

#endif