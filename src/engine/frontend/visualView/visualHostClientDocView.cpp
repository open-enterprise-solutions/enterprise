#include "visualHostClient.h"

#include "backend/metaCollection/partial/commonObject.h"

#ifdef OES_USE_WEB
#include <iostream>
#endif

#include <functional>

#ifndef OES_USE_WEB
#include "frontend/mainFrame/mainFrame.h"   // mainFrame->ActivateView — rebuilds the chrome
#endif

static std::set<ibFormVisualDocument*> s_createdDocFormArray = {};

//********************************************************************************************
//*                                  Visual Document & View                                  *
//********************************************************************************************

ibFormVisualEditView* ibFormVisualDocument::GetFirstView() const
{
	return wxDynamicCast(
		ibDocument::GetFirstView(), ibFormVisualEditView
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

	return ibDocument::OnCreate(path, flags);
}

bool ibFormVisualDocument::OnCloseDocument()
{
	if (m_valueForm != nullptr)
		m_valueForm->m_formModified = false;

	ibDocManager* documentManager = GetDocumentManager();

	// When the parent document closes, its children must be closed as well as
	// they can't exist without the parent. m_documentParent is ibDocument*;
	// GetFirstView lives on ibDocument, so no downcast is needed.
	ibDocument const* documentParent = m_documentParent;

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
	catch (const ibBackendAccessException&) {
		// Already reported where it happened (ProcessExceptionError hands it to the frame) - saying it
		// again puts one failure in the pane twice. Unnamed: nothing is read from it any more, and a
		// named-but-unused parameter is a warning on the compilers this has to stay quiet on.
		success = false;
	}
	catch (const ibBackendLockException& err) {
		// Version-conflict / row-lock-timeout — show the actual reason
		// ("changed by another user, please reload") instead of the
		// generic "An error occurred". Same pattern as access-denied.
		// Already reported where it happened (ProcessExceptionError hands it to the frame) - saying it
		// again puts one failure in the pane twice.
		success = false;
	}
	catch (const ibBackendException& err) {
		// The exception already carries the reason ("Register 'Stock': failed to store the
		// records", "… cancelled by the OnWrite handler"). Replacing it with a generic
		// "an error occurred" threw away the only part worth reading — show what it says.
		// Already reported where it happened (ProcessExceptionError hands it to the frame) - saying it
		// again puts one failure in the pane twice.
		success = false;
	}

	if (success) {
		ibFormVisualDocument::Modify(false);
		return true;
	}

	return true;
}

wxCommandProcessor* ibFormVisualDocument::GetCommandProcessor() const
{
	// While the form's active control is a document of its own, the undo is that document's.
	const ibValueFrame* const control = m_valueForm != nullptr ? m_valueForm->GetActiveControl() : nullptr;
	if (const ibView* const view = control != nullptr ? control->GetControlView() : nullptr)
		return view->GetDocument()->GetCommandProcessor();
	return ibDocument::GetCommandProcessor();
}

void ibFormVisualDocument::SetDocParent(ibDocument* docParent)
{
	// Base's SetDocParent now lives on ibDocument (step-4 collapse); the
	// adapter no longer overrides it. Forward to base, then run the
	// form-side cleanup if we're detaching.
	ibDocument::SetDocParent(docParent);

	if (docParent == nullptr &&
		(m_valueForm != nullptr && m_valueForm->m_controlOwner != nullptr)) {

		m_valueForm->m_controlOwner->ControlDecrRef();
		m_valueForm->m_controlOwner = nullptr;
	}
}

ibView* ibFormVisualDocument::DoCreateView()
{
	return new ibFormVisualEditView();
}

/////////////////////////////////////////////////////////////////////////////////////////////

ibFormVisualDocument::ibFormVisualDocument(ibValueForm* valueForm)
	: m_valueForm(valueForm) {

	if (m_valueForm != nullptr) {

		ibFormVisualDocument::SetCommandProcessor(new ibFormVisualCommandProcessor);
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

const ibMetaData* ibFormVisualDocument::GetMetaData() const
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
					ibControlFrame* ownerControl = valueForm->GetOwnerControl();
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

#ifndef OES_USE_WEB
// What a control prints as — ONE answer for both searches below: the view it holds prints it (a grid box
// prints as the spreadsheet document does, a text box as the text document), and a bare control prints
// nothing.
static wxPrintout* CreateControlPrintout(const ibValueFrame* control)
{
	ibView* const view = control->GetControlView();
	return view != nullptr ? view->OnCreatePrintout() : nullptr;
}
#endif

wxPrintout* ibFormVisualEditView::OnCreatePrintout()
{
#ifdef OES_USE_WEB
	// Printing reaches back through wxWindow::FindFocus() and
	// ibVisualHost::GetObjectBase (a map of wx widgets). Neither exists
	// on web — printing would need a dedicated "render-current-form-to-
	// PDF" endpoint instead. Until then, no printout.
	return nullptr;
#else
	ibValueForm* const form = m_visualHost->GetValueForm();
	if (form == nullptr)
		return nullptr;

	// ⭐ THE ACTIVE CONTROL FIRST: the cursor in a table's cell means that table.
	//
	// 🛑 It used to be the FOCUSED control only. A field in focus, which prints nothing, ended the search
	// there, so a report with the cursor in its settings printed nothing at all and said nothing; and in the
	// preview the focus is not in the form at all (2026-09-22). The active control stays where it was put.
	if (const ibValueFrame* const control = form->GetActiveControl()) {
		if (wxPrintout* const printout = CreateControlPrintout(control))
			return printout;
	}

	// …AND FAILING THAT, THE ONE THE FORM HAS. A report or a printed form holds a single table, and it
	// is what Print means wherever the cursor stands. With several, which one is meant is the person's
	// to say — by putting the cursor in it.
	wxPrintout* only = nullptr;
	int printable = 0;

	std::function<void(ibValueFrame*)> walk = [&](ibValueFrame* frame) {
		for (unsigned int idx = 0; idx < frame->GetChildCount(); idx++) {
			ibValueFrame* child = frame->GetChild(idx);
			if (wxPrintout* printout = CreateControlPrintout(child)) {
				if (printable++ == 0)
					only = printout;
				else
					delete printout;
			}
			walk(child);
		}
	};

	walk(form);

	if (printable == 1)
		return only;

	delete only;
	return nullptr;
#endif
}

bool ibFormVisualEditView::OnCreate(ibDocument* doc, long flags)
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
		const bool created = m_visualHost->CreateAndUpdateVisualHost();
#ifndef OES_USE_WEB
		if (created) {
			WatchFocus(true);
			// EVERY command passes by: which ids are the active control's is its view's to say.
			Bind(wxEVT_MENU, &ibFormVisualEditView::OnActiveControlCommand, this);
			Bind(wxEVT_UPDATE_UI, &ibFormVisualEditView::OnUpdateActiveControlSave, this, wxID_SAVE);
			Bind(wxEVT_UPDATE_UI, &ibFormVisualEditView::OnUpdateActiveControlSave, this, wxID_SAVEAS);
		}
#endif
		return created;
	}

	return ibView::OnCreate(doc, flags);
}

//********************************************************************************************
//*                          The facade over the active control                              *
//********************************************************************************************

ibView* ibFormVisualEditView::GetActiveControlView() const
{
	const ibValueForm* const form = m_visualHost != nullptr ? m_visualHost->GetValueForm() : nullptr;
	const ibValueFrame* const control = form != nullptr ? form->GetActiveControl() : nullptr;
	return control != nullptr ? control->GetControlView() : nullptr;
}

#if wxUSE_MENUS
wxMenuBar* ibFormVisualEditView::CreateMenuBar() const
{
	if (ibView* view = GetActiveControlView())
		return view->CreateMenuBar();
	return nullptr;
}
#endif

void ibFormVisualEditView::OnCreateToolbar(wxAuiToolBar* toolbar)
{
	if (ibView* view = GetActiveControlView())
		view->OnCreateToolbar(toolbar);
}

void ibFormVisualEditView::OnActivateView(bool activate, ibView* WXUNUSED(activeView), ibView* deactiveView)
{
	if (ibView* view = GetActiveControlView())
		view->OnActivateView(activate, view, deactiveView);
}

#ifndef OES_USE_WEB

void ibFormVisualEditView::WatchFocus(bool watch)
{
	wxWindow* const frame = dynamic_cast<wxWindow*>(GetFrame());
	if (frame == nullptr)
		return;

	if (watch)
		frame->Bind(wxEVT_CHILD_FOCUS, &ibFormVisualEditView::OnChildFocus, this);
	else
		frame->Unbind(wxEVT_CHILD_FOCUS, &ibFormVisualEditView::OnChildFocus, this);
}

// ⭐ THE ACTIVE CONTROL IS PUT WHERE THE FOCUS GOES — the first control of this form on the way from the
// focused window up. A focus outside the form (a menu, a toolbar, the preview) leaves it where it was. The
// chrome is rebuilt only when the view it comes from changes, and after the focus has settled.
void ibFormVisualEditView::OnChildFocus(wxChildFocusEvent& event)
{
	event.Skip();   // the focus goes where it was going; this only watches it

	if (ibValueForm* const form = m_visualHost != nullptr ? m_visualHost->GetValueForm() : nullptr) {
		wxWindow* const frame = dynamic_cast<wxWindow*>(GetFrame());
		for (wxWindow* window = wxWindow::FindFocus(); window != nullptr && window != frame; window = window->GetParent()) {
			if (ibValueFrame* control = m_visualHost->GetObjectBase(window)) {
				form->SetActiveControl(control);
				break;
			}
		}
	}

	const ibView* const shown = GetActiveControlView();
	if (shown == m_shownControlView)
		return;

	m_shownControlView = shown;

	// The chrome, and the view it now comes from is ACTIVATED, as a document's view is when its tab is —
	// a grid box shows its sheet's properties in the inspector, as a spreadsheet document does.
	CallAfter([this]() {
		if (mainFrame != nullptr && GetFrame() != nullptr) {
			mainFrame->ActivateView(this, true);
			OnActivateView(true, this, nullptr);
		}
	});
}

// ⭐ THE FORM IS A FACADE over the view its active control holds. That view answers its own commands, LOCALLY —
// it hands nothing further up, since up is where the command came from. Save and Save as are asked of the
// document behind it: the manager would save the FORM's, and activating the control means its document.
// Anything else goes on its usual way.
void ibFormVisualEditView::OnActiveControlCommand(wxCommandEvent& event)
{
	ibView* const view = GetActiveControlView();
	if (view == nullptr) {
		event.Skip();
		return;
	}

	if (event.GetId() == wxID_SAVE) {
		view->GetDocument()->Save();
		return;
	}
	if (event.GetId() == wxID_SAVEAS) {
		view->GetDocument()->SaveAs();
		return;
	}

	if (!view->ProcessEventLocally(event))
		event.Skip();
}

// …and Save and Save as are offered for it: the manager would ask the FORM's document whether there is anything
// to save, and whether it may be saved as a file.
void ibFormVisualEditView::OnUpdateActiveControlSave(wxUpdateUIEvent& event)
{
	if (GetActiveControlView() != nullptr)
		event.Enable(true);
	else
		event.Skip();
}

#endif // !OES_USE_WEB

/////////////////////////////////////////////////////////////////////////////////////////////

void ibFormVisualEditView::OnUpdate(ibView* sender, wxObject* hint)
{
	if (m_visualHost != nullptr)
		m_visualHost->UpdateForm();
}

bool ibFormVisualEditView::OnClose(bool deleteWindow)
{
	// A COMPOSED form cannot close itself: its window belongs to the parent, and only the
	// parent takes it down. THIS is where every teardown passes — the Close command, a forced
	// close from the object, a manager sweep — so refusing here is what keeps a pane from
	// ending up empty. (ibValueForm::CloseForm refuses too, but that covers only the button.)
	const ibDocument* const composedDoc = GetDocument();
	if (composedDoc != nullptr && composedDoc->IsEmbedded() && !composedDoc->IsClosedByParent())
		return false;

	if (!deleteWindow) {

		ibDocument const* doc = GetDocument();
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
	// The focus watch comes off before the window is let go: an embedded form's window outlives this view.
	WatchFocus(false);

	// GetFrame() returns a wxWindow (the ibDocChildFrame hosting this
	// view on desktop). On web the view's "frame" is an ibWebDocChildFrame
	// living in ibWebFrame's tab vector — its lifetime is managed by
	// the session's tab close path, not by the view. So no Destroy here.
	//
	// This runs through the CloseForm → CallAfter defer path
	// (formObject.cpp), so we're on a fresh idle dispatch — safe to
	// Destroy synchronously.
	//
	// An EMBEDDED form (a cell of the home-page composite) does NOT own its
	// frame — the composite does. Destroying it here would tear a hole in
	// the host's splitter tree, so the embedded view only releases it.
	const ibDocument* const ownerDoc = GetDocument();
	if (ownerDoc != nullptr && ownerDoc->IsEmbedded()) {
		SetFrame(nullptr);
	}
	else if (deleteWindow) {
		GetFrame()->Destroy();
		SetFrame(nullptr);
	}
#endif

	return ibView::OnClose(deleteWindow);
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
