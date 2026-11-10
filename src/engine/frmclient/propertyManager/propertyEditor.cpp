#include "propertyEditor.h"

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/props.h"

#include "frmclient/propertyManager/property/private/prop.h"

#include "deskgui/univ/ctrl/comboBox.h"     // ibComboBox — the combo and button cell's editor
#include "deskgui/univ/ctrl/staticText.h"   // ibStaticText — the link cell's editor

// -----------------------------------------------------------------------
// wxComboBox-based property editor
// -----------------------------------------------------------------------

IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(ComboBoxAndButton,
	ibPGComboBoxAndButtonEditor,
	ibPGComboBoxEditor)

	ibPGWindowList ibPGComboBoxAndButtonEditor::CreateControls(ibPropertyGrid* propGrid,
		ibPGProperty* property,
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
	ch_sz.x -= 2; // was ibPG_TEXTCTRL_AND_BUTTON_SPACING, removed in wxWidgets 3.3
#endif

	wxWindow* ch = CreateControlsBase(propGrid, property,
		pos, ch_sz, 0);

	if (!property->HasFlag(ibPGPropertyFlags_ActiveButton)) {
		bt->Disable();
	}

#ifdef __WXMSW__
	bt->Show();
#endif


	return ibPGWindowList(ch, bt);
}

bool ibPGComboBoxAndButtonEditor::OnEvent(ibPropertyGrid* propGrid,
	ibPGProperty* property,
	wxWindow* ctrl,
	wxEvent& event) const
{
	ibComboBox* cb =
		dynamic_cast<ibComboBox*>(ctrl);

	wxWindow* textCtrl = cb ?
		cb->GetTextCtrl() : nullptr;

	wxWindow* wndEditor2 =
		propGrid->GetEditorControlSecondary();

	if (wndEditor2 != nullptr) {
		bool needButton = property->HasFlag(ibPGPropertyFlags_ActiveButton);
		if (needButton != wndEditor2->IsEnabled()) {
			wndEditor2->Enable(needButton);
		}
	}

	if (ibPGTextCtrlEditor::OnTextCtrlEvent(propGrid, property, textCtrl, event))
		return true;

	return ibPGChoiceEditor::OnEvent(propGrid, property, ctrl, event);
}

void ibPGComboBoxAndButtonEditor::UpdateControl(ibPGProperty* property, wxWindow* ctrl) const
{
	ibComboBox* cb = (ibComboBox*)ctrl;
	const int index = property->GetChoiceSelection();
	wxString s = property->GetValueAsString(ibPGPropValFormatFlags::EditableValue);
	cb->SetSelection(index);
	property->GetGrid()->SetupTextCtrlValue(s);
	cb->SetValue(s);
}

bool ibPGComboBoxAndButtonEditor::GetValueFromControl(ibVariant& variant, ibPGProperty* property, wxWindow* ctrl) const
{
	ibComboBox* cb = (ibComboBox*)ctrl;
	wxString textVal = cb->GetValue();

	if (property->UsesAutoUnspecified() && textVal.empty()) {
		variant.Reset();
		return true;
	}

	int selection = cb->FindString(textVal); bool res = true;

	if (selection != wxNOT_FOUND) {
		res = property->IntToValue(variant, selection, ibPGPropValFormatFlags::EditableValue | ibPGPropValFormatFlags::PropertySpecific);
	}
	else {
		res = property->StringToValue(variant, textVal, ibPGPropValFormatFlags::EditableValue | ibPGPropValFormatFlags::PropertySpecific);
	}

	// Changing unspecified always causes event (returning
	// true here should be enough to trigger it).
	if (!res && variant.GetType() == ibValueTypes::TYPE_EMPTY)
		res = true;

	return res;
}

ibPGComboBoxAndButtonEditor::~ibPGComboBoxAndButtonEditor()
{
	ibPG_EDITOR(ComboBoxAndButton) = nullptr;
}

IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(HyperLink,
	ibPGHyperLinkEditor,
	ibPGEditor)

	ibPGWindowList ibPGHyperLinkEditor::CreateControls(ibPropertyGrid* propGrid,
		ibPGProperty* property,
		const wxPoint& pos,
		const wxSize& sz) const
{

	// Use two stage creation to allow cleaner display on wxMSW
	ibStaticText* ctrl = new ibStaticText();

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

	// a link as the library draws one: underlined in the theme's link colour, the hand over it
	ctrl->SetHyperlink();

	// Connect all required events to grid's OnCustomEditorEvent
	// (all relevenat ibTextCtrl, wxComboBox and wxButton events are
	// already connected)

#ifdef __WXMSW__
	ctrl->Show();
#endif

	return ctrl;
}

bool ibPGHyperLinkEditor::OnEvent(ibPropertyGrid* propGrid,
	ibPGProperty* property,
	wxWindow* wnd,
	wxEvent& event) const
{
	if (event.GetEventType() == wxEVT_LEFT_DOWN)
	{
		ibStaticText* ctrl = wxDynamicCast(wnd, ibStaticText);
		if (ctrl)
		{
			// ⭐⭐ THE CLICK IS THE CELL'S — answered in its own OnEvent, which the grid calls right after this one,
			// inside the same event (ibPropertyGrid::HandleCustomEditorEvent): what the cell sets there by
			// SetValueInEvent is the pending value. The value is left alone — the cell still has it to read. Before the
			// fork the click was a name stamped on the value (wx's variant had one; the core's has none), and every
			// SetValue that came after it found the name still standing.
			return true;
		}
	}
	return false;
}

void ibPGHyperLinkEditor::UpdateControl(ibPGProperty* property, wxWindow* wnd) const
{
	assert(wxDynamicCast(wnd, ibStaticText) != nullptr);
}

bool ibPGHyperLinkEditor::GetValueFromControl(ibVariant& variant, ibPGProperty* property, wxWindow* wnd) const
{
	assert(wxDynamicCast(wnd, ibStaticText) != nullptr);
	return false;
}

ibPGHyperLinkEditor::~ibPGHyperLinkEditor()
{
	ibPG_EDITOR(HyperLink) = nullptr;
}

#include <wx/module.h>

class wxOESEditorModule : public wxModule
{
public:
	wxOESEditorModule() : wxModule() {}
	virtual bool OnInit() {
		//register new editor 
		ibPGRegisterEditorClass(ComboBoxAndButton);
		ibPGRegisterEditorClass(HyperLink);
		return true;
	}
	virtual void OnExit() {}
private:
	wxDECLARE_DYNAMIC_CLASS(wxOESEditorModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxOESEditorModule, wxModule)