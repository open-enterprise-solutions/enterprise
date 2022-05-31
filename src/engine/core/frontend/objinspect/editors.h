#ifndef _EDITORS_H__
#define _EDITORS_H__

#include <wx/bmpbndl.h>

#include <wx/propgrid/propgriddefs.h>
#include <wx/propgrid/editors.h>

class wxPGComboBoxAndButtonEditor : public wxPGComboBoxEditor
{
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

// -----------------------------------------------------------------------
// wxSlider-based property editor
// -----------------------------------------------------------------------

#if wxUSE_SLIDER
//
// Implement an editor control that allows using wxSlider to edit value of
// wxFloatProperty (and similar).
//
// Note that new editor classes needs to be registered before use.
// This can be accomplished using wxPGRegisterEditorClass macro.
// Registeration can also be performed in a constructor of a
// property that is likely to require the editor in question.
//

class wxPGSliderEditor : public wxPGEditor
{
	wxDECLARE_DYNAMIC_CLASS(wxPGSliderEditor);
public:
	wxPGSliderEditor()
		:
		m_max(10000)
	{
	}

	virtual ~wxPGSliderEditor();
	virtual wxPGWindowList CreateControls(wxPropertyGrid* propgrid,
		wxPGProperty* property,
		const wxPoint& pos,
		const wxSize& size) const;
	virtual void UpdateControl(wxPGProperty* property, wxWindow* wnd) const;
	virtual bool OnEvent(wxPropertyGrid* propgrid, wxPGProperty* property,
		wxWindow* wnd, wxEvent& event) const;
	virtual bool GetValueFromControl(wxVariant& variant, wxPGProperty* property, wxWindow* ctrl) const;
	virtual void SetValueToUnspecified(wxPGProperty* property, wxWindow* ctrl) const;

private:
	int m_max;
};
#endif // wxUSE_SLIDER

WX_PG_DECLARE_EDITOR(ComboBoxAndButton);

#endif