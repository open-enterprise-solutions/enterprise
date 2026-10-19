#ifndef __PROPERTY_EDITOR_H__
#define __PROPERTY_EDITOR_H__

#include <wx/bmpbndl.h>

#include <wx/propgrid/propgriddefs.h>
#include <wx/propgrid/editors.h>

class wxPGComboBoxAndButtonEditor : public wxPGComboBoxEditor {
	wxDECLARE_DYNAMIC_CLASS(wxPGComboBoxAndButtonEditor);
public:
	wxPGComboBoxAndButtonEditor() {}
	virtual ~wxPGComboBoxAndButtonEditor();
	virtual wxString GetName() const wxOVERRIDE;

	virtual wxPGWindowList CreateControls(wxPropertyGrid* propgrid,
		wxPGProperty* property,
		const wxPoint& pos,
		const wxSize& size) const wxOVERRIDE;

	virtual void UpdateControl(wxPGProperty* property, wxWindow* wnd) const wxOVERRIDE;
	virtual bool OnEvent(wxPropertyGrid* propgrid, wxPGProperty* property,
		wxWindow* wnd, wxEvent& event) const;

	virtual bool GetValueFromControl(wxVariant& variant, wxPGProperty* property, wxWindow* ctrl) const wxOVERRIDE;
};

class wxPGHyperLinkEditor : public wxPGEditor {
	wxDECLARE_DYNAMIC_CLASS(wxPGHyperLinkEditor);
public:
	wxPGHyperLinkEditor() : m_clicked(false) {}
	virtual ~wxPGHyperLinkEditor();
	virtual wxString GetName() const wxOVERRIDE;

	virtual wxPGWindowList CreateControls(wxPropertyGrid* propgrid,
		wxPGProperty* property,
		const wxPoint& pos,
		const wxSize& size) const wxOVERRIDE;

	virtual void UpdateControl(wxPGProperty* property, wxWindow* wnd) const wxOVERRIDE;
	virtual bool OnEvent(wxPropertyGrid* propgrid, wxPGProperty* property,
		wxWindow* wnd, wxEvent& event) const;

	virtual bool GetValueFromControl(wxVariant& variant, wxPGProperty* property, wxWindow* ctrl) const wxOVERRIDE;
private:
	mutable bool m_clicked;
};

WX_PG_DECLARE_EDITOR(ComboBoxAndButton);
WX_PG_DECLARE_EDITOR(HyperLink);

#endif