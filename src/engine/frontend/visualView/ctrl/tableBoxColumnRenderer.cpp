#include "tableBoxColumnRenderer.h"

#include "frontend/win/ctrls/controlTextEditor.h"
#include "backend/appData.h"

wxWindow* ibDataViewValueRenderer::CreateEditorCtrl(wxWindow* dv,
	wxRect labelRect,
	const wxVariant& value)
{
	ibControlTextEditor* textEditor = new ibControlTextEditor;

	textEditor->SetDVCMode(true);
	
	// create the window hidden to prevent flicker
	textEditor->Show(false);
	
	bool result = textEditor->Create(dv, wxID_ANY, value,
		labelRect.GetPosition(),
		labelRect.GetSize());

	if (!result)
		return nullptr;

	textEditor->ShowSelectButton(m_tableBoxColumn->GetSelectButton());
	textEditor->ShowClearButton(m_tableBoxColumn->GetClearButton());
	textEditor->ShowOpenButton(m_tableBoxColumn->GetOpenButton());

	ibDataViewCtrl* parentWnd = dynamic_cast<ibDataViewCtrl*>(dv->GetParent());
	if (parentWnd != nullptr) {
		textEditor->SetBackgroundColour(parentWnd->GetBackgroundColour());
		textEditor->SetForegroundColour(parentWnd->GetForegroundColour());
		textEditor->SetFont(parentWnd->GetFont());
	}
	else {
		textEditor->SetBackgroundColour(dv->GetBackgroundColour());
		textEditor->SetForegroundColour(dv->GetForegroundColour());
		textEditor->SetFont(dv->GetFont());
	}

	textEditor->SetPasswordMode(m_tableBoxColumn->GetPasswordMode());
	textEditor->SetMultilineMode(m_tableBoxColumn->GetMultilineMode());
	// View-only form → the inline cell editor is read-only: the value shows and can be opened / copied, the
	// row can be selected, but the cell can't be edited. The column is a frame, so it reads view-only off its
	// owner form (IsReadOnly). Row-add / delete / copy are gated separately as data-modifying commands.
	textEditor->SetTextEditMode(m_tableBoxColumn->GetTextEditMode() && !m_tableBoxColumn->IsReadOnly());

	if (!appData->DesignerMode()) {
		
		textEditor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibValueModelTableBoxColumn::OnSelectButtonPressed, m_tableBoxColumn);
		textEditor->Bind(wxEVT_CONTROL_BUTTON_OPEN, &ibValueModelTableBoxColumn::OnOpenButtonPressed, m_tableBoxColumn);
		textEditor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibValueModelTableBoxColumn::OnClearButtonPressed, m_tableBoxColumn);
		
		textEditor->Bind(wxEVT_CONTROL_TEXT_ENTER, &ibValueModelTableBoxColumn::OnTextEnter, m_tableBoxColumn);
	}

	textEditor->LayoutControls();
	textEditor->Show(true);

	textEditor->SetInsertionPointEnd();
	return textEditor;
}

bool ibDataViewValueRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
	ibControlTextEditor* textEditor = wxDynamicCast(ctrl, ibControlTextEditor);

	if (textEditor == nullptr)
		return false;

	if (!appData->DesignerMode()) {
		
		textEditor->Unbind(wxEVT_CONTROL_BUTTON_SELECT, &ibValueModelTableBoxColumn::OnSelectButtonPressed, m_tableBoxColumn);
		textEditor->Unbind(wxEVT_CONTROL_BUTTON_OPEN, &ibValueModelTableBoxColumn::OnOpenButtonPressed, m_tableBoxColumn);
		textEditor->Unbind(wxEVT_CONTROL_BUTTON_CLEAR, &ibValueModelTableBoxColumn::OnClearButtonPressed, m_tableBoxColumn);

		textEditor->Unbind(wxEVT_CONTROL_TEXT_ENTER, &ibValueModelTableBoxColumn::OnTextEnter, m_tableBoxColumn);
	}

	value = textEditor->GetValue();
	return true;
}
