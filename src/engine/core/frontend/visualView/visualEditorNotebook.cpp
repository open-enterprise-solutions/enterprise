#include "visualEditor.h"
#include "art/artProvider.h"
#include "frontend/codeEditor/codeEditorParser.h"
#include "frontend/theme/luna_auitabart.h"
#include "utils/stringUtils.h"

static std::vector<CVisualEditorNotebook*> s_visualEditor = {};

void CVisualEditorNotebook::CreateVisualEditor(CDocument* document, wxWindow* parent, wxWindowID id, long flags)
{
	wxAuiNotebook::AddPage(m_visualEditor, _("Designer"), false, wxArtProvider::GetBitmap(wxART_DESIGNER_PAGE, wxART_DOC_FORM));
	wxAuiNotebook::AddPage(m_codeEditor, _("Code"), false, wxArtProvider::GetBitmap(wxART_CODE_PAGE, wxART_DOC_FORM));
	m_visualEditor->SetReadOnly(flags == wxDOC_READONLY); 
	m_codeEditor->SetReadOnly(flags == wxDOC_READONLY);
	wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);

	wxAuiNotebook::Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &CVisualEditorNotebook::OnPageChanged, this);
	wxAuiNotebook::SetArtProvider(new CLunaTabArt());

	s_visualEditor.push_back(this);
}

void CVisualEditorNotebook::DestroyVisualEditor()
{
	auto foundedIt = std::find(s_visualEditor.begin(), s_visualEditor.end(), this);
	if (foundedIt != s_visualEditor.end())
		s_visualEditor.erase(foundedIt);

	wxAuiNotebook::Unbind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &CVisualEditorNotebook::OnPageChanged, this);
}

CVisualEditorNotebook* CVisualEditorNotebook::FindEditorByForm(CValueForm* valueForm)
{
	auto foundedIt = std::find_if(s_visualEditor.begin(), s_visualEditor.end(),
		[valueForm](CVisualEditorNotebook* visualNotebook) {
			return valueForm == visualNotebook->GetValueForm();
		}
	);

	if (foundedIt != s_visualEditor.end())
		return *foundedIt;

	return NULL;
}

bool CVisualEditorNotebook::Undo()
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		m_visualEditor->Undo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		m_codeEditor->Undo();
	return false;
}

bool CVisualEditorNotebook::Redo()
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		m_visualEditor->Redo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		m_codeEditor->Redo();
	return false;
}

bool CVisualEditorNotebook::CanUndo() const
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		return m_visualEditor->CanUndo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		return m_codeEditor->CanUndo();
	return false;
}

bool CVisualEditorNotebook::CanRedo() const
{
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER)
		return m_visualEditor->CanRedo();
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR)
		return m_codeEditor->CanRedo();
	return false;
}

void CVisualEditorNotebook::ModifyEvent(Event* event, const wxString& newValue)
{
	wxString prcArgs = "";
	for (auto& args : event->GetArgs()) {
		if (!prcArgs.IsEmpty()) {
			prcArgs += ", ";
		}
		prcArgs += args;
	}

	CParserModule parser; bool procFounded = false;

	unsigned int lineStart = m_codeEditor->GetLineCount();
	unsigned int lineEnd = lineStart;

	if (event->GetType() == ET_ACTION) {
		action_identifier_t action = wxNOT_FOUND;
		procFounded = newValue.ToInt(&action);
	}

	if (parser.ParseModule(m_codeEditor->GetText())) {
		for (auto content : parser.GetAllContent()) {
			if (content.eType == eContentType::eProcedure ||
				content.eType == eContentType::eFunction ||
				content.eType == eContentType::eExportProcedure ||
				content.eType == eContentType::eExportFunction) {
				lineStart = content.nLineStart; lineEnd = content.nLineEnd;
				if (event->GetType() == ET_EVENT) {
					if (StringUtils::CompareString(content.sName, event->GetValue())
						&& !StringUtils::CompareString(content.sName, newValue) && !newValue.IsEmpty()) {
						int answer = wxMessageBox(_("Do you rename an existing procedure?"), _("Rename"), wxYES_NO | wxCENTRE);
						if (answer == wxYES) {
							wxString currentLine = m_codeEditor->GetLine(content.nLineStart);
							currentLine.Replace(event->GetValue(), newValue);
							m_codeEditor->Replace(m_codeEditor->GetLineEndPosition(content.nLineStart - 1) + 1, m_codeEditor->GetLineEndPosition(content.nLineStart) + 1, currentLine);
							procFounded = true; lineStart = content.nLineStart; lineEnd = content.nLineEnd;
						}
						break;
					}
					else if (StringUtils::CompareString(content.sName, newValue)) {
						procFounded = true; lineStart = content.nLineStart; lineEnd = content.nLineEnd;
						break;
					}
					else if (StringUtils::CompareString(content.sName, event->GetValue())
						&& newValue.IsEmpty()) {
						int answer = wxMessageBox(_("Do you delete an existing procedure?"), _("Delete"), wxYES_NO | wxCENTRE);
						if (answer == wxYES) {
							m_codeEditor->Replace(
								m_codeEditor->GetLineEndPosition(content.nLineStart - 1) + 1,
								m_codeEditor->GetLineEndPosition(content.nLineEnd + 1),
								wxEmptyString
							);
						}
						procFounded = true;
					}
				}
				else if (event->GetType() == ET_ACTION) {
					action_identifier_t action = wxNOT_FOUND;
					if (newValue.ToInt(&action)) {
						const wxString& oldValue = event->GetValue();
						if (!oldValue.ToInt(&action)) {
							if (StringUtils::CompareString(content.sName, oldValue)) {
								int answer = wxMessageBox(_("Do you delete an existing procedure?"), _("Delete"), wxYES_NO | wxCENTRE);
								if (answer == wxYES) {
									m_codeEditor->Replace(
										m_codeEditor->GetLineEndPosition(content.nLineStart - 1) + 1,
										m_codeEditor->GetLineEndPosition(content.nLineEnd) + 1,
										wxEmptyString
									);
								}
								procFounded = true;
							}
						}
						else {
							procFounded = true;
						}
					}
					else {
						const wxString& oldValue = event->GetValue();
						if (!oldValue.ToInt(&action)) {
							if (StringUtils::CompareString(content.sName, event->GetValue())
								&& !StringUtils::CompareString(content.sName, newValue) && !newValue.IsEmpty()) {
								int answer = wxMessageBox(_("Do you rename an existing procedure?"), _("Rename"), wxYES_NO | wxCENTRE);
								if (answer == wxYES) {
									wxString currentLine = m_codeEditor->GetLine(content.nLineStart);
									currentLine.Replace(event->GetValue(), newValue);
									m_codeEditor->Replace(m_codeEditor->GetLineEndPosition(content.nLineStart - 1) + 1, m_codeEditor->GetLineEndPosition(content.nLineStart) + 1, currentLine);
									procFounded = true; lineStart = content.nLineStart; lineEnd = content.nLineEnd;
								}
								break;
							}
							else if (StringUtils::CompareString(content.sName, newValue)) {
								procFounded = true; lineStart = content.nLineStart; lineEnd = content.nLineEnd;
								break;
							}
							else if (StringUtils::CompareString(content.sName, event->GetValue())
								&& newValue.IsEmpty()) {
								int answer = wxMessageBox(_("Do you delete an existing procedure?"), _("Delete"), wxYES_NO | wxCENTRE);
								if (answer == wxYES) {
									m_codeEditor->Replace(
										m_codeEditor->GetLineEndPosition(content.nLineStart - 1) + 1,
										m_codeEditor->GetLineEndPosition(content.nLineEnd + 1),
										wxEmptyString
									);
								}
								procFounded = true;
							}
						}
					}
				}
			}
		}
	}

	bool changeSel = true; 

	if (event->GetType() == ET_ACTION) {
		action_identifier_t action = wxNOT_FOUND;
		changeSel = !newValue.ToInt(&action);
	}

	if (changeSel) {	
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		if (!procFounded) {
			int endPos = m_codeEditor->GetLineEndPosition(lineEnd);
			wxString offset = endPos > 0 ?
				"\r\n\r\n" : "";
			m_codeEditor->Replace(endPos, endPos,
				offset +
				"procedure " + newValue + "(" + prcArgs + ")\r\n"
				"\t\r\n"
				"endProcedure"
			);
			int patchLine = endPos > 0 ?
				2 : -1;
			lineStart = lineEnd + patchLine;
		}
		m_codeEditor->GotoLine(lineStart);
		m_codeEditor->SetSTCFocus(true);
	}

	m_visualEditor->ModifyEvent(event, newValue);
}

#include "frontend/mainFrame.h"
#include "metadata/metaObjects/metaObject.h"

void CVisualEditorNotebook::OnPageChanged(wxAuiNotebookEvent& event) {
	if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {
		objectInspector->SelectObject(m_visualEditor->GetSelectedObject());
	}
	else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
		if (m_visualEditor->m_document != NULL) {
			IMetaObject* moduleObject = m_visualEditor->m_document->GetMetaObject();
			if (moduleObject != NULL)
				objectInspector->SelectObject(moduleObject);
		}
		m_codeEditor->SetSTCFocus(true);
	}
	if (m_visualEditor->m_document != NULL)
		m_visualEditor->m_document->Activate();
	event.Skip();
}