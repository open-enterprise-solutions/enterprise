#include "debugClient.h"
#include "core/frontend/docView/docManager.h"

#include "frontend/mainFrame.h"

#include "frontend/stack/stackWindow.h"
#include "frontend/watch/watchwindow.h"
#include "frontend/output/outputWindow.h"

#include "frontend/codeEditor/codeEditorCtrl.h"
#include "frontend/metatree/metaTreeWnd.h"

#include "utils/stringUtils.h"

void CDebuggerClient::OnEnterLoop(const debugData_t& debugData)
{
	if (mainFrame->IsFocusable()) {
		mainFrame->Iconize(false); // restore the window if minimized
		mainFrame->SetFocus();     // focus on my window
		mainFrame->Raise();        // bring window to front
	}

	if (debugData.m_fileName.IsEmpty()) {
		IMetadataWrapperTree* metaTree = metadata->GetMetaTree();
		wxASSERT(metaTree);
		metaTree->EditModule(debugData.m_moduleName, debugData.m_line, false);
	}
	else if (!debugData.m_fileName.IsEmpty()) {
		IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(
			docManager->FindDocumentByPath(debugData.m_fileName)
			);
		if (foundedDoc == NULL) {
			foundedDoc = dynamic_cast<IMetaDataDocument*>(
				docManager->CreateDocument(debugData.m_fileName, wxDOC_SILENT)
				);
		}
		if (foundedDoc != NULL) {
			IMetadata* metaData = foundedDoc->GetMetadata();
			wxASSERT(metaData);
			IMetadataWrapperTree* metaTree = metaData->GetMetaTree();
			wxASSERT(metaTree);
			metaTree->EditModule(debugData.m_moduleName, debugData.m_line, true);
		}
	}
}

void CDebuggerClient::OnLeaveLoop(const debugData_t& debugData)
{
	if (debugData.m_fileName.IsEmpty()) {
		IMetadataWrapperTree* metaTree = metadata->GetMetaTree();
		if (metaTree != NULL) {
			IMetaObject* foundedMeta = metadata->FindByName(debugData.m_moduleName);
			if (foundedMeta != NULL) {
				wxDocument* foundedDoc = metaTree->GetDocument(foundedMeta);
				if (foundedDoc != NULL) {
					foundedDoc->UpdateAllViews();
				}
			}
		}
	}
	else if (!debugData.m_fileName.IsEmpty()) {
		wxDocument* foundedDoc = docManager->FindDocumentByPath(debugData.m_fileName);
		if (foundedDoc != NULL) {
			foundedDoc->UpdateAllViews();
		}
	}
}

void CDebuggerClient::OnFillAutoComplete(const debugAutoCompleteData_t& debugData)
{
	if (debugData.m_fileName.IsEmpty()) {
		IMetadataWrapperTree* metaTree = metadata->GetMetaTree();
		if (metaTree != NULL) {
			IMetaObject* foundedMeta = metadata->FindByName(debugData.m_moduleName);
			if (foundedMeta != NULL) {
				IModuleDocument* foundedDoc = dynamic_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
				if (foundedDoc != NULL) {
					CCodeEditorCtrl* codeEditor = foundedDoc->GetCodeEditor();
					wxASSERT(codeEditor);
					codeEditor->ShowAutoComp(debugData);
				}
			}
		}
	}
	else if (!debugData.m_fileName.IsEmpty()) {
		IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(docManager->FindDocumentByPath(debugData.m_fileName));
		if (foundedDoc != NULL) {
			IMetadata* metaData = foundedDoc->GetMetadata();
			wxASSERT(metaData);
			IMetadataWrapperTree* metaTree = metaData->GetMetaTree();
			if (metaTree != NULL) {
				IMetaObject* foundedMeta = metaData->FindByName(debugData.m_moduleName);
				if (foundedMeta != NULL) {
					IModuleDocument* foundedDoc = dynamic_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
					if (foundedDoc != NULL) {
						CCodeEditorCtrl* codeEditor = foundedDoc->GetCodeEditor();
						wxASSERT(codeEditor);
						codeEditor->ShowAutoComp(debugData);
					}
				}
			}
		}
	}
}

void CDebuggerClient::OnMessageFromEnterprise(const debugData_t& debugData, const wxString& message)
{
	if (mainFrame->IsFocusable()) {
		mainFrame->Iconize(false); // restore the window if minimized
		mainFrame->SetFocus();     // focus on my window
		mainFrame->Raise();        // bring window to front
	}

	if (debugData.m_fileName.IsEmpty()) {
		IMetadataWrapperTree* metaTree = metadata->GetMetaTree();
		wxASSERT(metaTree);
		metaTree->EditModule(debugData.m_moduleName, debugData.m_line, false);
	}
	if (!debugData.m_fileName.IsEmpty()) {
		IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(
			docManager->FindDocumentByPath(debugData.m_fileName)
			);
		if (foundedDoc == NULL) {
			foundedDoc = dynamic_cast<IMetaDataDocument*>(
				docManager->CreateDocument(debugData.m_fileName, wxDOC_SILENT)
				);
		}
		if (foundedDoc != NULL) {
			IMetadata* metaData = foundedDoc->GetMetadata();
			wxASSERT(metaData);
			IMetadataWrapperTree* metaTree = metaData->GetMetaTree();
			wxASSERT(metaTree);
			metaTree->EditModule(debugData.m_moduleName, debugData.m_line, false);
		}
	}

	outputWindow->OutputError(message,
		debugData.m_fileName, debugData.m_moduleName, debugData.m_line);
}

void CDebuggerClient::OnSetToolTip(const debugTipData_t& debugData, const wxString& resultStr)
{
	if (debugData.m_fileName.IsEmpty()) {
		IMetadataWrapperTree* metaTree = metadata->GetMetaTree();
		if (metaTree != NULL) {
			IMetaObject* foundedMeta = metadata->FindByName(debugData.m_moduleName);
			if (foundedMeta != NULL) {
				IModuleDocument* foundedDoc =
					dynamic_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
				if (foundedDoc != NULL) {
					foundedDoc->SetToolTip(resultStr);
				}
			}
		}
	}
	if (!debugData.m_fileName.IsEmpty()) {
		IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(
			docManager->FindDocumentByPath(debugData.m_fileName)
			);
		if (foundedDoc != NULL) {
			IMetadata* metaData = foundedDoc->GetMetadata();
			wxASSERT(metaData);
			IMetadataWrapperTree* metaTree = metaData->GetMetaTree();
			if (metaTree != NULL) {
				IMetaObject* foundedMeta = metaData->FindByName(debugData.m_moduleName);
				if (foundedMeta != NULL) {
					IModuleDocument* foundedDoc =
						dynamic_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
					if (foundedDoc != NULL) {
						foundedDoc->SetToolTip(resultStr);
					}
				}
			}
		}
	}
}
