#include "propertyEditor.h"

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/props.h>

#include "frontend/propertyManager/property/private/prop.h"

#include <wx/odcombo.h>
#include <wx/stattext.h>

// -----------------------------------------------------------------------
// wxComboBox-based property editor
// -----------------------------------------------------------------------

WX_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(ComboBoxAndButton,
	wxPGComboBoxAndButtonEditor,
	wxPGComboBoxEditor)

	wxPGWindowList wxPGComboBoxAndButtonEditor::CreateControls(wxPropertyGrid* propGrid,
		wxPGProperty* property,
		const wxPoint& pos,
		const wxSize& sz) const
{
	// Use one two units smaller to match size of the combo's dropbutton.
	// (normally a bigger button is used because it looks better)
	int bt_wid = sz.y;
	bt_wid -= 2;
	wxSize bt_sz(bt_wid, bt_wid);

	// Position of button.
	wxPoint bt_pos(pos.x + sz.x - bt_sz.x, pos.y);
#ifdef __WXOSX__
	bt_pos.y -= 1;
#else
	bt_pos.y += 1;
#endif

	wxWindow* bt = propGrid->GenerateEditorButton(bt_pos, bt_sz);

	// Size of choice.
	wxSize ch_sz(sz.x - bt->GetSize().x, sz.y);

#ifdef __WXOSX__
	ch_sz.x -= 2; // was wxPG_TEXTCTRL_AND_BUTTON_SPACING, removed in wxWidgets 3.3
#endif

	wxWindow* ch = CreateControlsBase(propGrid, property,
		pos, ch_sz, 0);

	if (!property->HasFlag(wxPGPropertyFlags_ActiveButton)) {
		bt->Disable();
	}

#ifdef __WXMSW__
	bt->Show();
#endif


	return wxPGWindowList(ch, bt);
}

bool wxPGComboBoxAndButtonEditor::OnEvent(wxPropertyGrid* propGrid,
	wxPGProperty* property,
	wxWindow* ctrl,
	wxEvent& event) const
{
	wxOwnerDrawnComboBox* cb =
		dynamic_cast<wxOwnerDrawnComboBox*>(ctrl);

	wxWindow* textCtrl = cb ?
		cb->GetTextCtrl() : nullptr;

	wxWindow* wndEditor2 =
		propGrid->GetEditorControlSecondary();

	if (wndEditor2 != nullptr) {
		bool needButton = property->HasFlag(wxPGPropertyFlags_ActiveButton);
		if (needButton != wndEditor2->IsEnabled()) {
			wndEditor2->Enable(needButton);
		}
	}

	if (wxPGTextCtrlEditor::OnTextCtrlEvent(propGrid, property, textCtrl, event))
		return true;

	return wxPGChoiceEditor::OnEvent(propGrid, property, ctrl, event);
}

void wxPGComboBoxAndButtonEditor::UpdateControl(wxPGProperty* property, wxWindow* ctrl) const
{
	wxOwnerDrawnComboBox* cb = (wxOwnerDrawnComboBox*)ctrl;
	const int index = property->GetChoiceSelection();
	wxString s = property->GetValueAsString(wxPGPropValFormatFlags::EditableValue);
	cb->SetSelection(index);
	property->GetGrid()->SetupTextCtrlValue(s);
	cb->SetValue(s);
}

bool wxPGComboBoxAndButtonEditor::GetValueFromControl(wxVariant& variant, wxPGProperty* property, wxWindow* ctrl) const
{
	wxOwnerDrawnComboBox* cb = (wxOwnerDrawnComboBox*)ctrl;
	wxString textVal = cb->GetValue();

	if (property->UsesAutoUnspecified() && textVal.empty()) {
		variant.MakeNull();
		return true;
	}

	int selection = cb->FindString(textVal); bool res = true;

	if (selection != wxNOT_FOUND) {
		res = property->IntToValue(variant, selection, wxPGPropValFormatFlags::EditableValue | wxPGPropValFormatFlags::PropertySpecific);
	}
	else {
		res = property->StringToValue(variant, textVal, wxPGPropValFormatFlags::EditableValue | wxPGPropValFormatFlags::PropertySpecific);
	}

	// Changing unspecified always causes event (returning
	// true here should be enough to trigger it).
	if (!res && variant.IsNull())
		res = true;

	return res;
}

wxPGComboBoxAndButtonEditor::~wxPGComboBoxAndButtonEditor()
{
	wxPG_EDITOR(ComboBoxAndButton) = nullptr;
}

WX_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(HyperLink,
	wxPGHyperLinkEditor,
	wxPGEditor)

	wxPGWindowList wxPGHyperLinkEditor::CreateControls(wxPropertyGrid* propGrid,
		wxPGProperty* property,
		const wxPoint& pos,
		const wxSize& sz) const
{

	// Use two stage creation to allow cleaner display on wxMSW
	wxStaticText* ctrl = new wxStaticText();

#ifdef __WXMSW__
	ctrl->Hide();
#endif

	ctrl->Create(propGrid->GetPanel(),
		wxID_ANY,
		property->GetValueAsString(),
		{ pos.x + 3, pos.y + 2 },
		{ sz.x - 3, sz.y - 2 },
		wxBORDER_NONE);

	ctrl->SetBackgroundColour(propGrid->GetPropertyBackgroundColour(property));
	ctrl->SetForegroundColour(propGrid->GetPropertyTextColour(property));

	wxFont font = ctrl->GetFont();
	font.SetUnderlined(true);
	ctrl->SetFont(font);

	ctrl->SetCursor(wxCURSOR_HAND);

	// Connect all required events to grid's OnCustomEditorEvent
	// (all relevenat wxTextCtrl, wxComboBox and wxButton events are
	// already connected)

#ifdef __WXMSW__
	ctrl->Show();
#endif

	return ctrl;
}

bool wxPGHyperLinkEditor::OnEvent(wxPropertyGrid* propGrid,
	wxPGProperty* property,
	wxWindow* wnd,
	wxEvent& event) const
{
	if (event.GetEventType() == wxEVT_LEFT_DOWN)
	{
		wxStaticText* ctrl = wxDynamicCast(wnd, wxStaticText);
		if (ctrl)
		{
			// ⭐⭐ THE CLICK IS A NAME STAMPED ON THE VALUE — the data is left alone, so the cell still
			// has its variant to read and to clone. What the cell watches for is the NAME, never a
			// bool: there is no payload here to test.
			wxVariant variant = property->GetValue();
			variant.SetName(wxT("hyperLink_clicked"));
			property->SetValue(variant);

			// 🛑 AND THE NAME IS *NOT* TAKEN BACK OFF HERE. Putting a second SetValue in — stamp, let
			// the cell react, un-stamp — reads as the tidy fix and is not one: every cell's OnSetValue
			// runs a second time inside this click, and the ones that DEFER their work (all of them —
			// a modal opened inside a click takes the mouse with it) then run against a grid that has
			// moved on. It cost a crash in the hyperlink's deferred OpenObjectForm and stopped the
			// composer's window from opening at all (measured, 2026-08-24).
			//
			// The click is CONSUMED BY WHOEVER ANSWERS IT — see the composer's and the list's cells:
			// they clear the name off m_value the moment they take it. That is the reader ending its
			// own signal, which is one place per road rather than one guess for all of them.
			return true;
		}
	}
	return false;
}

void wxPGHyperLinkEditor::UpdateControl(wxPGProperty* property, wxWindow* wnd) const
{
	wxStaticText* ctrl = (wxStaticText*)wnd;
	assert(ctrl && ctrl->IsKindOf(CLASSINFO(wxStaticText)));
}

bool wxPGHyperLinkEditor::GetValueFromControl(wxVariant& variant, wxPGProperty* property, wxWindow* wnd) const
{
	wxStaticText* ctrl = (wxStaticText*)wnd;
	assert(ctrl && ctrl->IsKindOf(CLASSINFO(wxStaticText)));
	return false;
}

wxPGHyperLinkEditor::~wxPGHyperLinkEditor()
{
	wxPG_EDITOR(HyperLink) = nullptr;
}

#include <wx/module.h>

class wxOESEditorModule : public wxModule
{
public:
	wxOESEditorModule() : wxModule() {}
	virtual bool OnInit() {
		//register new editor 
		wxPGRegisterEditorClass(ComboBoxAndButton);
		wxPGRegisterEditorClass(HyperLink);
		return true;
	}
	virtual void OnExit() {}
private:
	wxDECLARE_DYNAMIC_CLASS(wxOESEditorModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxOESEditorModule, wxModule)