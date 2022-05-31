////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame docview
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "metadata/metaObjects/objects/baseObject.h"
#include "frontend/visualView/visualHost.h"
#include "frontend/visualView/printout/formPrintOut.h"
#include "frontend/mainFrame.h"
#include "frontend/mainFrameChild.h"

#define st_demonstration _("preview")

static std::map<const CUniqueKey, CVisualDocument*> s_aOpenedForms;

//********************************************************************************************
//*                                  Visual Document & View                                  *
//********************************************************************************************

CVisualView* CVisualDocument::GetFirstView() const
{
	return wxDynamicCast(
		CDocument::GetFirstView(), CVisualView
	);
}

bool CVisualDocument::OnSaveModified()
{
	return wxDocument::OnSaveModified();
}

bool CVisualDocument::OnCloseDocument()
{
	CValueForm* valueForm = m_visualHost ? m_visualHost->GetValueForm() : NULL;

	if (valueForm) {
		valueForm->m_formModified = false;
		valueForm->m_valueFormDocument = NULL;

		if (!m_visualHost->IsDemonstration()) {
			valueForm->m_formModified = false;
			valueForm->m_valueFormDocument = NULL;

			wxTheApp->ScheduleForDestruction(m_visualHost);

			if (valueForm->GetRefCount() > 1) {
				valueForm->DecrRef();
			}
			else {
				wxTheApp->ScheduleForDestruction(valueForm);
			}

			s_aOpenedForms.erase(m_guidForm);
			m_guidForm.reset();
		}
	}

	return CDocument::OnCloseDocument();
}

bool CVisualDocument::IsModified() const
{
	return m_documentModified;
}

void CVisualDocument::Modify(bool modify)
{
	if (m_visualHost) {
		CValueForm* valueForm =
			m_visualHost->GetValueForm();
		if (valueForm) {
			valueForm->m_formModified = modify;
		}
	}

	if (modify != m_documentModified) {
		m_documentModified = modify;
		// Allow views to append asterix to the title
		CVisualView* view = GetFirstView();
		if (view) {
			view->OnChangeFilename();
		}
	}
}

bool CVisualDocument::Save()
{
	CValueForm* valueForm = m_visualHost ?
		m_visualHost->GetValueForm() : NULL;

	ISourceDataObject* srcData = valueForm ?
		valueForm->GetSourceObject() : NULL;

	if (srcData != NULL &&
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
	if (m_visualHost) {
		if (!wxTheApp->IsScheduledForDestruction(m_visualHost)) {
			wxTheApp->ScheduleForDestruction(m_visualHost);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxPrintout* CVisualView::OnCreatePrintout()
{
	CVisualDocument* visualDocument =
		m_valueForm->GetVisualDocument();
	return visualDocument ? new CFormPrintout(visualDocument->GetVisualView()) : NULL;
}

void CVisualView::OnUpdate(wxView* sender, wxObject* hint)
{
	if (m_valueForm) {
		m_valueForm->UpdateForm();
	}
}

bool CVisualView::OnClose(bool deleteWindow)
{
	if (!deleteWindow) {
		if (m_valueForm->m_valueFormDocument
			&& !m_valueForm->CloseDocumentForm())
			return false;
	}

	if (CMainFrame::Get())
		Activate(false);

	if (deleteWindow) {
		m_viewFrame->Destroy();
		m_viewFrame = NULL;
	}

	return m_viewDocument ? m_viewDocument->Close() : true;
}

/////////////////////////////////////////////////////////////////////////////////////////////

CValueForm* CValueForm::FindFormByGuid(const CUniqueKey& guid)
{
	if (!guid.isValid())
		return NULL;

	std::map<const CUniqueKey, CVisualDocument*>::iterator foundedForm =
		std::find_if(s_aOpenedForms.begin(), s_aOpenedForms.end(),
			[guid](std::pair<const CUniqueKey, CVisualDocument* >& pair) {
				const CUniqueKey& uniqueKey = pair.first;
				return uniqueKey == guid;
			}
	);

	if (foundedForm != s_aOpenedForms.end()) {
		CVisualDocument* foundedVisualDocument = foundedForm->second;
		wxASSERT(foundedVisualDocument);
		CVisualView* visualView = foundedVisualDocument->GetFirstView();
		wxASSERT(visualView);
		return visualView->GetValueForm();
	}

	return NULL;
}

bool CValueForm::UpdateFormKey(const CUniquePairKey& formKey)
{
	std::map<const CUniqueKey, CVisualDocument*>::iterator foundedForm =
		std::find_if(s_aOpenedForms.begin(), s_aOpenedForms.end(),
			[formKey](std::pair<const CUniqueKey, CVisualDocument* >& pair) {
				const CUniqueKey& uniqueKey = pair.first;
				return uniqueKey.GetGuid() == formKey.GetGuid();
			}
	);

	if (foundedForm != s_aOpenedForms.end()) {

		CVisualDocument* visualDocument = foundedForm->second;
		wxASSERT(visualDocument);

		s_aOpenedForms.erase(formKey);
		s_aOpenedForms.insert_or_assign(formKey, visualDocument);

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

IMetaFormObject* CValueForm::GetFormMetaObject() const
{
	return m_metaFormObject;
}

IMetaObjectWrapperData* CValueForm::GetMetaObject() const
{
	return m_sourceObject ?
		m_sourceObject->GetMetaObject() : NULL;
}

#include "common/docManager.h"

bool CValueForm::CreateDocumentForm(CDocument* docParent, bool demoRun)
{
	if (m_valueFormDocument != NULL) {
		ActivateForm();
		return true;
	}

	if (demoRun) {
		m_valueFormDocument = new CVisualDocument();
	}
	else {
		const CUniqueKey& formKey = m_formKey;
		std::map<const CUniqueKey, CVisualDocument*>::iterator foundedForm =
			std::find_if(s_aOpenedForms.begin(), s_aOpenedForms.end(),
				[formKey](std::pair<const CUniqueKey, CVisualDocument* >& pair) {
					const CUniqueKey& uniqueKey = pair.first;
					return uniqueKey == formKey;
				}
		);

		if (foundedForm != s_aOpenedForms.end()) {
			CVisualDocument* foundedVisualDocument = foundedForm->second;
			wxASSERT(foundedVisualDocument);
			foundedVisualDocument->Activate();
			return true;
		}

		m_valueFormDocument = new CVisualDocument(m_formKey);
	}

	if (docParent != NULL) {
		m_valueFormDocument->SetDocParent(docParent);
	}

	//if doc has parent - special delete!
	if (docParent == NULL) {
		docManager->AddDocument(m_valueFormDocument);
	}

	m_valueFormDocument->SetCommandProcessor(m_valueFormDocument->CreateCommandProcessor());
	m_valueFormDocument->SetMetaObject(m_metaFormObject);

	if (m_sourceObject != NULL && demoRun == false) {
		IMetaObjectWrapperData* metaValue =
			m_sourceObject->GetMetaObject();
		m_valueFormDocument->SetIcon(metaValue->GetIcon());
	}
	else {
		m_valueFormDocument->SetIcon(m_metaFormObject->GetIcon());
	}

	wxString valueFrameTitle = m_caption;

	if (demoRun) {
		valueFrameTitle = st_demonstration + wxT(": ") + m_caption;
	}

	if (valueFrameTitle.IsEmpty()) {
		valueFrameTitle = docManager->MakeNewDocumentName();
	}

	m_valueFormDocument->SetTitle(valueFrameTitle);
	m_valueFormDocument->SetFilename(m_formKey);

	wxScopedPtr<CVisualView> view(new CVisualView(this));

	if (view == NULL) {
		m_valueFormDocument->DeleteAllViews();
		m_valueFormDocument = NULL;
		return false;
	}

	view->SetDocument(m_valueFormDocument);

	//set window if is not demonstation
	if (!demoRun) {
		s_aOpenedForms.insert_or_assign(m_formKey, m_valueFormDocument);
	}

	if (appData->EnterpriseMode()) {

		CValue bCancel = false;

		if (m_procUnit != NULL) {
			m_procUnit->CallFunction("beforeOpen", bCancel);
		}

		if (bCancel.GetBoolean()) {
			s_aOpenedForms.erase(m_formKey);
			m_valueFormDocument = NULL;
			return false;
		}

		if (m_procUnit != NULL) {
			m_procUnit->CallFunction("onOpen");
		}
	}

	// create a child valueForm of appropriate class for the current mode
	CMainFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE);

	if (demoRun) {
		m_visualHostContext = NULL;
	}

	CVisualHost* visualView =
		new CVisualHost(m_valueFormDocument, this, view->GetFrame(), demoRun);

	//set visual view
	m_valueFormDocument->SetVisualView(visualView);

	if (!visualView->IsDemonstration()) {
		m_valueFormDocument->Modify(m_formModified);
	}

	//create frame 
	visualView->CreateFrame();
	//update and show frame
	visualView->UpdateFrame();

	if (!view->ShowFrame())
		return false;

	return view.release() != NULL;
}

bool CValueForm::CloseDocumentForm()
{
	if (m_valueFormDocument == NULL)
		return false;

	CValue bCancel = false;

	if (m_procUnit != NULL) {
		m_procUnit->CallFunction("beforeClose", bCancel);
	}

	if (bCancel.GetBoolean())
		return false;

	if (m_procUnit != NULL) {
		m_procUnit->CallFunction("onClose");
	}

	return true;
}
