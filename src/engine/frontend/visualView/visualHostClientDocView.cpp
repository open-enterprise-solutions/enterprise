#include "visualHostClient.h"

#include "backend/metaCollection/partial/commonObject.h"

#ifdef OES_USE_WEB
#include <iostream>
#endif

static std::set<ibFormVisualDocument*> s_createdDocFormArray = {};

//********************************************************************************************
//*                                  Visual Document & View                                  *
//********************************************************************************************

ibFormVisualEditView* ibFormVisualDocument::GetFirstView() const
{
	return wxDynamicCast(
		ibMetaDocument::GetFirstView(), ibFormVisualEditView
	);
}

const ibUniqueKey& ibFormVisualDocument::GetFormKey() const
{
	return m_valueForm->GetFormKey();
}

bool ibFormVisualDocument::CompareFormKey(const ibUniqueKey& formKey) const
{
	return m_valueForm->CompareFormKey(formKey);
}

ibValueForm* ibFormVisualDocument::GetValueForm() const
{
	return m_valueForm;
}

bool ibFormVisualDocument::OnCreate(const wxString& path, long flags)
{
	const ibSourceDataObject* sourceObject = m_valueForm->GetSourceObject();

	if (sourceObject != nullptr && !IsVisualDemonstrationDoc()) {
		const ibValueMetaObjectGenericData* genericObject = sourceObject->GetSourceMetaObject();
		if (genericObject != nullptr) {
			ibFormVisualDocument::SetIcon(genericObject->GetIcon());
			ibFormVisualDocument::SetFilename(genericObject->GetFileName());
		}
	}
	else {
		const ibValueMetaObjectFormBase* creator = m_valueForm->GetFormMetaObject();
		if (creator != nullptr) {
			ibFormVisualDocument::SetIcon(creator->GetIcon());
			ibFormVisualDocument::SetFilename(creator->GetFileName());
		}
	}

	ibFormVisualDocument::SetTitle(m_valueForm->GetCaption());

	if (IsVisualDemonstrationDoc()) m_childDoc = false;

	return ibMetaDocument::OnCreate(path, flags);
}

bool ibFormVisualDocument::OnCloseDocument()
{
	if (m_valueForm != nullptr)
		m_valueForm->m_formModified = false;

	wxDocManager* documentManager = GetDocumentManager();

	// When the parent document closes, its children must be closed as well as
	// they can't exist without the parent.
	ibMetaDocument const* documentParent = m_documentParent;

	if (documentManager != nullptr && documentParent != nullptr)
		documentManager->ActivateView(documentParent->GetFirstView());

	return ibMetaDocument::OnCloseDocument();
}

bool ibFormVisualDocument::IsCloseOnOwnerClose() const
{
	if (m_valueForm != nullptr)
		return m_valueForm->IsCloseOnOwnerClose();
	return true;
}

void ibFormVisualDocument::Modify(bool modify)
{
	if (m_valueForm != nullptr)
		m_valueForm->m_formModified = modify;

	if (modify != m_documentModified) {

		m_documentModified = modify;

		// Allow views to append asterix to the title
		ibFormVisualEditView* view = GetFirstView();
		if (view != nullptr) view->OnChangeFilename();
	}
}

#include "backend/system/systemManager.h"

bool ibFormVisualDocument::Save()
{
	ibSourceDataObject* sourceObject = m_valueForm != nullptr ?
		m_valueForm->GetSourceObject() : nullptr;

	bool success = true;

	try {
		success = sourceObject != nullptr ?
			sourceObject->SaveModify() : true;
	}
	catch (const ibBackendAccessException& err) {
		ibValueSystemFunction::Alert(err.GetErrorDescription());
		success = false;
	}
	catch (const ibBackendLockException& err) {
		// Version-conflict / row-lock-timeout — show the actual reason
		// ("changed by another user, please reload") instead of the
		// generic "An error occurred". Same pattern as access-denied.
		ibValueSystemFunction::Alert(err.GetErrorDescription());
		success = false;
	}
	catch (const ibBackendException&) {
		ibValueSystemFunction::Alert(_("An error occurred while trying to save the form!"));
		success = false;
	}

	if (success) {
		ibFormVisualDocument::Modify(false);
		return true;
	}

	return true;
}

void ibFormVisualDocument::SetDocParent(ibMetaDocument* docParent)
{
	ibMetaDocument::SetDocParent(docParent);

	if (docParent == nullptr &&
		(m_valueForm != nullptr && m_valueForm->m_controlOwner != nullptr)) {

		m_valueForm->m_controlOwner->ControlDecrRef();
		m_valueForm->m_controlOwner = nullptr;
	}
}

ibMetaView* ibFormVisualDocument::DoCreateView()
{
	return new ibFormVisualEditView();
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibFormVisualDocument::ibFormVisualDocument(ibValueForm* valueForm)
	: m_valueForm(valueForm) {

	if (m_valueForm != nullptr) {

		ibFormVisualDocument::SetCommandProcessor(new ibFormVisualCommandProcessor);
		ibFormVisualDocument::SetMetaObject(nullptr);
	}

	s_createdDocFormArray.insert(this);
}

ibFormVisualDocument::~ibFormVisualDocument()
{
#ifdef OES_USE_WEB
	std::cerr << "[life] ~ibFormVisualDocument " << this
		<< " form=" << (void*)GetValueForm() << std::endl;
#endif
	s_createdDocFormArray.erase(this);
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibMetaData* ibFormVisualDocument::GetMetaData() const
{
	return m_valueForm != nullptr ?
		m_valueForm->GetMetaData() : nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibUniqueKey ibFormVisualDocument::CreateFormUniqueKey(const ibBackendControlFrame* ownerControl, const ibSourceDataObject* sourceObject, const ibUniqueKey& formKey)
{
	if (formKey.isValid()) {
		// 1. if set guid from user
		return formKey;
	}
	else if (ownerControl != nullptr) {
		// 2. if set guid in owner
		return ownerControl->GetControlGuid();
	}
	else if (sourceObject != nullptr) {
		//3. if set guid in object 
		return sourceObject->GetGuid();
	}

	//4. just generate 
	return wxNewUniqueGuid;
}

ibValueForm* ibFormVisualDocument::FindFormByUniqueKey(const ibBackendControlFrame* ownerControl, const ibSourceDataObject* sourceObject, const ibUniqueKey& formKey)
{
	return FindFormByUniqueKey(
		CreateFormUniqueKey(ownerControl, sourceObject, formKey)
	);
}

ibValueForm* ibFormVisualDocument::FindFormByUniqueKey(const ibUniqueKey& formKey)
{
	if (formKey.isValid()) {

		std::set<ibFormVisualDocument*>::iterator foundedForm =
			std::find_if(s_createdDocFormArray.begin(), s_createdDocFormArray.end(),
				[formKey](const ibFormVisualDocument* visualDoc) {
					return visualDoc->CompareFormKey(formKey);
				}
			);

		if (foundedForm != s_createdDocFormArray.end()) {
			ibFormVisualDocument* foundedVisualDocument = *foundedForm;
			wxASSERT(foundedVisualDocument);
			return foundedVisualDocument->GetValueForm();
		}
	}

	return nullptr;
}

ibValueForm* ibFormVisualDocument::FindFormByControlUniqueKey(const ibUniqueKey& formKey)
{
	if (formKey.isValid()) {
		std::set<ibFormVisualDocument*>::iterator foundedSourceForm =
			std::find_if(s_createdDocFormArray.begin(), s_createdDocFormArray.end(),
				[formKey](const ibFormVisualDocument* visualDoc) {
					wxASSERT(visualDoc);
					ibValueForm* valueForm = visualDoc->GetValueForm();
					wxASSERT(valueForm);
					ibValueFrame* ownerControl = valueForm->GetOwnerControl();
					if (ownerControl != nullptr) return formKey == ownerControl->GetControlGuid();
					return false;
				}
			);

		if (foundedSourceForm != s_createdDocFormArray.end()) {
			ibFormVisualDocument* foundedVisualDocument = *foundedSourceForm;
			wxASSERT(foundedVisualDocument);
			return foundedVisualDocument->GetValueForm();
		}
	}

	return nullptr;
}

ibValueForm* ibFormVisualDocument::FindFormBySourceUniqueKey(const ibUniqueKey& formKey)
{
	if (formKey.isValid()) {
		std::set<ibFormVisualDocument*>::iterator foundedSourceForm =
			std::find_if(s_createdDocFormArray.begin(), s_createdDocFormArray.end(),
				[formKey](const ibFormVisualDocument* visualDoc) {
					ibValueForm* valueForm = visualDoc->GetValueForm();
					wxASSERT(valueForm);
					ibSourceDataObject* sourceObject = valueForm->GetSourceObject();
					if (sourceObject != nullptr) return formKey == sourceObject->GetGuid();
					return false;
				}
			);

		if (foundedSourceForm != s_createdDocFormArray.end()) {
			ibFormVisualDocument* foundedVisualDocument = *foundedSourceForm;
			wxASSERT(foundedVisualDocument);
			return foundedVisualDocument->GetValueForm();
		}
	}

	return nullptr;
}

ibFormVisualDocument* ibFormVisualDocument::FindDocByUniqueKey(const ibUniqueKey& formKey)
{
	for (auto& visualDocument : s_createdDocFormArray) {
		if (visualDocument != nullptr &&
			visualDocument->CompareFormKey(formKey))
		{
			return visualDocument;
		}
	}

	return nullptr;
}

bool ibFormVisualDocument::UpdateFormUniqueKey(const ibUniqueKeyPair& formKey)
{
	// Lookup by stable instance GUID, NOT by composite key:
	//  * `m_objGuid` is per-instance (set once by the ctor), shared
	//    between the manager's m_objGuid and the form's m_formKey
	//    because they reference the same ibUniqueKeyPair instance.
	//  * `m_keyValues` changes when the user edits a dimension —
	//    using operator== (which dispatches to enUniqueKey →
	//    m_keyValues compare) would miss the form right after save
	//    because form still holds the OLD keyValues at this point.
	// The whole purpose of this method is to write the NEW keyValues
	// onto the form, so we must locate it by the stable identity
	// (m_objGuid), then replace m_formKey with the fresh pair.
	std::set<ibFormVisualDocument*>::iterator foundedForm =
		std::find_if(s_createdDocFormArray.begin(), s_createdDocFormArray.end(),
			[formKey](const ibFormVisualDocument* visualDoc) {
				return visualDoc != nullptr &&
					visualDoc->GetFormKey().GetGuid() == formKey.GetGuid();
			}
		);

	if (foundedForm != s_createdDocFormArray.end()) {
		ibFormVisualDocument* visualDocument = *foundedForm;
		wxASSERT(visualDocument);
		ibValueForm* formValue = visualDocument->GetValueForm();
		wxASSERT(formValue);
		formValue->m_formKey = formKey;
		return true;
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////

wxPrintout* ibFormVisualEditView::OnCreatePrintout()
{
#ifdef OES_USE_WEB
	// Printing reaches back through wxWindow::FindFocus() and
	// ibVisualHost::GetObjectBase (a map of wx widgets). Neither exists
	// on web — printing would need a dedicated "render-current-form-to-
	// PDF" endpoint instead. Until then, no printout.
	return nullptr;
#else
	wxWindow* focusedWindow = wxWindow::FindFocus();
	while (focusedWindow != nullptr) {

		ibValueFrame* currentFrame = m_visualHost->GetObjectBase(focusedWindow);
		if (currentFrame != nullptr)
			return currentFrame->CreatePrintout();

		focusedWindow = focusedWindow->GetParent();
	}

	return nullptr;
#endif
}

bool ibFormVisualEditView::OnCreate(ibMetaDocument* doc, long flags)
{
	std::set<ibFormVisualDocument*>::iterator foundedVisualDoc =

		std::find_if(s_createdDocFormArray.begin(), s_createdDocFormArray.end(),
			[doc](const ibFormVisualDocument* visualDoc) {
				return doc != nullptr &&
					doc == visualDoc;
			}
		);

	if (foundedVisualDoc != s_createdDocFormArray.end()) {
		ibValueForm* const valueForm = (*foundedVisualDoc)->GetValueForm();
		wxASSERT(valueForm);
		m_visualHost = new ibVisualHostClient(*foundedVisualDoc, valueForm, m_viewFrame);
		return m_visualHost->CreateAndUpdateVisualHost();
	}

	return ibMetaView::OnCreate(doc, flags);
}

void ibFormVisualEditView::OnUpdate(wxView* sender, wxObject* hint)
{
	if (m_visualHost != nullptr)
		m_visualHost->UpdateForm();
}

bool ibFormVisualEditView::OnClose(bool deleteWindow)
{
	if (!deleteWindow) {

		ibMetaDocument const* doc = GetDocument();
		wxASSERT(doc);

		std::set<ibFormVisualDocument*>::iterator foundedVisualDoc =

			std::find_if(s_createdDocFormArray.begin(), s_createdDocFormArray.end(),
				[doc](const ibFormVisualDocument* visualDoc) {
					return doc != nullptr &&
						doc == visualDoc;
				}
			);

		if (foundedVisualDoc != s_createdDocFormArray.end()) {
			ibValueForm* const valueForm = (*foundedVisualDoc)->GetValueForm();
			wxASSERT(valueForm);
			if (valueForm != nullptr && !valueForm->CloseDocForm())
				return false;
		}
	}

	//if (CDocMDIFrame::Get())
	//	Activate(false);

#ifndef OES_USE_WEB
	// GetFrame() returns a wxWindow (the wxDocChildFrame hosting this
	// view on desktop). On web the view's "frame" is an ibWebDocChildFrame
	// living in ibWebFrame's tab vector — its lifetime is managed by
	// the session's tab close path, not by the view. So no Destroy here.
	//
	// This runs through the CloseForm → CallAfter defer path
	// (formObject.cpp), so we're on a fresh idle dispatch — safe to
	// Destroy synchronously.
	if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}
#endif

	return ibMetaView::OnClose(deleteWindow);
}

void ibFormVisualEditView::OnClosingDocument()
{
	if (m_visualHost != nullptr) {
#ifdef OES_USE_WEB
		// wxTheApp is null in wfrontend.dll (no wxApp instance, just
		// wxInitializer). Delete inline — safe because the web host
		// isn't an evented wxWindow, just an ibWebWindow-derived node.
		delete m_visualHost;
#else
		wxTheApp->ScheduleForDestruction(m_visualHost);
#endif
		m_visualHost = nullptr;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibFormVisualEditView::~ibFormVisualEditView()
{
#ifdef OES_USE_WEB
	if (m_visualHost != nullptr) {
		delete m_visualHost;
		m_visualHost = nullptr;
	}
#else
	if (m_visualHost != nullptr && !wxTheApp->IsScheduledForDestruction(m_visualHost)) {
		wxTheApp->ScheduleForDestruction(m_visualHost);
	}
#endif
}
