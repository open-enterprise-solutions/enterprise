////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame docview
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "backend/metaCollection/partial/object.h"
#include "frontend/visualView/visualHost.h"
//#include "window/visualView/printout/formPrintOut.h"
#include "frontend/mainFrame/mainFrame.h"
#include "frontend/mainFrame/mainFrameChild.h"

#include "backend/appData.h"

#define st_demonstration _("preview")

static std::map<const CUniqueKey, CVisualDocument*> ms_formOpened = {};

//********************************************************************************************
//*                                  Visual Document & View                                  *
//********************************************************************************************

CVisualView* CVisualDocument::GetFirstView() const
{
	return wxDynamicCast(
		CMetaDocument::GetFirstView(), CVisualView
	);
}

bool CVisualDocument::OnSaveModified()
{
	return wxDocument::OnSaveModified();
}

bool CVisualDocument::OnCloseDocument()
{
	CValueForm* valueForm = m_visualHost ?
		m_visualHost->GetValueForm() : nullptr;

	if (valueForm != nullptr) {
		valueForm->m_valueFormDocument = nullptr;
		if (!m_visualHost->IsDemonstration()) {
			wxTheApp->ScheduleForDestruction(m_visualHost);
			if (valueForm->GetRefCount() > 1) {
				valueForm->DecrRef();
			}
			else {
				wxTheApp->ScheduleForDestruction(valueForm);
			}
			ms_formOpened.erase(m_guidForm);
			m_guidForm.reset();
		}
		valueForm->m_formModified = false;
	}

	return CMetaDocument::OnCloseDocument();
}

bool CVisualDocument::IsModified() const
{
	return m_documentModified;
}

void CVisualDocument::Modify(bool modify)
{
	if (m_visualHost != nullptr) {
		CValueForm* valueForm =
			m_visualHost->GetValueForm();
		if (valueForm != nullptr) {
			valueForm->m_formModified = modify;
		}
	}

	if (modify != m_documentModified) {
		m_documentModified = modify;
		// Allow views to append asterix to the title
		CVisualView* view = GetFirstView();
		if (view != nullptr) {
			view->OnChangeFilename();
		}
	}
}

bool CVisualDocument::Save()
{
	CValueForm* valueForm = m_visualHost ?
		m_visualHost->GetValueForm() : nullptr;

	ISourceDataObject* srcData = valueForm ?
		valueForm->GetSourceObject() : nullptr;

	if (srcData != nullptr &&
		srcData->SaveModify()) {
		CVisualDocument::Modify(false);
		return true;
	}

	return true;
}

bool CVisualDocument::SaveAs()
{
	return true;
}

void CVisualDocument::SetVisualView(CVisualHost* visualHost)
{
	if (visualHost->IsDemonstration()) {
		m_childDoc = false;
	}

	m_visualHost = visualHost;
}

CVisualDocument::~CVisualDocument()
{
	if (m_visualHost != nullptr) {
		if (!wxTheApp->IsScheduledForDestruction(m_visualHost)) {
			wxTheApp->ScheduleForDestruction(m_visualHost);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxPrintout* CVisualView::OnCreatePrintout()
{
	//CVisualDocument* visualDocument =
	//	m_valueForm->GetVisualDocument();
	//return visualDocument ? new CFormPrintout(visualDocument->GetVisualView()) : nullptr;

	return nullptr;
}

void CVisualView::OnUpdate(wxView* sender, wxObject* hint)
{
	if (m_valueForm != nullptr)
		m_valueForm->UpdateForm();
}

bool CVisualView::OnClose(bool deleteWindow)
{
	if (!deleteWindow) {
		if (m_valueForm->m_valueFormDocument
			&& !m_valueForm->CloseDocForm())
			return false;
	}

	//if (CDocMDIFrame::Get())
	//	Activate(false);

	if (deleteWindow) {
		m_viewFrame->Destroy();
		m_viewFrame = nullptr;
	}

	return m_viewDocument ?
		m_viewDocument->Close() : true;
}

/////////////////////////////////////////////////////////////////////////////////////////////

CValueForm* CValueForm::FindFormByUniqueKey(const CUniqueKey& guid)
{
	if (!guid.isValid())
		return nullptr;

	std::map<const CUniqueKey, CVisualDocument*>::iterator foundedForm =
		std::find_if(ms_formOpened.begin(), ms_formOpened.end(),
			[guid](std::pair<const CUniqueKey, CVisualDocument* >& pair) {
				const CUniqueKey& uniqueKey = pair.first; return uniqueKey == guid;
			}
		);

	if (foundedForm != ms_formOpened.end()) {
		CVisualDocument* foundedVisualDocument = foundedForm->second;
		wxASSERT(foundedVisualDocument);
		CVisualView* visualView = foundedVisualDocument->GetFirstView();
		wxASSERT(visualView);
		return visualView->GetValueForm();
	}

	return nullptr;
}

bool CValueForm::UpdateFormUniqueKey(const CUniquePairKey& formKey)
{
	std::map<const CUniqueKey, CVisualDocument*>::iterator foundedForm =
		std::find_if(ms_formOpened.begin(), ms_formOpened.end(),
			[formKey](std::pair<const CUniqueKey, CVisualDocument* >& pair) {
				const CUniqueKey& uniqueKey = pair.first; return uniqueKey.GetGuid() == formKey.GetGuid();
			}
		);

	if (foundedForm != ms_formOpened.end()) {
		CVisualDocument* visualDocument = foundedForm->second;
		wxASSERT(visualDocument);
		ms_formOpened.erase(foundedForm);
		ms_formOpened.insert_or_assign(formKey, visualDocument);
		CVisualView* visualView = visualDocument->GetFirstView();
		wxASSERT(visualView);
		CValueForm* formValue = visualView->GetValueForm();
		wxASSERT(formValue);
		formValue->m_formKey = formKey;
		return true;
	}

	return false;
}

ISourceDataObject* CValueForm::GetSourceObject() const
{
	return m_sourceObject;
}

IMetaObjectForm* CValueForm::GetFormMetaObject() const
{
	return m_metaFormObject;
}

IMetaObjectGenericData* CValueForm::GetMetaObject() const
{
	return m_sourceObject ?
		m_sourceObject->GetSourceMetaObject() : nullptr;
}

#include "frontend/docView/docManager.h"

bool CValueForm::CreateDocForm(CMetaDocument* docParent, bool demoRun)
{
	if (m_valueFormDocument != nullptr) {
		ActivateForm();
		return true;
	}

	if (demoRun) {
		m_valueFormDocument = new CVisualDocument();
	}
	else {
		const CUniqueKey& formKey = m_formKey;
		std::map<const CUniqueKey, CVisualDocument*>::iterator foundedForm =
			std::find_if(ms_formOpened.begin(), ms_formOpened.end(),
				[formKey](std::pair<const CUniqueKey, CVisualDocument* >& pair) {
					const CUniqueKey& uniqueKey = pair.first; return uniqueKey == formKey;
				}
			);

		ISourceDataObject* srcData = GetSourceObject();
		if (srcData != nullptr && !srcData->IsNewObject()) {
			if (foundedForm != ms_formOpened.end()) {
				CVisualDocument* foundedVisualDocument = foundedForm->second;
				wxASSERT(foundedVisualDocument);
				foundedVisualDocument->Activate();
				return true;
			}
		}
		else if (srcData == nullptr) {
			if (foundedForm != ms_formOpened.end()) {
				CVisualDocument* foundedVisualDocument = foundedForm->second;
				wxASSERT(foundedVisualDocument);
				foundedVisualDocument->Activate();
				return true;
			}
		}
		m_valueFormDocument = new CVisualDocument(m_formKey);
	}

	if (docParent != nullptr) {
		m_valueFormDocument->SetDocParent(docParent);
	}

	//if doc has parent - special delete!
	if (docParent == nullptr) {
		docManager->AddDocument(m_valueFormDocument);
	}

	m_valueFormDocument->SetCommandProcessor(m_valueFormDocument->CreateCommandProcessor());
	m_valueFormDocument->SetMetaObject(m_metaFormObject);

	if (m_sourceObject != nullptr && demoRun == false) {
		IMetaObjectGenericData* metaValue = m_sourceObject->GetSourceMetaObject();
		m_valueFormDocument->SetIcon(metaValue->GetIcon());
	}
	else if (m_metaFormObject != nullptr) {
		m_valueFormDocument->SetIcon(m_metaFormObject->GetIcon());
	}

	wxString valueFrameTitle = GetCaption();

	if (demoRun) {
		valueFrameTitle = st_demonstration + wxT(": ") + GetCaption();
	}

	if (valueFrameTitle.IsEmpty()) {
		valueFrameTitle = docManager->MakeNewDocumentName();
	}

	m_valueFormDocument->SetTitle(valueFrameTitle);
	m_valueFormDocument->SetFilename(m_formKey);

	wxScopedPtr<CVisualView> view(new CVisualView(this));

	if (view == nullptr) {
		m_valueFormDocument->DeleteAllViews();
		m_valueFormDocument = nullptr;
		return false;
	}

	view->SetDocument(m_valueFormDocument);

	//set window if is not demonstation
	if (!demoRun)
		ms_formOpened.insert_or_assign(m_formKey, m_valueFormDocument);

	if (!appData->DesignerMode()) {

		CValue bCancel = false;
		if (m_procUnit != nullptr) {
			m_procUnit->CallAsProc("beforeOpen", bCancel);
		}
		if (bCancel.GetBoolean()) {
			ms_formOpened.erase(m_formKey);
			m_valueFormDocument = nullptr;
			return false;
		}
		if (m_procUnit != nullptr) {
			m_procUnit->CallAsProc("onOpen");
		}
	}

	bool isModal = false;
	for (wxWindow* window : wxTopLevelWindows) {
		if (window->IsKindOf(CLASSINFO(wxDialog))) {
			if (((wxDialog*)window)->IsModal()) {
				isModal = true; break;
			}
		}
	}

	long style = wxDEFAULT_FRAME_STYLE;
	if (isModal) style = style | wxCREATE_SDI_FRAME;

	// create a child valueForm of appropriate class for the current mode
	CDocMDIFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, style);

	CVisualHost* visualView = new CVisualHost(m_valueFormDocument, this, view->GetFrame(), demoRun);

	//set visual view
	m_valueFormDocument->SetVisualView(visualView);

	if (!visualView->IsDemonstration())
		m_valueFormDocument->Modify(m_formModified);

	//create and update show frame
	visualView->Freeze();
	visualView->CreateFrame();
	visualView->UpdateFrame();
	visualView->Thaw();

	if (!view->ShowFrame())
		return false;

	return view.release() != nullptr;
}

bool CValueForm::CloseDocForm()
{
	if (m_valueFormDocument == nullptr)
		return false;

	CValue bCancel = false;
	if (m_procUnit != nullptr)
		m_procUnit->CallAsProc("beforeClose", bCancel);
	if (bCancel.GetBoolean())
		return false;

	if (m_procUnit != nullptr)
		m_procUnit->CallAsProc("onClose");

	if (m_controlOwner != nullptr) {
		IBackendValueForm* ownerForm = m_controlOwner->GetOwnerForm();
		if (ownerForm != nullptr) {
			ownerForm->ActivateForm();
		}
	}

	return true;
}
