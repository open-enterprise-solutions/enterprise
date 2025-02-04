#include "debugClientImpl.h"
#include "docManager/docManager.h"

void CDebuggerClientBridge::OnSessionStart(wxSocketClient* sock)
{
	if (docManager != nullptr) {
		for (auto& doc : docManager->GetDocumentsVector()) {
			CMetaDocument* metaDoc = dynamic_cast<CMetaDocument*>(doc);
			if (metaDoc != nullptr) {
				IModuleDocument* foundedDoc = dynamic_cast<IModuleDocument*>(metaDoc);
				if (foundedDoc != nullptr) {
					foundedDoc->SetCurrentLine(wxNOT_FOUND, false);
					foundedDoc->SetToolTip(wxEmptyString);
				}
				for (auto& child_doc : metaDoc->GetChild()) {
					IModuleDocument* foundedDoc = dynamic_cast<IModuleDocument*>(child_doc);
					if (foundedDoc != nullptr) {
						foundedDoc->SetCurrentLine(wxNOT_FOUND, false);
						foundedDoc->SetToolTip(wxEmptyString);
					}
				}
			}
		}
	}

	mainFrame->Debugger_OnSessionStart();
	mainFrame->RefreshFrame();
}

void CDebuggerClientBridge::OnSessionEnd(wxSocketClient* sock)
{
	if (docManager != nullptr) {
		for (auto& doc : docManager->GetDocumentsVector()) {
			CMetaDocument* metaDoc = dynamic_cast<CMetaDocument*>(doc);
			if (metaDoc != nullptr) {
				IModuleDocument* foundedDoc = dynamic_cast<IModuleDocument*>(metaDoc);
				if (foundedDoc != nullptr) {
					foundedDoc->SetCurrentLine(wxNOT_FOUND, false);
					foundedDoc->SetToolTip(wxEmptyString);
				}
				for (auto& child_doc : metaDoc->GetChild()) {
					IModuleDocument* foundedDoc = dynamic_cast<IModuleDocument*>(child_doc);
					if (foundedDoc != nullptr) {
						foundedDoc->SetCurrentLine(wxNOT_FOUND, false);
						foundedDoc->SetToolTip(wxEmptyString);
					}
				}
			}
		}
	}

	localWindow->ClearAndCreate();
	stackWindow->ClearAndCreate();

	mainFrame->Debugger_OnSessionEnd();
	mainFrame->RefreshFrame();
}

void CDebuggerClientBridge::OnEnterLoop(wxSocketClient* sock, const debugLineData_t& data)
{
	mainFrame->RaiseFrame();

	if (docManager != nullptr) {
		if (data.m_fileName.IsEmpty()) {
			IBackendMetadataTree* metaTree = commonMetaData->GetMetaTree();
			wxASSERT(metaTree);
			metaTree->EditModule(data.m_moduleName, data.m_line, true);
		}
		else if (!data.m_fileName.IsEmpty()) {
			IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(
				docManager->FindDocumentByPath(data.m_fileName)
				);
			if (foundedDoc == nullptr) {
				foundedDoc = dynamic_cast<IMetaDataDocument*>(
					docManager->CreateDocument(data.m_fileName, wxDOC_SILENT)
					);
			}
			if (foundedDoc != nullptr) {
				IMetaData* metaData = foundedDoc->GetMetaData();
				wxASSERT(metaData);
				IBackendMetadataTree* metaTree = metaData->GetMetaTree();
				wxASSERT(metaTree);
				metaTree->EditModule(data.m_moduleName, data.m_line, true);
			}
		}
	}

	mainFrame->Debugger_OnEnterLoop();
	mainFrame->RefreshFrame();
}

void CDebuggerClientBridge::OnLeaveLoop(wxSocketClient* sock, const debugLineData_t& data)
{
	if (docManager != nullptr) {
		if (data.m_fileName.IsEmpty()) {
			IBackendMetadataTree* metaTree = commonMetaData->GetMetaTree();
			if (metaTree != nullptr) {
				IMetaObject* foundedMeta = commonMetaData->FindByName(data.m_moduleName);
				if (foundedMeta != nullptr) {
					IModuleDocument* foundedDoc = dynamic_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
					if (foundedDoc != nullptr) {
						foundedDoc->SetCurrentLine(data.m_line, false);
						foundedDoc->SetToolTip(wxEmptyString);
					}
				}
			}
		}
		else if (!data.m_fileName.IsEmpty()) {
			IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(docManager->FindDocumentByPath(data.m_fileName));
			if (foundedDoc != nullptr) {
				IMetaData* foundedMetadata = foundedDoc->GetMetaData();
				wxASSERT(foundedMetadata);
				IBackendMetadataTree* metaTree = foundedMetadata->GetMetaTree();
				if (metaTree != nullptr) {
					IMetaObject* foundedMeta = foundedMetadata->FindByName(data.m_moduleName);
					if (foundedMeta != nullptr) {
						IModuleDocument* foundedDoc = dynamic_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
						if (foundedDoc != nullptr) {
							foundedDoc->SetCurrentLine(data.m_line, false);
							foundedDoc->SetToolTip(wxEmptyString);
						}
					}
				}
			}
		}
	}

	localWindow->ClearAndCreate();
	stackWindow->ClearAndCreate();

	mainFrame->Debugger_OnLeaveLoop();
	mainFrame->RefreshFrame();
}

void CDebuggerClientBridge::OnAutoComplete(const debugAutoCompleteData_t& data)
{
	if (docManager != nullptr) {
		if (data.m_fileName.IsEmpty()) {
			IBackendMetadataTree* metaTree = commonMetaData->GetMetaTree();
			if (metaTree != nullptr) {
				IMetaObject* foundedMeta = commonMetaData->FindByName(data.m_moduleName);
				if (foundedMeta != nullptr) {
					IModuleDocument* foundedDoc = static_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
					if (foundedDoc != nullptr) {
						foundedDoc->ShowAutoComplete(data);
					}
				}
			}
		}
		else if (!data.m_fileName.IsEmpty()) {
			IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(docManager->FindDocumentByPath(data.m_fileName));
			if (foundedDoc != nullptr) {
				IMetaData* metaData = foundedDoc->GetMetaData();
				wxASSERT(metaData);
				IBackendMetadataTree* metaTree = metaData->GetMetaTree();
				if (metaTree != nullptr) {
					IMetaObject* foundedMeta = metaData->FindByName(data.m_moduleName);
					if (foundedMeta != nullptr) {
						IModuleDocument* foundedDoc = static_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
						if (foundedDoc != nullptr) {
							foundedDoc->ShowAutoComplete(data);
						}
					}
				}
			}
		}
	}
}

void CDebuggerClientBridge::OnMessageFromServer(const debugLineData_t& data, const wxString& message)
{
	mainFrame->RaiseFrame();

	if (docManager != nullptr) {
		if (data.m_fileName.IsEmpty()) {
			IBackendMetadataTree* metaTree = commonMetaData->GetMetaTree();
			wxASSERT(metaTree);
			metaTree->EditModule(data.m_moduleName, data.m_line, false);
		}
		if (!data.m_fileName.IsEmpty()) {
			IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(
				docManager->FindDocumentByPath(data.m_fileName)
				);
			if (foundedDoc == nullptr) {
				foundedDoc = dynamic_cast<IMetaDataDocument*>(
					docManager->CreateDocument(data.m_fileName, wxDOC_SILENT)
					);
			}
			if (foundedDoc != nullptr) {
				IMetaData* metaData = foundedDoc->GetMetaData();
				wxASSERT(metaData);
				IBackendMetadataTree* metaTree = metaData->GetMetaTree();
				wxASSERT(metaTree);
				metaTree->EditModule(data.m_moduleName, data.m_line, false);
			}
		}
	}

	outputWindow->OutputError(message,
		data.m_fileName, data.m_moduleName, data.m_line);
}

void CDebuggerClientBridge::OnSetToolTip(const debugExpressionData_t& data, const wxString& resultStr)
{
	if (docManager != nullptr) {
		if (data.m_fileName.IsEmpty()) {
			IBackendMetadataTree* metaTree = commonMetaData->GetMetaTree();
			if (metaTree != nullptr) {
				IMetaObject* foundedMeta = commonMetaData->FindByName(data.m_moduleName);
				if (foundedMeta != nullptr) {
					IModuleDocument* foundedDoc =
						static_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
					if (foundedDoc != nullptr) {
						foundedDoc->SetToolTip(resultStr);
					}
				}
			}
		}
		if (!data.m_fileName.IsEmpty()) {
			IMetaDataDocument* foundedDoc = dynamic_cast<IMetaDataDocument*>(
				docManager->FindDocumentByPath(data.m_fileName)
				);
			if (foundedDoc != nullptr) {
				IMetaData* metaData = foundedDoc->GetMetaData();
				wxASSERT(metaData);
				IBackendMetadataTree* metaTree = metaData->GetMetaTree();
				if (metaTree != nullptr) {
					IMetaObject* foundedMeta = metaData->FindByName(data.m_moduleName);
					if (foundedMeta != nullptr) {
						IModuleDocument* foundedDoc =
							static_cast<IModuleDocument*>(metaTree->GetDocument(foundedMeta));
						if (foundedDoc != nullptr) {
							foundedDoc->SetToolTip(resultStr);
						}
					}
				}
			}
		}
	}
}

void CDebuggerClientBridge::OnSetStack(const stackData_t& stackData)
{
	stackWindow->SetStack(stackData);
}

void CDebuggerClientBridge::OnSetLocalVariable(const localWindowData_t& data)
{
	localWindow->SetLocalVariable(data);
}

void CDebuggerClientBridge::OnSetVariable(const watchWindowData_t& watchData)
{
	watchWindow->SetVariable(watchData);
}

void CDebuggerClientBridge::OnSetExpanded(const watchWindowData_t& watchData)
{
	watchWindow->SetExpanded(watchData);
}
