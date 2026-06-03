#include "debugClientImpl.h"
#include "docManager/docManager.h"

void ibDebuggerClientBridgeDesigner::OnSessionStart(wxSocketClient* sock)
{
	if (docManager != nullptr) {
		for (auto& doc : docManager->GetDocumentsVector()) {
			ibMetaDocument* metaDoc = dynamic_cast<ibMetaDocument*>(doc);
			if (metaDoc != nullptr) {
				ibValueModuleDocument* foundedDoc = dynamic_cast<ibValueModuleDocument*>(metaDoc);
				if (foundedDoc != nullptr) {
					foundedDoc->SetCurrentLine(wxNOT_FOUND, false);
					foundedDoc->SetToolTip(wxEmptyString);
				}
				for (auto& child_doc : metaDoc->GetChild()) {
					ibValueModuleDocument* foundedDoc = dynamic_cast<ibValueModuleDocument*>(child_doc);
					if (foundedDoc != nullptr) {
						foundedDoc->SetCurrentLine(wxNOT_FOUND, false);
						foundedDoc->SetToolTip(wxEmptyString);
					}
				}
			}
		}
	}

	if (mainFrame != nullptr) mainFrame->Debugger_OnSessionStart();
}

void ibDebuggerClientBridgeDesigner::OnSessionEnd(wxSocketClient* sock)
{
	if (docManager != nullptr) {
		for (auto& doc : docManager->GetDocumentsVector()) {
			ibMetaDocument* metaDoc = dynamic_cast<ibMetaDocument*>(doc);
			if (metaDoc != nullptr) {
				ibValueModuleDocument* foundedDoc = dynamic_cast<ibValueModuleDocument*>(metaDoc);
				if (foundedDoc != nullptr) {
					foundedDoc->SetCurrentLine(wxNOT_FOUND, false);
					foundedDoc->SetToolTip(wxEmptyString);
				}
				for (auto& child_doc : metaDoc->GetChild()) {
					ibValueModuleDocument* foundedDoc = dynamic_cast<ibValueModuleDocument*>(child_doc);
					if (foundedDoc != nullptr) {
						foundedDoc->SetCurrentLine(wxNOT_FOUND, false);
						foundedDoc->SetToolTip(wxEmptyString);
					}
				}
			}
		}
	}

	if (localWindow != nullptr) localWindow->ClearAndCreate();
	if (stackWindow != nullptr) stackWindow->ClearAndCreate();

	if (mainFrame != nullptr) mainFrame->Debugger_OnSessionEnd();
}

void ibDebuggerClientBridgeDesigner::OnEnterLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	if (mainFrame != nullptr) mainFrame->RaiseFrame();

	if (docManager != nullptr) {
		if (data.m_fileName.IsEmpty()) {
			ibBackendMetadataTree* metaTree = activeMetaData->GetMetaTree();
			wxASSERT(metaTree);
			metaTree->EditModule(data.m_moduleName, data.m_line, true);
		}
		else if (!data.m_fileName.IsEmpty()) {
			ibMetaDataDocument* foundedDoc = dynamic_cast<ibMetaDataDocument*>(
				docManager->FindDocumentByPath(data.m_fileName)
				);
			if (foundedDoc == nullptr) {
				foundedDoc = dynamic_cast<ibMetaDataDocument*>(
					docManager->CreateDocument(data.m_fileName, ibDOC_SILENT)
					);
			}
			if (foundedDoc != nullptr) {
				ibMetaData* metaData = foundedDoc->GetMetaData();
				wxASSERT(metaData);
				ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
				wxASSERT(metaTree);
				metaTree->EditModule(data.m_moduleName, data.m_line, true);
			}
		}
	}

	if (mainFrame != nullptr) mainFrame->Debugger_OnEnterLoop();
}

void ibDebuggerClientBridgeDesigner::OnLeaveLoop(wxSocketClient* sock, const ibDebugLineData& data)
{
	if (docManager != nullptr) {
		const ibGuid& moduleName = data.m_moduleName;
		if (data.m_fileName.IsEmpty()) {
			const ibBackendMetadataTree* metaTree = activeMetaData->GetMetaTree();
			if (metaTree != nullptr) {
				ibValueMetaObject* foundedMeta = activeMetaData->FindAnyObjectByFilter(moduleName, true);
				if (foundedMeta != nullptr) {
					ibValueModuleDocument* foundedDoc = dynamic_cast<ibValueModuleDocument*>(metaTree->GetDocument(foundedMeta));
					if (foundedDoc != nullptr) {
						foundedDoc->SetCurrentLine(data.m_line, false);
						foundedDoc->SetToolTip(wxEmptyString);
					}
				}
			}
		}
		else if (!data.m_fileName.IsEmpty()) {
			ibMetaDataDocument* foundedDoc = dynamic_cast<ibMetaDataDocument*>(docManager->FindDocumentByPath(data.m_fileName));
			if (foundedDoc != nullptr) {
				ibMetaData* foundedMetadata = foundedDoc->GetMetaData();
				wxASSERT(foundedMetadata);
				const ibBackendMetadataTree* metaTree = foundedMetadata->GetMetaTree();
				if (metaTree != nullptr) {
					ibValueMetaObject* foundedMeta = foundedMetadata->FindAnyObjectByFilter(moduleName, true);
					if (foundedMeta != nullptr) {
						ibValueModuleDocument* foundedDoc = dynamic_cast<ibValueModuleDocument*>(metaTree->GetDocument(foundedMeta));
						if (foundedDoc != nullptr) {
							foundedDoc->SetCurrentLine(data.m_line, false);
							foundedDoc->SetToolTip(wxEmptyString);
						}
					}
				}
			}
		}
	}

	// Do not clear the locals/stack windows here. The next OnEnterLoop
	// will deliver fresh data via SetStack/SetLocalVariable, which update
	// the tables incrementally. Clearing here caused a visible blank-out
	// of the tables between every debugger step.

	if (mainFrame != nullptr) mainFrame->Debugger_OnLeaveLoop();
}

void ibDebuggerClientBridgeDesigner::OnAutoComplete(const ibDebugAutoCompleteData& data)
{
	if (docManager != nullptr) {
		const ibGuid& moduleName = data.m_moduleName;
		if (data.m_fileName.IsEmpty()) {
			const ibBackendMetadataTree* metaTree = activeMetaData->GetMetaTree();
			if (metaTree != nullptr) {
				ibValueMetaObject* foundedMeta = activeMetaData->FindAnyObjectByFilter(moduleName, true);
				if (foundedMeta != nullptr) {
					ibValueModuleDocument* foundedDoc = static_cast<ibValueModuleDocument*>(metaTree->GetDocument(foundedMeta));
					if (foundedDoc != nullptr) {
						foundedDoc->ShowAutoComplete(data);
					}
				}
			}
		}
		else if (!data.m_fileName.IsEmpty()) {
			const ibMetaDataDocument* foundedDoc = dynamic_cast<ibMetaDataDocument*>(docManager->FindDocumentByPath(data.m_fileName));
			if (foundedDoc != nullptr) {
				ibMetaData* metaData = foundedDoc->GetMetaData();
				wxASSERT(metaData);
				const ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
				if (metaTree != nullptr) {
					ibValueMetaObject* foundedMeta = metaData->FindAnyObjectByFilter(moduleName, true);
					if (foundedMeta != nullptr) {
						ibValueModuleDocument* foundedDoc = static_cast<ibValueModuleDocument*>(metaTree->GetDocument(foundedMeta));
						if (foundedDoc != nullptr) {
							foundedDoc->ShowAutoComplete(data);
						}
					}
				}
			}
		}
	}
}

void ibDebuggerClientBridgeDesigner::OnMessageFromServer(const ibDebugLineData& data, const wxString& message)
{
	if (mainFrame != nullptr) mainFrame->RaiseFrame();

	if (docManager != nullptr) {
		if (data.m_fileName.IsEmpty()) {
			ibBackendMetadataTree* metaTree = activeMetaData->GetMetaTree();
			wxASSERT(metaTree);
			metaTree->EditModule(data.m_moduleName, data.m_line, false);
		}
		if (!data.m_fileName.IsEmpty()) {
			ibMetaDataDocument* foundedDoc = dynamic_cast<ibMetaDataDocument*>(
				docManager->FindDocumentByPath(data.m_fileName)
				);
			if (foundedDoc == nullptr) {
				foundedDoc = dynamic_cast<ibMetaDataDocument*>(
					docManager->CreateDocument(data.m_fileName, ibDOC_SILENT)
					);
			}
			if (foundedDoc != nullptr) {
				ibMetaData* metaData = foundedDoc->GetMetaData();
				wxASSERT(metaData);
				ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
				wxASSERT(metaTree);
				metaTree->EditModule(data.m_moduleName, data.m_line, false);
			}
		}
	}

	outputWindow->OutputError(message,
		data.m_fileName, data.m_moduleName, data.m_line);
}

void ibDebuggerClientBridgeDesigner::OnSetToolTip(const ibDebugExpressionData& data, const wxString& resultStr)
{
	if (docManager != nullptr) {
		const ibGuid& moduleName = data.m_moduleName;
		if (data.m_fileName.IsEmpty()) {
			const ibBackendMetadataTree* metaTree = activeMetaData->GetMetaTree();
			if (metaTree != nullptr) {
				ibValueMetaObject* foundedMeta = activeMetaData->FindAnyObjectByFilter(moduleName, true);
				if (foundedMeta != nullptr) {
					ibValueModuleDocument* foundedDoc =
						static_cast<ibValueModuleDocument*>(metaTree->GetDocument(foundedMeta));
					if (foundedDoc != nullptr) {
						foundedDoc->SetToolTip(resultStr);
					}
				}
			}
		}
		if (!data.m_fileName.IsEmpty()) {
			ibMetaDataDocument* foundedDoc = dynamic_cast<ibMetaDataDocument*>(
				docManager->FindDocumentByPath(data.m_fileName)
				);
			if (foundedDoc != nullptr) {
				ibMetaData* metaData = foundedDoc->GetMetaData();
				wxASSERT(metaData);
				const ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
				if (metaTree != nullptr) {
					ibValueMetaObject* foundedMeta = metaData->FindAnyObjectByFilter(moduleName, true);
					if (foundedMeta != nullptr) {
						ibValueModuleDocument* foundedDoc =
							static_cast<ibValueModuleDocument*>(metaTree->GetDocument(foundedMeta));
						if (foundedDoc != nullptr) {
							foundedDoc->SetToolTip(resultStr);
						}
					}
				}
			}
		}
	}
}

void ibDebuggerClientBridgeDesigner::OnSetStack(const ibStackData& stackData)
{
	stackWindow->SetStack(stackData);
}

void ibDebuggerClientBridgeDesigner::OnSetLocalVariable(const ibLocalWindowData& data)
{
	localWindow->SetLocalVariable(data);
}

void ibDebuggerClientBridgeDesigner::OnSetVariable(const ibWatchWindowData& watchData)
{
	watchWindow->SetVariable(watchData);
}

void ibDebuggerClientBridgeDesigner::OnSetExpanded(const ibWatchWindowData& watchData)
{
	watchWindow->SetExpanded(watchData);
}
