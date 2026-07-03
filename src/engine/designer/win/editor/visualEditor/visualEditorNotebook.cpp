#include "visualEditor.h"
#include "frontend/artProvider/artProvider.h"
#include "frontend/win/theme/luna_tabart.h"

#include "frontend/win/editor/codeEditor/codeEditor.h"
#include "frontend/win/editor/codeEditor/codeEditorParser.h"
#include "frontend/mainFrame/objinspect/objinspect.h"   // objectInspector (SelectPropertyObject)
#include "frontend/visualView/layers/commandBar.h"       // ibValueCommandBarItem (tree reveal)

void ibVisualEditorNotebook::CreateVisualEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id, long flags)
{
	wxAuiNotebook::AddPage(m_visualEditor, _("Designer"), false, wxArtProvider::GetBitmapBundle(wxART_DESIGNER_PAGE, wxART_DOC_FORM));
	wxAuiNotebook::AddPage(m_codeEditor, _("Code"), false, wxArtProvider::GetBitmapBundle(wxART_CODE_PAGE, wxART_DOC_FORM));
	m_visualEditor->SetReadOnly(flags == ibDOC_READONLY);
	m_codeEditor->SetReadOnly(flags == ibDOC_READONLY);
	wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);

	wxAuiNotebook::Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibVisualEditorNotebook::OnPageChanged, this);
	wxAuiNotebook::SetArtProvider(new wxAuiLunaTabArt());
}

void ibVisualEditorNotebook::DestroyVisualEditor()
{
	wxAuiNotebook::Unbind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibVisualEditorNotebook::OnPageChanged, this);
}

bool ibVisualEditorNotebook::Undo()
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		m_visualEditor->Undo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		m_codeEditor->Undo();
	return false;
}

bool ibVisualEditorNotebook::Redo()
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		m_visualEditor->Redo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		m_codeEditor->Redo();
	return false;
}

bool ibVisualEditorNotebook::CanUndo() const
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		return m_visualEditor->CanUndo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		return m_codeEditor->CanUndo();
	return false;
}

bool ibVisualEditorNotebook::CanRedo() const
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		return m_visualEditor->CanRedo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		return m_codeEditor->CanRedo();
	return false;
}

void ibVisualEditorNotebook::ModifyEvent(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibParserModule parser; bool procFounded = false;

	const wxString& strEvent = newValue.GetString();

	unsigned int m_lineStart = m_codeEditor->GetLineCount();
	unsigned int m_lineEnd = m_lineStart;

	bool changeSel = true;

	long action = wxNOT_FOUND;
	if (strEvent.ToLong(&action)) {
		changeSel = false;
	}
	else if (strEvent.IsEmpty()) {
		changeSel = false;
	}

	if (parser.ParseModule(m_codeEditor->GetText())) {
		for (auto content : parser.GetAllContent()) {
			if (content.m_eType == ibContentType::eProcedure ||
				content.m_eType == ibContentType::eFunction ||
				content.m_eType == ibContentType::eExportProcedure ||
				content.m_eType == ibContentType::eExportFunction) {
				m_lineStart = content.m_lineStart; m_lineEnd = content.m_lineEnd;
				if (stringUtils::CompareString(content.m_name, oldValue) && !stringUtils::CompareString(content.m_name, newValue) && changeSel) {
					int answer = wxMessageBox(_("Do you rename an existing procedure?"), _("Rename"), wxYES_NO | wxCENTRE);
					if (answer == wxYES) {
						wxString currentLine = m_codeEditor->GetLine(content.m_lineStart);
						currentLine.Replace(content.m_name, newValue);
						m_codeEditor->Replace(m_codeEditor->GetLineEndPosition(content.m_lineStart - 1) + 1, m_codeEditor->GetLineEndPosition(content.m_lineStart) + 1, currentLine);
						procFounded = true; m_lineStart = content.m_lineStart; m_lineEnd = content.m_lineEnd;
					}
					break;
				}
				else if (stringUtils::CompareString(content.m_name, newValue)) {
					procFounded = true; m_lineStart = content.m_lineStart; m_lineEnd = content.m_lineEnd;
					break;
				}
				else if (stringUtils::CompareString(content.m_name, oldValue) && !changeSel) {
					int answer = wxMessageBox(_("Do you delete an existing procedure?"), _("Delete"), wxYES_NO | wxCENTRE);
					if (answer == wxYES) {
						m_codeEditor->Replace(
							m_codeEditor->GetLineEndPosition(content.m_lineStart - 1) + 1,
							m_codeEditor->GetLineEndPosition(content.m_lineEnd + 1),
							wxEmptyString
						);
					}
					procFounded = true;
				}
			}
		}
	}

	if (changeSel) {

		wxString prcArgs;

		for (auto& args : event->GetArgs()) {
			if (!prcArgs.IsEmpty()) prcArgs += ", ";
			prcArgs += args;
		}

		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);

		if (!procFounded) {
			int endPos = m_codeEditor->GetLineEndPosition(m_lineEnd);
			wxString offset = endPos > 0 ?
				wxT("\r\n\r\n") : wxT("");
			m_codeEditor->Replace(endPos, endPos,
				offset + ibCodeEditor::MakeProcedureTemplate(strEvent, prcArgs));
			int patchLine = endPos > 0 ?
				2 : -1;
			m_lineStart = m_lineEnd + patchLine;
		}
		m_codeEditor->GotoLine(m_lineStart);
		m_codeEditor->SetSTCFocus(true);
	}

	m_visualEditor->ModifyEvent(event, oldValue, newValue);
}

void ibVisualEditorNotebook::SelectPropertyObject(ibPropertyObject* obj)
{
	if (obj == nullptr)
		return;
	if (!objectInspector->IsShownInspector())
		objectInspector->ShowInspector();
	objectInspector->SelectObject(obj, true);
	// Reveal a command in the object tree too (EnsureVisible + SelectItem) — so a click on the
	// rendered toolbar / an add-move from the menu also highlights the node. Bar nodes and plain
	// property objects aren't in the command map; SelectCommandItem no-ops for them.
	if (m_visualEditor != nullptr && m_visualEditor->GetObjectTree() != nullptr) {
		if (ibValueCommandBarItem* citem = dynamic_cast<ibValueCommandBarItem*>(obj))
			m_visualEditor->GetObjectTree()->SelectCommandItem(citem);
	}
	if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
		wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
}

void ibVisualEditorNotebook::ActivateEditor()
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		m_visualEditor->ActivateEditor();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		m_codeEditor->ActivateEditor();
}

void ibVisualEditorNotebook::OnPageChanged(wxAuiNotebookEvent& event) {
	m_visualEditor->Activate();
	event.Skip();
}