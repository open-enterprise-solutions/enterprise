////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form host
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "frontend/visualView/visualHostClient.h"

#include "backend/appData.h"

#define runFlag 0x000000
#define demoFlag 0x000001

//////////////////////////////////////////////////////////////

void ibValueForm::Modify(bool modify) 
{
	if (IsShown()) {
		GetVisualDocument()->Modify(modify);
	}
	m_formModified = modify;
}

ibFormVisualDocument* ibValueForm::GetVisualDocument() const
{
	return ibFormVisualDocument::FindDocByUniqueKey(m_formKey);
}

const ibValueMetaObjectGenericData* ibValueForm::GetMetaObject() const
{
	return m_sourceObject != nullptr ?
		m_sourceObject->GetSourceMetaObject() : nullptr;
}

//////////////////////////////////////////////////////////////

wxString ibValueForm::GetControlTitle() const
{
	if (m_propertyTitle->IsEmptyProperty()) {

		const ibValueMetaObjectGenericData* metaSource = GetMetaObject();
		if (metaSource != nullptr) return metaSource->GetSynonym();

		const ibValueMetaObjectFormBase* metaForm = GetFormMetaObject();
		if (metaForm != nullptr) return metaForm->GetSynonym();
	}

	return m_propertyTitle->GetValueAsTranslateString();
}

//////////////////////////////////////////////////////////////

#include "frontend/docView/docView.h"

bool ibValueForm::CreateDocForm(ibMetaDocument* docParent, bool createContext)
{
	ibFormVisualDocument* const visualFoundedDoc = GetVisualDocument();
	if (visualFoundedDoc != nullptr) {
		ActivateForm();
		return true;
	}

	if (createContext) {
		ibSourceDataObject* srcData = GetSourceObject();
		if (srcData != nullptr && !srcData->IsNewObject()) {
			ibFormVisualDocument* foundedVisualDocument =
				ibFormVisualDocument::FindDocByUniqueKey(m_formKey);
			if (foundedVisualDocument != nullptr) {
				foundedVisualDocument->Activate();
				return true;
			}
		}
		else if (srcData == nullptr) {
			ibFormVisualDocument* foundedVisualDocument =
				ibFormVisualDocument::FindDocByUniqueKey(m_formKey);
			if (foundedVisualDocument != nullptr) {
				foundedVisualDocument->Activate();
				return true;
			}
		}
	}

#pragma region __value_ref_guard_h__

	class ibValueFormControlGuard {
	public:
		ibValueFormControlGuard(ibValueForm* valueForm) : m_valueForm(valueForm) { valueForm->IncrRef(); }
		~ibValueFormControlGuard() { m_valueForm->DecrRef(); }
	private:
		ibValueForm* m_valueForm;
	};

	ibValueFormControlGuard enter(this);

#pragma endregion

	if (createContext) {

		ibValue bCancel = false;

		if (!CallAsEvent(wxT("beforeOpen"), bCancel))
			return false;

		if (bCancel.GetBoolean())
			return false;

		if (!CallAsEvent(wxT("onOpen")))
			return false;
	}

	ibFormVisualDocument* visualCreatedDoc = createContext ?
		new ibFormVisualDocument(this) :
		new ibFormVisualDocumentDemo(this);

	//if doc has parent - special delete!
	if (docParent != nullptr) {
		visualCreatedDoc->SetDocParent(docParent);
	}
#ifndef OES_USE_WEB
	else if (docParent == nullptr) {
		// docManager is the desktop-wide ibDocManager singleton;
		// wfrontend.dll doesn't construct one. Web sessions keep the
		// document alive through ibWebFrame::m_tabs ownership, not
		// through a manager's document list.
		docManager->AddDocument(visualCreatedDoc);
	}
#endif

	if (visualCreatedDoc->OnCreate(m_formKey, createContext ? runFlag : demoFlag)) {

		if (createContext)
			visualCreatedDoc->Modify(m_formModified);

		RefreshForm();
		return true;
	}

	// The document may be already destroyed, this happens if its view
	// creation fails as then the view being created is destroyed
	// triggering the destruction of the document as this first view is
	// also the last one. However if OnCreate() fails for any reason other
	// than view creation failure, the document is still alive and we need
	// to clean it up ourselves to avoid having a zombie document.

	visualCreatedDoc->DeleteAllViews();
	return false;
}

void ibValueForm::ActivateDocForm()
{
	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();
	if (ownerDocForm != nullptr) {
		CallAsEvent(wxT("onReOpen"));
		ownerDocForm->Activate();
	}
}

void ibValueForm::ChoiceDocForm(ibValue& vSelected)
{
	if (m_controlOwner != nullptr) {
		ibValueForm* ownerForm = m_controlOwner->GetOwnerForm();
		if (ownerForm != nullptr)
			ownerForm->CallAsEvent(wxT("choiceProcessing"), vSelected, GetValue());
		m_controlOwner->ChoiceProcessing(vSelected);
		if (ownerForm != nullptr)
			ownerForm->UpdateForm();
	}
}

void ibValueForm::RefreshDocForm()
{
	if (!appData->DesignerMode()) {
		CallAsEvent(wxT("refreshDisplay"));
	}
}

bool ibValueForm::CloseDocForm()
{
	ibFormVisualDocument* const visualDoc = GetVisualDocument();

	if (visualDoc == nullptr)
		return false;

	if (!appData->DesignerMode() && !visualDoc->IsVisualDemonstrationDoc()) {

		ibValue bCancel = false;
		CallAsEvent(wxT("beforeClose"), bCancel);

		if (bCancel.GetBoolean())
			return false;

		CallAsEvent(wxT("onClose"));
	}

	return true;
}
