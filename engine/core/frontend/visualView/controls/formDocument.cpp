////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame docview
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "metadata/objects/baseObject.h"
#include "frontend/visualView/visualEditorView.h"
#include "frontend/visualView/printout/formPrintOut.h"
#include "frontend/mainFrame.h"
#include "frontend/mainFrameChild.h"

#define st_demonstration _("preview")

class CFormView : public CView
{
	CValueForm *m_valueForm;

public:

	CFormView(CValueForm *valueForm) : m_valueForm(valueForm) {}

	virtual wxPrintout *OnCreatePrintout() override
	{
		CVisualDocument *m_visualDocument = m_valueForm->GetVisualDocument();
		return m_visualDocument ? new CFormPrintout(m_visualDocument->GetVisualView()) : NULL;
	}

	virtual void OnUpdate(wxView *sender, wxObject *hint = NULL) override {
		if (m_valueForm) {
			IDataObjectSource *srcData = m_valueForm->GetSourceObject();
			if (srcData) {
				srcData->UpdateData();
			}
		}
	}

	virtual bool OnClose(bool deleteWindow = true) override
	{
		if (!deleteWindow)
		{
			if (m_valueForm->m_valueFormDocument
				&& !m_valueForm->CloseFrame())
				return false;
		}

		if (CMainFrame::Get())
			Activate(false);

		if (deleteWindow)
		{
			m_viewFrame->Destroy();
			m_viewFrame = NULL;
		}

		return m_viewDocument ? m_viewDocument->Close() : true;
	}

	CValueForm *GetValueForm() { return m_valueForm; }
};

static std::map<const Guid, CVisualDocument *> s_aOpenedForms;

bool CVisualDocument::OnSaveModified()
{
	return wxDocument::OnSaveModified();
}

bool CVisualDocument::OnCloseDocument()
{
	CValueForm *valueForm = m_visualHost ? m_visualHost->GetValueForm() : NULL;

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
		CValueForm *valueForm =
			m_visualHost->GetValueForm();
		if (valueForm) {
			valueForm->m_formModified = modify;
		}
	}

	if (modify != m_documentModified) {
		m_documentModified = modify;
		// Allow views to append asterix to the title
		wxView* view = GetFirstView();
		if (view) {
			view->OnChangeFilename();
		}
	}
}

bool CVisualDocument::Save()
{
	CValueForm *valueForm = m_visualHost ?
		m_visualHost->GetValueForm() : NULL;

	IDataObjectSource *srcData = valueForm ?
		valueForm->GetSourceObject() : NULL;

	if (srcData &&
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

void CVisualDocument::SetVisualView(CVisualView *visualHost)
{
	if (visualHost->IsDemonstration()) {
		m_bChildDoc = false;
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

CValueForm *CValueForm::FindFormByGuid(const Guid &guid)
{
	if (!guid.isValid())
		return NULL;

	std::map<const Guid, CVisualDocument *>::iterator foundedForm
		= s_aOpenedForms.find(guid);

	if (foundedForm != s_aOpenedForms.end()) {
		CVisualDocument *foundedVisualDocument = foundedForm->second;
		wxASSERT(foundedVisualDocument);
		CVisualView *visualView = foundedVisualDocument->GetVisualView();
		wxASSERT(visualView);
		return visualView->GetValueForm();
	}

	return NULL;
}

IDataObjectSource *CValueForm::GetSourceObject() const
{
	return m_sourceObject;
}

IMetaFormObject *CValueForm::GetFormMetaObject() const
{
	return m_metaFormObject;
}

IMetaObjectValue *CValueForm::GetMetaObject() const
{
	return m_sourceObject ?
		m_sourceObject->GetMetaObject() : NULL;
}

#include "common/reportManager.h"

bool CValueForm::ShowDocumentForm(CDocument *docParent, bool demoRun)
{
	if (m_valueFormDocument) {
		ActivateForm();
		return true;
	}

	if (demoRun) {
		m_valueFormDocument = new CVisualDocument();
	}
	else {
		std::map<const Guid, CVisualDocument *>::iterator foundedForm
			= s_aOpenedForms.find(m_formGuid);

		if (foundedForm != s_aOpenedForms.end()) {
			CVisualDocument *foundedVisualDocument = foundedForm->second;
			wxASSERT(foundedVisualDocument);
			foundedVisualDocument->Activate();
			return true;
		}

		m_valueFormDocument = new CVisualDocument(m_formGuid);
	}

	if (docParent) {
		m_valueFormDocument->SetDocParent(docParent);
	}

	//if doc has parent - special delete!
	if (!docParent) {
		reportManager->AddDocument(m_valueFormDocument);
	}

	m_valueFormDocument->SetCommandProcessor(m_valueFormDocument->CreateCommandProcessor());
	m_valueFormDocument->SetMetaObject(m_metaFormObject);

	if (m_sourceObject && demoRun == false) {
		IMetaObjectValue *metaValue = m_sourceObject->GetMetaObject();
		m_valueFormDocument->SetIcon(metaValue->GetIcon());
	}
	else {
		m_valueFormDocument->SetIcon(m_metaFormObject->GetIcon());
	}

	wxString m_valueFrameTitle = m_caption;

	if (demoRun) {
		m_valueFrameTitle = st_demonstration + wxT(": ") + m_caption;
	}

	if (m_valueFrameTitle.IsEmpty()) {
		m_valueFrameTitle = reportManager->MakeNewDocumentName();
	}

	m_valueFormDocument->SetTitle(m_valueFrameTitle);
	m_valueFormDocument->SetFilename(m_formGuid.str());

	wxScopedPtr<CFormView> view(new CFormView(this));
	if (!view) return false;

	view->SetDocument(m_valueFormDocument);

	// create a child valueForm of appropriate class for the current mode
	CMainFrame::CreateChildFrame(view.get(), wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE);

	if (demoRun) {
		m_visualHostContext = NULL;
	}

	CVisualView *visualView =
		new CVisualView(m_valueFormDocument, this, view->GetFrame(), demoRun);

	//set visual view
	m_valueFormDocument->SetVisualView(visualView);

	if (!visualView->IsDemonstration()) {
		m_valueFormDocument->Modify(m_formModified);
	}

	//create frame 
	visualView->CreateFrame();

	//set window if is not demonstation
	if (!visualView->IsDemonstration()) {
		s_aOpenedForms.insert_or_assign(m_formGuid, m_valueFormDocument);
	}

	//update and show frame
	visualView->UpdateFrame();
	view->ShowFrame();
	return view.release() != NULL;
}