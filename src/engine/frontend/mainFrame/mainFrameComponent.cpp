////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

void CDocMDIFrame::CreatePropertyPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_PROPERTY).IsOk())
		return;

	m_objectInspector = new CObjectInspector(this, wxID_ANY);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_PROPERTY);
	paneInfo.CloseButton(true);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.DestroyOnClose(false);
	paneInfo.Caption(_("Property"));
	paneInfo.MinSize(300, 0);
	paneInfo.Right();
	paneInfo.Show(false);

	m_mgr.AddPane(m_objectInspector, paneInfo);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void CDocMDIFrame::ShowProperty()
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

#include "frontend/docView/docManager.h"
#include "frontend/win/dlgs/authorization.h"
#include "frontend/visualView/ctrl/form.h"

#include "backend/appData.h"

bool CDocMDIFrame::AuthenticationUser(const wxString& userName, const wxString& userPassword) const
{
	if (appData == nullptr)
		return false;
	CDialogAuthentication* autorization = new CDialogAuthentication();
	autorization->SetLogin(userName);
	autorization->SetPassword(userPassword);
	const int result = autorization->ShowModal();
	autorization->Destroy();
	if (result == wxID_CANCEL) return false;
	return appData->AuthenticationAndSetUser(autorization->GetLogin(), autorization->GetPassword());
}

IMetaData* CDocMDIFrame::FindMetadataByPath(const wxString& strFileName) const
{
	IMetaDataDocument* const foundedDoc = dynamic_cast<IMetaDataDocument*>(docManager->FindDocumentByPath(strFileName));
	if (foundedDoc != nullptr) return foundedDoc->GetMetaData();
	return nullptr;
}

IBackendValueForm* CDocMDIFrame::ActiveWindow() const {

	if (CDocMDIFrame::GetFrame()) {
		wxDocChildFrameAnyBase* activeChild =
			dynamic_cast<wxDocChildFrameAnyBase*>(CDocMDIFrame::GetActiveChild());
		if (activeChild != nullptr) {
			CVisualView* formView = dynamic_cast<CVisualView*>(activeChild->GetView());
			if (formView != nullptr) {
				return formView->GetValueForm();
			}
		}
	}
	return nullptr;
}

IBackendValueForm* CDocMDIFrame::CreateNewForm(IBackendControlFrame* ownerControl, IMetaObjectForm* metaForm, ISourceDataObject* ownerSrc, const CUniqueKey& formGuid, bool readOnly)
{
	IControlFrame* frameControl = dynamic_cast<IControlFrame*>(ownerControl);
	wxASSERT(!(frameControl == nullptr && ownerControl != nullptr));
	return CValue::CreateAndConvertObjectValueRef<CValueForm>(
		frameControl, metaForm, ownerSrc, formGuid, readOnly
	);
}

IBackendValueForm* CDocMDIFrame::FindFormByUniqueKey(const CUniqueKey& guid)
{
	return CValueForm::FindFormByUniqueKey(guid);
}

bool CDocMDIFrame::UpdateFormUniqueKey(const CUniquePairKey& guid)
{
	return CValueForm::UpdateFormUniqueKey(guid);
}

void CDocMDIFrame::OnActivateView(bool activate, wxView* activeView, wxView* deactiveView) {

	if (m_docToolbar != nullptr) {

		if (deactiveView != nullptr) {
			CMetaView* deactView = dynamic_cast<CMetaView*>(deactiveView);
			if (deactView != nullptr) {
				deactView->OnRemoveToolbar(m_docToolbar);
			}
			m_docToolbar->ClearTools();
		}

		wxAuiPaneInfo& infoToolBar = m_mgr.GetPane(m_docToolbar);

		if (activate) {
			CMetaView* actView = dynamic_cast<CMetaView*>(activeView);
			if (actView != nullptr) {
				actView->OnCreateToolbar(m_docToolbar);
			}
		}

		m_docToolbar->Freeze();
		m_docToolbar->Realize();
		m_docToolbar->Thaw();

		infoToolBar.Show(m_docToolbar->GetToolCount() > 0);

		infoToolBar.BestSize(m_docToolbar->GetSize());
		infoToolBar.FloatingSize(
			m_docToolbar->GetSize().x,
			m_docToolbar->GetSize().y + 25
		);

		m_mgr.Update();
	}
}

IPropertyObject* CDocMDIFrame::GetProperty() const
{
	return m_objectInspector->GetSelectedObject();
}

bool CDocMDIFrame::SetProperty(IPropertyObject* prop)
{
	m_objectInspector->SelectObject(prop);
	return true;
}
