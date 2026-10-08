#include "tableBoxColumnRenderer.h"

#include "frmclient/win/ctrls/controlTextEditor.h"

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

	// The look of the view it is edited in — the desktop's.
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

	// (A column's buttons, its password and multiline modes and its handlers were bound here — a settings cell
	// overrides this editor with its own.)
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

	value = textEditor->GetValue();
	return true;
}
