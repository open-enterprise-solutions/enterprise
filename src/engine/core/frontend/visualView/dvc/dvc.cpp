#include "dvc.h"

#include "frontend/controls/checkBoxEditor.h"
#include "frontend/controls/textEditor.h"

#include "frontend/visualView/controls/tableBox.h"

#include "appData.h"

wxWindow* CValueViewRenderer::CreateEditorCtrl(wxWindow* dv,
	wxRect labelRect,
	const wxVariant& value)
{
	wxTextContainerCtrl* textCtrl = new wxTextContainerCtrl;

	textCtrl->SetDVCMode(true);

	textCtrl->SetPasswordMode(m_colControl->GetPasswordMode());
	textCtrl->SetMultilineMode(m_colControl->GetMultilineMode());
	textCtrl->SetTextEditMode(m_colControl->GetTextEditMode());
	textCtrl->SetButtonSelect(m_colControl->GetSelectButton());
	textCtrl->SetButtonClear(m_colControl->GetClearButton());
	textCtrl->SetButtonOpen(m_colControl->GetOpenButton());

	bool result = textCtrl->Create(dv, wxID_ANY, value,
		labelRect.GetPosition(),
		labelRect.GetSize());

	if (!result)
		return NULL;

	wxDataViewCtrl* parentWnd = dynamic_cast<wxDataViewCtrl*>(dv->GetParent());
	if (parentWnd != NULL) {
		textCtrl->SetBackgroundColour(parentWnd->GetBackgroundColour());
		textCtrl->SetForegroundColour(parentWnd->GetForegroundColour());
		textCtrl->SetFont(parentWnd->GetFont());
	}
	else {
		textCtrl->SetBackgroundColour(dv->GetBackgroundColour());
		textCtrl->SetForegroundColour(dv->GetForegroundColour());
		textCtrl->SetFont(dv->GetFont());
	}

	if (!appData->DesignerMode()) {
		textCtrl->BindButtonSelect(&CValueTableBoxColumn::OnSelectButtonPressed, m_colControl);
		textCtrl->BindButtonOpen(&CValueTableBoxColumn::OnOpenButtonPressed, m_colControl);
		textCtrl->BindButtonClear(&CValueTableBoxColumn::OnClearButtonPressed, m_colControl);
		textCtrl->BindTextEnter(&CValueTableBoxColumn::OnTextEnter, m_colControl);
		textCtrl->BindKillFocus(&CValueTableBoxColumn::OnKillFocus, m_colControl);
	}

	textCtrl->SetInsertionPointEnd();
	return textCtrl;
}

bool CValueViewRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
	wxTextContainerCtrl* textCtrl = wxDynamicCast(ctrl, wxTextContainerCtrl);

	if (textCtrl == NULL)
		return false;

	if (!appData->DesignerMode()) {
		textCtrl->UnbindButtonSelect(&CValueTableBoxColumn::OnSelectButtonPressed, m_colControl);
		textCtrl->UnbindButtonOpen(&CValueTableBoxColumn::OnOpenButtonPressed, m_colControl);
		textCtrl->UnbindButtonClear(&CValueTableBoxColumn::OnClearButtonPressed, m_colControl);
		textCtrl->UnbindTextEnter(&CValueTableBoxColumn::OnTextEnter, m_colControl);
		textCtrl->UnbindKillFocus(&CValueTableBoxColumn::OnKillFocus, m_colControl);
	}

	value = textCtrl->GetTextValue();
	return true;
}
