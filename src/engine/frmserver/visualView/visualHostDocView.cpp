#include "visualHost.h"

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/system/systemManager.h"

//********************************************************************************************
//*                                       The registry                                       *
//********************************************************************************************

namespace {

void CollectFormDocuments(ibDocument* document, std::vector<ibFormVisualDocument*>& documents)
{
	if (ibFormVisualDocument* const formDocument = dynamic_cast<ibFormVisualDocument*>(document))
		documents.push_back(formDocument);
	for (ibDocument* child : document->GetChildDocuments())
		CollectFormDocuments(child, documents);
}

// The current client's form documents. A form opened on its own is one of its manager's documents; a form opened
// FROM another (a selection form, a form its owner closes with it) is that document's child — so the manager's
// documents and their children, all the way down, ARE the forms the client has open. There is no second list to
// keep in step with them.
std::vector<ibFormVisualDocument*> OpenFormDocuments()
{
	std::vector<ibFormVisualDocument*> documents;
	if (ibDocManager* const manager = ibDocManager::GetDocumentManager()) {
		for (ibDocument* document : manager->GetDocumentsVector())
			CollectFormDocuments(document, documents);
	}
	return documents;
}

// The first open form document `match` accepts — null when none does.
template <class Match>
ibFormVisualDocument* FindFormDocument(Match match)
{
	for (ibFormVisualDocument* document : OpenFormDocuments()) {
		if (match(document))
			return document;
	}
	return nullptr;
}

} // namespace

//********************************************************************************************
//*                                        Document                                          *
//********************************************************************************************

ibFormVisualDocument::ibFormVisualDocument(ibValueForm* valueForm)
	: m_valueForm(valueForm)
{
	if (m_valueForm != nullptr)
		ibFormVisualDocument::SetCommandProcessor(new ibFormVisualCommandProcessor);
}

ibFormVisualDocument::~ibFormVisualDocument()
{
}

ibFormVisualEditView* ibFormVisualDocument::GetFirstView() const
{
	return dynamic_cast<ibFormVisualEditView*>(ibDocument::GetFirstView());
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

const ibMetaData* ibFormVisualDocument::GetMetaData() const
{
	return m_valueForm != nullptr ? m_valueForm->GetMetaData() : nullptr;
}

bool ibFormVisualDocument::OnCreate(const wxString& path, long flags)
{
	const ibSourceDataObject* sourceObject = m_valueForm->GetSourceObject();

	if (sourceObject != nullptr && !IsVisualDemonstrationDoc()) {
		const ibValueMetaObjectGenericData* genericObject = sourceObject->GetSourceMetaObject();
		if (genericObject != nullptr) {
			ibFormVisualDocument::SetIcon(ibBackendPicture::GetServerPicture(genericObject->GetClassType()));
			ibFormVisualDocument::SetFilename(genericObject->GetFileName());
		}
	}
	else {
		const ibValueMetaObjectFormBase* creator = m_valueForm->GetFormMetaObject();
		if (creator != nullptr) {
			ibFormVisualDocument::SetIcon(ibBackendPicture::GetServerPicture(creator->GetClassType()));
			ibFormVisualDocument::SetFilename(creator->GetFileName());
		}
	}
	ibFormVisualDocument::SetTitle(m_valueForm->GetCaption());

	return ibDocument::OnCreate(path, flags);
}

bool ibFormVisualDocument::OnCloseDocument()
{
	if (m_valueForm != nullptr)
		m_valueForm->m_formModified = false;

	// The form it was opened over takes the client back — a selection form closing returns to the form that
	// asked for the choice.
	ibDocManager* const documentManager = GetDocumentManager();
	const ibDocument* const documentParent = m_documentParent;
	if (documentManager != nullptr && documentParent != nullptr)
		documentManager->ActivateView(documentParent->GetFirstView());

	return ibDocument::OnCloseDocument();
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

		// The view puts the mark into the title.
		if (ibFormVisualEditView* const view = GetFirstView())
			view->OnChangeFilename();
	}
}

bool ibFormVisualDocument::Save()
{
	ibSourceDataObject* sourceObject = m_valueForm != nullptr ? m_valueForm->GetSourceObject() : nullptr;

	bool success = true;

	try {
		success = sourceObject != nullptr ? sourceObject->SaveModify() : true;
	}
	catch (const ibCoreException&) {
		// Every refusal — no right, a version conflict, the object's own OnWrite — was reported where it
		// happened, with its own reason (ProcessExceptionError hands it to the frame); saying it again here
		// puts one failure in the messages twice.
		success = false;
	}

	if (success)
		ibFormVisualDocument::Modify(false);

	return true;
}

ibCommandProcessor* ibFormVisualDocument::GetCommandProcessor() const
{
	// While the form's active control is a document of its own, the undo is that document's.
	const ibValueFrame* const control = m_valueForm != nullptr ? m_valueForm->GetActiveControl() : nullptr;
	if (const ibView* const view = control != nullptr ? control->GetControlView() : nullptr)
		return view->GetDocument()->GetCommandProcessor();
	return ibDocument::GetCommandProcessor();
}

void ibFormVisualDocument::SetDocParent(ibDocument* docParent)
{
	ibDocument::SetDocParent(docParent);

	// Detached from its owner, the form lets go of the control that opened it.
	if (docParent == nullptr &&
		(m_valueForm != nullptr && m_valueForm->m_controlOwner != nullptr)) {
		m_valueForm->m_controlOwner->ControlDecrRef();
		m_valueForm->m_controlOwner = nullptr;
	}
}

ibView* ibFormVisualDocument::DoCreateView()
{
	return new ibFormVisualEditView(this);
}

//********************************************************************************************
//*                                    Registry lookups                                      *
//********************************************************************************************

ibUniqueKey ibFormVisualDocument::CreateFormUniqueKey(const ibBackendControlFrame* ownerControl, const ibSourceDataObject* sourceObject, const ibUniqueKey& formKey)
{
	if (formKey.isValid())
		return formKey;                          // 1. the key the caller set
	if (ownerControl != nullptr)
		return ownerControl->GetControlGuid();   // 2. the owner control's
	if (sourceObject != nullptr)
		return sourceObject->GetGuid();          // 3. the object's
	return wxNewUniqueGuid;                      // 4. a new one
}

ibValueForm* ibFormVisualDocument::FindFormByUniqueKey(const ibBackendControlFrame* ownerControl, const ibSourceDataObject* sourceObject, const ibUniqueKey& formKey)
{
	return FindFormByUniqueKey(CreateFormUniqueKey(ownerControl, sourceObject, formKey));
}

ibValueForm* ibFormVisualDocument::FindFormByUniqueKey(const ibUniqueKey& formKey)
{
	if (!formKey.isValid())
		return nullptr;
	ibFormVisualDocument* const document = FindDocByUniqueKey(formKey);
	return document != nullptr ? document->GetValueForm() : nullptr;
}

ibValueForm* ibFormVisualDocument::FindFormByControlUniqueKey(const ibUniqueKey& formKey)
{
	if (!formKey.isValid())
		return nullptr;
	ibFormVisualDocument* const document = FindFormDocument([&formKey](const ibFormVisualDocument* visualDoc) {
		const ibControlFrame* const ownerControl = visualDoc->GetValueForm()->GetOwnerControl();
		return ownerControl != nullptr && formKey == ownerControl->GetControlGuid();
	});
	return document != nullptr ? document->GetValueForm() : nullptr;
}

ibValueForm* ibFormVisualDocument::FindFormBySourceUniqueKey(const ibUniqueKey& formKey)
{
	if (!formKey.isValid())
		return nullptr;
	ibFormVisualDocument* const document = FindFormDocument([&formKey](const ibFormVisualDocument* visualDoc) {
		const ibSourceDataObject* const sourceObject = visualDoc->GetValueForm()->GetSourceObject();
		return sourceObject != nullptr && formKey == sourceObject->GetGuid();
	});
	return document != nullptr ? document->GetValueForm() : nullptr;
}

ibValueForm* ibFormVisualDocument::FindFormByGuid(const ibGuid& guid)
{
	if (!guid.isValid())
		return nullptr;
	// The key's guid — the stable identity UpdateFormUniqueKey finds a form by, whatever its key values.
	ibFormVisualDocument* const document = FindFormDocument([&guid](const ibFormVisualDocument* visualDoc) {
		return visualDoc->GetFormKey().GetGuid() == guid;
	});
	return document != nullptr ? document->GetValueForm() : nullptr;
}

void ibFormVisualDocument::RunIdleHandlers()
{
	for (ibFormVisualDocument* document : OpenFormDocuments())
		document->GetValueForm()->RunIdleHandlers();
}

ibFormVisualDocument* ibFormVisualDocument::FindDocByUniqueKey(const ibUniqueKey& formKey)
{
	return FindFormDocument([&formKey](const ibFormVisualDocument* visualDoc) {
		return visualDoc->CompareFormKey(formKey);
	});
}

bool ibFormVisualDocument::UpdateFormUniqueKey(const ibUniqueKeyPair& formKey)
{
	// Found by the STABLE identity — the instance guid the form's key shares with the object's manager — not by
	// the composite key: the key values change when a dimension is edited, and the form still holds the OLD ones
	// at this point. Writing the new ones onto the form is what this is for.
	ibFormVisualDocument* const document = FindFormDocument([&formKey](const ibFormVisualDocument* visualDoc) {
		return visualDoc->GetFormKey().GetGuid() == formKey.GetGuid();
	});
	if (document == nullptr)
		return false;
	document->GetValueForm()->m_formKey = formKey;
	return true;
}

//********************************************************************************************
//*                                          View                                            *
//********************************************************************************************

ibValueForm* ibFormVisualEditView::GetValueForm() const
{
	return m_document->GetValueForm();
}

bool ibFormVisualEditView::OnCreate(ibDocument* WXUNUSED(doc), long WXUNUSED(flags))
{
	// The form has opened — its beforeOpen / onOpen ran before its document was made (ibValueForm::CreateDocForm):
	// its controls are created. Nothing is drawn: the frame is written when a client asks for it (OnDraw).
	CreateVisualHost();
	return true;
}

void ibFormVisualEditView::OnDraw(ibDataNode& frame)
{
	// The key a client names this form by when it is not its tab's own — a cell of the start page.
	frame.SetValue(wxT("Key"), GetValueForm()->GetFormKey().GetGuid().str());
	UpdateVisualHost(frame);
}

void ibFormVisualEditView::SetCaption(const wxString& strCaption)
{
	const ibValueForm* handler = m_document->GetValueForm();

	if (m_document->IsVisualDemonstrationDoc()) {
		if (strCaption.IsEmpty()) {
			const ibSourceDataObject* srcObject = handler->GetSourceObject();
			if (srcObject != nullptr) {
				const ibValueMetaObjectFormBase* creator = handler->GetFormMetaObject();
				const ibValueMetaObjectGenericData* genericObject = srcObject->GetSourceMetaObject();
				if (genericObject != nullptr) {
					m_document->SetTitle(genericObject->GetSynonym() + wxT(": ") + creator->GetSynonym());
					m_document->SetFilename(genericObject->GetFileName(), true);
				}
				else if (creator != nullptr) {
					m_document->SetTitle(creator->GetSynonym());
					m_document->SetFilename(creator->GetFileName(), true);
				}
			}
			else {
				const ibValueMetaObjectFormBase* creator = handler->GetFormMetaObject();
				if (creator != nullptr) {
					m_document->SetTitle(creator->GetSynonym());
					m_document->SetFilename(creator->GetFileName(), true);
				}
			}
		}
		else {
			m_document->SetTitle(strCaption);
			m_document->SetFilename(wxEmptyString, true);
		}
	}
	else if (strCaption.IsEmpty()) {
		const ibSourceDataObject* srcObject = handler->GetSourceObject();
		if (srcObject != nullptr && !m_document->IsVisualDemonstrationDoc()) {
			m_document->SetTitle(srcObject->GetSourceCaption());
			const ibValueMetaObjectGenericData* genericObject = srcObject->GetSourceMetaObject();
			if (genericObject != nullptr) {
				m_document->SetFilename(genericObject->GetFileName(), true);
			}
			else {
				m_document->SetFilename(srcObject->GetSourceCaption(), true);
			}
		}
		else {
			const ibValueMetaObjectFormBase* creator = handler->GetFormMetaObject();
			if (creator != nullptr && !m_document->IsVisualDemonstrationDoc()) {
				m_document->SetTitle(creator->GetSynonym());
				m_document->SetFilename(creator->GetFileName(), true);
			}
		}
	}
	else if (m_document != nullptr && !m_document->IsVisualDemonstrationDoc()) {
		m_document->SetTitle(strCaption);
		m_document->SetFilename(wxEmptyString, true);
	}
}

#include "frmserver/client/clientChildFrame.h"   // ibClientChildFrame — the tab, and the client it is drawn for
#include "frmserver/client/clientFrame.h"        // ibClientFrame::SendPicture

wxString ibFormVisualEditView::SendPicture(const ibPictureDescription& picture)
{
	// The tab this view is drawn in — its own, or the start page's for a form composed into it.
	const ibClientChildFrame* const tab = dynamic_cast<const ibClientChildFrame*>(GetDocChildFrame());
	ibClientFrame* const frame = tab != nullptr ? tab->GetFrame() : nullptr;
	const ibValueForm* const form = GetValueForm();
	return frame != nullptr ? frame->SendPicture(picture, form != nullptr ? form->GetMetaData() : nullptr) : wxString();
}

void ibFormVisualEditView::OnUpdate(ibView* WXUNUSED(sender), wxObject* WXUNUSED(hint))
{
	// The next frame drawn reads the form as it is; there is nothing kept to refresh.
}

ibView* ibFormVisualEditView::GetActiveControlView() const
{
	const ibValueForm* const form = GetValueForm();
	const ibValueFrame* const control = form != nullptr ? form->GetActiveControl() : nullptr;
	return control != nullptr ? control->GetControlView() : nullptr;
}

void ibFormVisualEditView::OnActivateView(bool activate, ibView* WXUNUSED(activeView), ibView* deactiveView)
{
	if (ibView* view = GetActiveControlView())
		view->OnActivateView(activate, view, deactiveView);
}

bool ibFormVisualEditView::OnClose(bool deleteFrame)
{
	// A COMPOSED form cannot close itself: it shows in its parent's frame, and only the parent takes it down.
	// THIS is where every teardown passes — the Close command, a forced close from the object, a manager sweep —
	// so refusing here is what keeps a composed part from ending up empty.
	const ibDocument* const document = GetDocument();
	if (document != nullptr && document->IsEmbedded() && !document->IsClosedByParent())
		return false;

	if (!deleteFrame) {
		ibValueForm* const valueForm = GetValueForm();
		if (valueForm != nullptr && !valueForm->CloseDocForm())
			return false;
	}

	// A composed form's frame is its parent's: it is let go of, never taken down.
	if (document != nullptr && document->IsEmbedded())
		SetFrame(nullptr);

	return ibView::OnClose(deleteFrame);
}

void ibFormVisualEditView::OnClosingDocument()
{
	// The document is closing — whoever closes it (a tab, the client leaving): the controls are cleaned up
	// while it still holds the form, as the window host's teardown did.
	ClearVisualHost();
}
