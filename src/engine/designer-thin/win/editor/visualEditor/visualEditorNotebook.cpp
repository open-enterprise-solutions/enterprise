#include "visualEditor.h"
#include "frmclient/artProvider/artProvider.h"
#include "frmclient/win/theme/luna_tabart.h"

#include "frmclient/docView/docView.h"   // ibDOC_READONLY
#include "core/serialize/dataBuilder.h"   // ibDataNode — an event's row, its Args
#include "protocol/protocol.h"            // ibProtocol::ibEvent::Args

#include "core/compiler/scriptParseCode.h"
#include "core/stringUtils.h"

void ibVisualEditorNotebook::CreateVisualEditor(ibModuleEditDocument* document, wxWindow* parent, wxWindowID id, long flags)
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
	return true;
}

bool ibVisualEditorNotebook::Redo()
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		m_visualEditor->Redo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		m_codeEditor->Redo();
	return true;
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

// THE DESKTOP'S (designer visualEditorNotebook.cpp) over the module this tab holds: the syntax and the template are the
// code editor's (the desktop's were the process's). The event's value itself is the server's — a row set (ModifyProperty).
void ibVisualEditorNotebook::ModifyEvent(ibProperty* event, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibParseCode parser(m_codeEditor->GetCodeStyle()); bool procFounded = false;

	const wxString strEvent = newValue.GetString();
	const wxString oldEvent = oldValue.GetString();

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
				if (stringUtils::CompareString(content.m_name, oldEvent) && !stringUtils::CompareString(content.m_name, strEvent) && changeSel) {
					int answer = wxMessageBox(_("Do you rename an existing procedure?"), _("Rename"), wxYES_NO | wxCENTRE);
					if (answer == wxYES) {
						wxString currentLine = m_codeEditor->GetLine(content.m_lineStart);
						currentLine.Replace(content.m_name, strEvent);
						m_codeEditor->Replace(m_codeEditor->GetLineEndPosition(content.m_lineStart - 1) + 1, m_codeEditor->GetLineEndPosition(content.m_lineStart) + 1, currentLine);
						procFounded = true; m_lineStart = content.m_lineStart; m_lineEnd = content.m_lineEnd;
					}
					break;
				}
				else if (stringUtils::CompareString(content.m_name, strEvent)) {
					procFounded = true; m_lineStart = content.m_lineStart; m_lineEnd = content.m_lineEnd;
					break;
				}
				else if (stringUtils::CompareString(content.m_name, oldEvent) && !changeSel) {
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

		// Its procedure's parameters — the event's row's Args (the server's ibEvent::GetProtocolValue).
		ibDataNode row;
		event->GetProtocolValue(row);
		if (const ibDataValue* const args = row.FindField(ibProtocol::ibEvent::Args)) {
			for (const ibDataValue& arg : args->AsArray()) {
				if (!prcArgs.IsEmpty()) prcArgs += ", ";
				prcArgs += arg.AsString();
			}
		}

		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);

		if (!procFounded) {
			int endPos = m_codeEditor->GetLineEndPosition(m_lineEnd);
			wxString offset = endPos > 0 ?
				wxT("\r\n\r\n") : wxT("");
			m_codeEditor->Replace(endPos, endPos,
				offset + m_codeEditor->MakeProcedureTemplate(strEvent, prcArgs));
			int patchLine = endPos > 0 ?
				2 : -1;
			m_lineStart = m_lineEnd + patchLine;
		}
		m_codeEditor->GotoLine(m_lineStart);
		m_codeEditor->SetSTCFocus(true);
	}
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
