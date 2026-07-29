////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

void ibFrontendMainFrame::CreatePropertyPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_PROPERTY).IsOk())
		return;

	m_objectInspector = new ibObjectInspector(this, wxID_ANY);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_PROPERTY);
	paneInfo.CloseButton(true);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.DestroyOnClose(false);
	paneInfo.Caption(_("Properties"));
	paneInfo.MinSize(300, 0);
	paneInfo.Right();
	paneInfo.Show(false);

	m_mgr.AddPane(m_objectInspector, paneInfo);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibFrontendMainFrame::IsShownInspector()
{
	const wxAuiPaneInfo propertyPane = m_mgr.GetPane(wxAUI_PANE_PROPERTY);
	if (!propertyPane.IsOk()) return false;
	return propertyPane.IsShown();
}

void ibFrontendMainFrame::ShowInspector()
{
	wxAuiPaneInfo& propertyPane = m_mgr.GetPane(wxAUI_PANE_PROPERTY);
	if (!propertyPane.IsOk())
		return;
	if (!propertyPane.IsShown()) {
		propertyPane.Show();
		m_objectInspector->SetFocus();
		m_objectInspector->Raise();
		m_mgr.Update();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#include "frontend/docView/docView.h"

void ibFrontendMainFrame::ActivateView(ibView* view, bool activate) {

	if (m_docToolbar != nullptr) {

		m_docToolbar->Freeze();

		if (activate) {

			wxFrame* viewFrame = dynamic_cast<wxFrame*>(view->GetFrame());
#if wxUSE_MENUS	
			if (viewFrame != nullptr) viewFrame->SetMenuBar(view->CreateMenuBar());
#endif
			if (viewFrame != nullptr) {
				m_docToolbar->Clear();
				view->OnCreateToolbar(m_docToolbar);
				m_docToolbar->Realize();
			}
		}
		else {

#if wxUSE_MENUS		

			wxFrame* viewFrame = dynamic_cast<wxFrame*>(view->GetFrame());

			if (viewFrame != nullptr) {

				class ibProcSubMenu {

					static void SetChildEnable(wxMenu* dst, bool enable = false) {

						for (const auto it : dst->GetMenuItems()) {
							if (!it->IsSubMenu()) it->Enable(enable);
							if (it->IsSubMenu()) SetChildEnable(it->GetSubMenu(), enable);
						}
					}

				public:

					static void SetMenuEnabled(wxMenuBar* menuBar, bool enable = false) {
						for (size_t idx = 0; idx < menuBar->GetMenuCount(); idx++) {
							ibProcSubMenu::SetChildEnable(menuBar->GetMenu(idx), enable);
						}
					}
				};

				wxMenuBar* menuBar = view->CreateMenuBar();
				if (menuBar != nullptr)
					ibProcSubMenu::SetMenuEnabled(menuBar);
				
				viewFrame->SetMenuBar(menuBar);
			}
#endif
			for (size_t idx = 0; idx < m_docToolbar->GetToolCount(); idx++) {
				m_docToolbar->EnableTool(m_docToolbar->FindToolByIndex(idx)->GetId(), false);
			}
		}

		m_docToolbar->Thaw();

		// update frame manager 
		UpdateManager();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/metadataConfiguration.h"

bool ibFrontendMainFrame::EnsureRuntime()
{
	// The session comes from the holder we were built with — no separate
	// bind step, so there is no window in which the frame exists without
	// knowing its session.
	ibSession* session = GetSession();
	if (session == nullptr || activeMetaData == nullptr)
		return false;

	// Re-entry guard — root module manager lives on the session; if it's
	// already installed the runtime was started on a previous Show().
	if (session->GetManagerModule() != nullptr)
		return true;

	const ibSessionKind kind = session->GetKind();
	const bool wantsRuntime =
		(kind == ibSessionKind::Enterprise) ||
		(kind == ibSessionKind::WebClient)  ||
		(kind == ibSessionKind::Service);
	if (!wantsRuntime)
		return true;

	// CreateRoot + CompileRoot already happened during authentication
	// (registry's NotifyAuthenticated chain) — only the runtime attach is
	// left, and it waits until Show() so activeMetaData is populated.
	if (auto* mm = session->GetManagerModule())
		mm->AttachRuntime(session);
	return true;
}

ibMetaData* ibFrontendMainFrame::FindMetadataByPath(const wxString& strFileName) const
{
	ibMetaDataDocument* const foundedDoc = dynamic_cast<ibMetaDataDocument*>(docManager->FindDocumentByPath(strFileName));
	if (foundedDoc != nullptr)
		return foundedDoc->GetMetaData();
	return nullptr;
}

#pragma region _frontend_call_h__

#include "frontend/visualView/visualHostClient.h"

// Form support
ibBackendValueForm* ibFrontendMainFrame::ActiveWindow() const {

	if (ibFrontendMainFrame::GetFrame() != nullptr) {
		ibDocChildFrameAnyBase* activeChild =
			dynamic_cast<ibDocChildFrameAnyBase*>(ibFrontendMainFrame::GetActiveChild());
		if (activeChild != nullptr) {
			ibFormVisualDocument* const ownerFormDoc = dynamic_cast<ibFormVisualDocument*>(activeChild->GetDocument());
			if (ownerFormDoc != nullptr) {
				return ownerFormDoc->GetValueForm();
			}
		}
	}

	return nullptr;
}

ibBackendValueForm* ibFrontendMainFrame::CreateNewForm(const ibValueMetaObjectFormBase* creator, ibBackendControlFrame* backendControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid)
{
	ibControlFrame* ownerControl = dynamic_cast<ibControlFrame*>(backendControl);
	wxASSERT(!(backendControl == nullptr && ownerControl != nullptr));
	// Parent descriptor wiring happens inside ibValueForm's ctor — it
	// already receives ownerControl; for the UI path (null owner) it
	// falls back to backend_mainFrame->GetSession()->GetManagerModule().
	return new ibValueForm(creator, ownerControl, srcObject, formGuid);
}

ibUniqueKey ibFrontendMainFrame::CreateFormUniqueKey(const ibBackendControlFrame* ownerControl, const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid)
{
	return ibFormVisualDocument::CreateFormUniqueKey(ownerControl, sourceObject, formGuid);
}

ibBackendValueForm* ibFrontendMainFrame::FindFormByUniqueKey(const ibBackendControlFrame* ownerControl, const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid)
{
	return ibFormVisualDocument::FindFormByUniqueKey(ownerControl, sourceObject, formGuid);
}

ibBackendValueForm* ibFrontendMainFrame::FindFormByUniqueKey(const ibUniqueKey& guid)
{
	return ibFormVisualDocument::FindFormByUniqueKey(guid);
}

ibBackendValueForm* ibFrontendMainFrame::FindFormByControlUniqueKey(const ibUniqueKey& guid)
{
	return ibFormVisualDocument::FindFormByControlUniqueKey(guid);
}

ibBackendValueForm* ibFrontendMainFrame::FindFormBySourceUniqueKey(const ibUniqueKey& guid)
{
	return ibFormVisualDocument::FindFormBySourceUniqueKey(guid);
}

bool ibFrontendMainFrame::UpdateFormUniqueKey(const ibUniqueKeyPair& guid)
{
	return ibFormVisualDocument::UpdateFormUniqueKey(guid);
}

#include "frontend/docView/templates/docViewSpreadsheet.h"

// Grid support
bool ibFrontendMainFrame::ShowSpreadsheetDocument(const wxString& strTitle, wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadSheetDocument)
{
	class ibSpreadsheetMemoryDocument :
		public ibSpreadsheetFileDocument {
	public:

		ibSpreadsheetMemoryDocument(const wxString& strTitle, const wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadSheetDocument) :
			ibSpreadsheetFileDocument(spreadSheetDocument)
		{
			ibSpreadsheetFileDocument::SetTitle(strTitle);
			ibSpreadsheetFileDocument::SetFilename(strTitle);
		}

		virtual bool OnCreate(const wxString& path, long flags) override {

			if (!ibMetaDocument::OnCreate(path, flags))
				return false;

			return GetGridCtrl()->LoadDocument(m_spreadSheetDocument);
		}
	};

	ibSpreadsheetMemoryDocument* createdDoc =
		docManager->CreateDocument<ibSpreadsheetMemoryDocument>(strTitle, spreadSheetDocument);

	wxASSERT(createdDoc != nullptr);

	if (createdDoc->OnCreate(wxEmptyString, 0)) {
		createdDoc->SetCommandProcessor(createdDoc->OnCreateCommandProcessor());
		docManager->AddDocument(createdDoc);
		return true;
	}

	// The document may be already destroyed, this happens if its view
	// creation fails as then the view being created is destroyed
	// triggering the destruction of the document as this first view is
	// also the last one. However if OnCreate() fails for any reason other
	// than view creation failure, the document is still alive and we need
	// to clean it up ourselves to avoid having a zombie document.

	createdDoc->DeleteAllViews();
	return false;
}

#include "frontend/win/editor/gridEditor/gridPrintout.h"

bool ibFrontendMainFrame::PrintSpreadsheetDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, bool showPrintDlg)
{
	wxScopedPtr<ibGridEditorPrintout> printout(new ibGridEditorPrintout(doc));

	const wxPageSetupDialogData& pageSetupDialogData =
		docManager->GetPageSetupDialogData();

	wxPrintDialogData printDialogData(pageSetupDialogData.GetPrintData());
	wxPrinter printer(&printDialogData);

	return printer.Print(this, printout.get(), true);
}

#pragma endregion 

ibPropertyObject* ibFrontendMainFrame::GetProperty() const
{
	if (m_objectInspector != nullptr)
		return m_objectInspector->GetSelectedObject();

	return nullptr;
}

bool ibFrontendMainFrame::SetProperty(ibPropertyObject* prop)
{
	if (m_objectInspector != nullptr) {
		m_objectInspector->SelectObject(prop);
		return true;
	}

	return false;
}
