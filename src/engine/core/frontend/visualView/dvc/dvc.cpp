#include "dvc.h"

#include <wx/radiobut.h>

#include "frontend/controls/checkBox.h"
#include "frontend/controls/textEditor.h"

#include "frontend/visualView/controls/tableBox.h"

#include "appData.h"

wxWindow* CValueViewRenderer::CreateEditorCtrl(wxWindow* parent,
	wxRect labelRect,
	const wxVariant& value) 
{
	CTextCtrl* textCtrl = new CTextCtrl;

	textCtrl->SetDVCMode(true);

	textCtrl->SetPasswordMode(m_colControl->m_passwordMode);
	textCtrl->SetMultilineMode(m_colControl->m_multilineMode);
	textCtrl->SetTextEditMode(m_colControl->m_textEditMode);
	textCtrl->SetButtonSelect(m_colControl->m_selbutton);
	textCtrl->SetButtonList(m_colControl->m_listbutton);
	textCtrl->SetButtonClear(m_colControl->m_clearbutton);

	textCtrl->Create(parent, wxID_ANY, value,
		labelRect.GetPosition(),
		labelRect.GetSize());

	textCtrl->SetBackgroundColour(parent->GetBackgroundColour());
	textCtrl->SetForegroundColour(parent->GetForegroundColour());

	textCtrl->SetFont(parent->GetFont());

	if (!appData->DesignerMode()) {
		textCtrl->BindButtonSelect(&CValueTableBoxColumn::OnSelectButtonPressed, m_colControl);
		textCtrl->BindButtonList(&CValueTableBoxColumn::OnListButtonPressed, m_colControl);
		textCtrl->BindButtonClear(&CValueTableBoxColumn::OnClearButtonPressed, m_colControl);
		textCtrl->BindTextCtrl(&CValueTableBoxColumn::OnTextEnter, m_colControl);
		textCtrl->BindKillFocus(&CValueTableBoxColumn::OnKillFocus, m_colControl);
	}

	textCtrl->SetInsertionPointEnd(); 

	return textCtrl;
}

bool CValueViewRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
	CTextCtrl* textCtrl = wxDynamicCast(ctrl, CTextCtrl);
	
	if (!textCtrl)
		return false;

	if (!appData->DesignerMode()) {
		textCtrl->UnbindButtonSelect(&CValueTableBoxColumn::OnSelectButtonPressed, m_colControl);
		textCtrl->UnbindButtonList(&CValueTableBoxColumn::OnListButtonPressed, m_colControl);
		textCtrl->UnbindButtonClear(&CValueTableBoxColumn::OnClearButtonPressed, m_colControl);
		textCtrl->UnbindTextCtrl(&CValueTableBoxColumn::OnTextEnter, m_colControl);
		textCtrl->UnbindKillFocus(&CValueTableBoxColumn::OnKillFocus, m_colControl);
	}

	value = textCtrl->GetTextValue();	
	return true;
}
