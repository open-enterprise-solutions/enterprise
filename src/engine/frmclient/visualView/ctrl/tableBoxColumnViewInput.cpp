////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a table column's Input — its cells typed into
////////////////////////////////////////////////////////////////////////////

#include "tableBox.h"

#include "deskgui/univ/ctrl/checkBox.h"   // ibCheckBox — a boolean cell's editor
#include "deskgui/univ/ctrl/textCtrl.h"   // ibTextCtrl — any other's

wxWindow* ibViewCellRendererInput::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const ibVariant& value)
{
	// The editor of the edit before, if it was cancelled, is gone: neither pointer may answer for the new one.
	m_textEditor = nullptr;
	m_checkEditor = nullptr;

	if (value.GetType() == ibValueTypes::TYPE_BOOLEAN) {
		ibCheckBox* const checkEditor = new ibCheckBox(parent, wxID_ANY, wxEmptyString, labelRect.GetPosition(),
			labelRect.GetSize());
		checkEditor->SetBackgroundColour(GetView()->GetBackgroundColour());
		checkEditor->SetValue(value.GetBoolean());
		m_checkEditor = checkEditor;
		return checkEditor;
	}

	// Under a mask the editor starts from the text the mask is typed over — the server's EditText where the cell shows
	// another (a long date, digit groups) — laid into it: an empty date opens as its skeleton.
	const ibInputMask& mask = m_column.GetMask();
	wxString text = value.GetType() == ibValueTypes::TYPE_EMPTY ? wxString() : value.GetString().ToWxString();
	if (!mask.IsEmpty())
		text = mask.Apply(ibString(ibViewTableModel::GetCellEditText(m_item, GetOwner()->GetModelColumn()))).ToWxString();

	ibTextCtrl* const textEditor = new ibTextCtrl;
	textEditor->Show(false);   // hidden while it is made: no flicker
	if (!textEditor->Create(parent, wxID_ANY, text, labelRect.GetPosition(), labelRect.GetSize()))
		return nullptr;

	textEditor->ShowSelectButton(m_column.GetSelectButton());
	textEditor->ShowOpenButton(m_column.GetOpenButton());
	textEditor->ShowClearButton(m_column.GetClearButton());

	wxWindow* const view = GetView();
	textEditor->SetBackgroundColour(view->GetBackgroundColour());
	textEditor->SetForegroundColour(view->GetForegroundColour());
	textEditor->SetFont(view->GetFont());

	textEditor->SetPassword(m_column.GetPasswordMode());
	textEditor->SetMultiLine(m_column.GetMultilineMode());
	textEditor->SetMask(mask);
	textEditor->SetEditable(m_column.GetTextEditMode());
	// …its value still picked and cleared where its text is not typed: an editor is opened only on a cell that may be
	// written (IsCellEditable), and Select and Clear stay (Max, 2026-11-24).
	textEditor->EnableSelectButton(true);
	textEditor->EnableClearButton(true);

	textEditor->Show(true);
	// …a masked one all selected, as a masked field is entered: the first key starts the value anew.
	if (mask.IsEmpty())
		textEditor->SetInsertionPointEnd();
	else
		textEditor->SelectAll();
	m_textEditor = textEditor;
	return textEditor;
}

bool ibViewCellRendererInput::GetValueFromEditorCtrl(wxWindow* editor, ibVariant& value)
{
	if (editor == nullptr)
		return false;
	if (editor == m_checkEditor)
		value = m_checkEditor->GetValue();
	else if (editor == m_textEditor)
		value = m_textEditor->GetValue();
	else
		return false;
	m_textEditor = nullptr;
	m_checkEditor = nullptr;
	return true;
}
