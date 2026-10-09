////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame object
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "formAttribute.h"
#include "frmserver/settings/formSettings.h"   // the person's own arrangement, replayed on open
#include "backend/appData.h"
#include "backend/system/value/valueJob.h"   // g_valueScheduleCLSID — a schedule requisite builds as static text
#include "backend/system/value/valueDataComposition.h"   // g_valueDataCompositionCLSID — a composition builds as a gridbox
#include "gridBox.h"   // ibValueGridBox — a report's default view
#include "widgets.h"   // the check box, caption and field a generated form is made of
#include "backend/metaCollection/partial/dataReport.h"   // …bound to the report itself, when it has a default composer
#include "backend/metaData.h"
#include "frmserver/visualView/visualHost.h"
#include "backend/srcDataObject.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/session/session.h"
#include "backend/backend_mainFrame.h"

//*************************************************************************************************
//*                                    System attribute                                           *
//*************************************************************************************************

#include "tableBox.h"

void ibValueForm::BuildForm(const ibFormID& formType)
{
	m_formType = formType;

	ibFormAttributeValue* mainAttr = GetMainAttribute();
	const ibSourceDataObject* sourceObject = mainAttr != nullptr ? mainAttr->GetSourceValue() : nullptr;

	if (sourceObject != nullptr) {

		// Everything binds THROUGH the main attribute (the gate): control source
		// paths start with its id, then walk the metadata. The incoming source
		// object is only used to lay the controls out, then copied into the main
		// attribute (InitializeForm) and forgotten — reads go via the attribute.
		const ibMetaID mainAttrId = mainAttr->GetId();

		// Form-level toolbar is now the form's command-bar chrome (m_commandBar,
		// AutoFill from the same action collection) — no explicit MainToolbar.
		ibValueModelTableBox* mainTableBox = nullptr;

		const ibSourceExplorer* sourceExplorerPtr = sourceObject->GetSourceExplorer();
		static const ibSourceExplorer s_emptyExplorer;
		const ibSourceExplorer& sourceExplorer = sourceExplorerPtr != nullptr ? *sourceExplorerPtr : s_emptyExplorer;

		// List vs object is decided by the SOURCE class via the class factory (IsTableSource —
		// CLSID → ctor → IsTableValue), not the source explorer's flag. Every ibValueModel
		// (list / tree / table / dynamic list) is a tabular source, a record object is not.
		// The explorer is now only the column/field TEMPLATE — a queryable-based dynamic list,
		// which carries no tableSection flag, renders as a tablebox just the same.
		const bool isTableSource = sourceObject->IsTableSource();

		if (isTableSource) {

			mainTableBox =
				dynamic_cast<ibValueModelTableBox*>(ibValueForm::CreateControl(wxT("Tablebox")));

			mainTableBox->SetControlName(sourceExplorer.GetSourceName());
			
			// A picker source stamps its main table node with choice mode — carry it onto the table so it shows
			// Select first (the runtime open-as-choice path; the designer property is the alternative source).
			mainTableBox->SetChoiceMode(sourceExplorer.IsChoiceMode());

			// The MAIN attribute IS the list (its Type is CatalogList.<X>) — its source is
			// just the attribute itself, shown as "List". The extra source-id hop (the row
			// catalog) was redundant here and rendered "List.Catalog1".
			mainTableBox->SetSource({ mainAttrId });
		}

		// ⭐⭐ A REPORT IS SHOWN BY A GRID (Max, 2026-08-20: "we know we are looking at a report
		// object, so we can give it a grid by default") — but only a report that DECLARED a
		// composer, because the box is bound to the composer and there is nothing to bind to
		// without one. A report with no composer gets no box; declaring the first one is what
		// makes the box appear.
		//
		// Bound to the object — a single hop — which is what makes this box the form's MAIN view:
		// the form's command provider resolves to it, so the composer's verbs appear on the form's
		// own toolbar and the box carries no second bar (IsMainSourceBound / HasCommandBar).
		//
		// ⭐ AND ITS SOURCE IS THE COMPOSER, NOT THE REPORT (Max, 2026-08-20: "a report cannot itself
		// be the source — the composer can; you substitute it in the builder by default"). The
		// report DECLARES what to show; what is shown is the composition, so the binding names it.
		if (!isTableSource) {
			const auto* report = dynamic_cast<const ibValueRecordDataObjectReport*>(sourceObject);
			const ibValueMetaObjectReport* metaReport =
				report != nullptr ? dynamic_cast<const ibValueMetaObjectReport*>(report->GetMetaObject()) : nullptr;
			const ibMetaID defaultComposer = metaReport != nullptr ? metaReport->GetDefComposer() : wxNOT_FOUND;

			if (defaultComposer != wxNOT_FOUND) {
				ibValueGridBox* gridBox =
					dynamic_cast<ibValueGridBox*>(ibValueForm::CreateControl(wxT("Gridbox")));
				if (gridBox != nullptr) {
					gridBox->SetControlName(sourceExplorer.GetSourceName());
					gridBox->SetSource({ mainAttrId, defaultComposer });
				}
			}
		}

		for (unsigned int idx = 0; idx < sourceExplorer.GetHelperCount(); idx++) {

			const ibSourceExplorer* nextPtr = sourceExplorer.GetHelper(idx);
			if (nextPtr == nullptr)
				continue;
			const ibSourceExplorer& nextSourceExplorer = *nextPtr;

			if (isTableSource) {
				// The source says which of its columns are ONE FAMILY (the register's dimension
				// slots), and such a column hangs on that family's GROUP rather than on the
				// table — which stacks them, instead of laying twelve of them out sideways.
				ibValueFrame* holder = mainTableBox->GetColumnGroupHolder(nextSourceExplorer.GetSourceGroup());

				ibValueModelTableBoxColumn* tableBoxColumn =
					dynamic_cast<ibValueModelTableBoxColumn*>(ibValueForm::CreateControl(wxT("TableboxColumn"), holder));
				tableBoxColumn->SetControlName(mainTableBox->GetControlName() + nextSourceExplorer.GetSourceName());
				tableBoxColumn->SetVisibleColumn(nextSourceExplorer.IsVisible() || sourceExplorer.GetHelperCount() == 1);
				// Column = [mainAttr, field] → "List.Field". The row-type hop (the catalog/document)
				// is implicit in the list-typed main attribute — no "List.Document1.Field".
				tableBoxColumn->SetSource({ mainAttrId, nextSourceExplorer.GetSourceId() });
			}
			else
			{
				if (nextSourceExplorer.IsTableSection()) {

					ibValueModelTableBox* tableBox =
						dynamic_cast<ibValueModelTableBox*>(ibValueForm::CreateControl(wxT("Tablebox")));

					tableBox->SetControlName(nextSourceExplorer.GetSourceName());
					tableBox->SetSource({ mainAttrId, nextSourceExplorer.GetSourceId() });

					for (unsigned int col = 0; col < nextSourceExplorer.GetHelperCount(); col++) {
						const ibSourceExplorer* colExplorerPtr = nextSourceExplorer.GetHelper(col);
						if (colExplorerPtr == nullptr)
							continue;
						const ibSourceExplorer& colSourceExplorer = *colExplorerPtr;

						ibValueFrame* holder = tableBox->GetColumnGroupHolder(colSourceExplorer.GetSourceGroup());

						ibValueModelTableBoxColumn* tableBoxColumn =
							dynamic_cast<ibValueModelTableBoxColumn*>(ibValueForm::CreateControl(wxT("TableboxColumn"), holder));
						tableBoxColumn->SetControlName(tableBox->GetControlName() + colSourceExplorer.GetSourceName());
						//tableBoxColumn->SetCaption(colSourceExplorer.GetSourceSynonym());
						tableBoxColumn->SetVisibleColumn(colSourceExplorer.IsVisible()
							|| nextSourceExplorer.GetHelperCount() == 1);
						tableBoxColumn->SetSource({ mainAttrId, nextSourceExplorer.GetSourceId(), colSourceExplorer.GetSourceId() });
					}
				}
				else {
					if (nextSourceExplorer.ContainType(ibValueTypes::TYPE_BOOLEAN)
						&& nextSourceExplorer.GetClsidList().size() == 1) {
						ibValueCheckbox* checkbox =
							dynamic_cast<ibValueCheckbox*>(ibValueForm::CreateControl(wxT("Checkbox")));
						checkbox->SetControlName(nextSourceExplorer.GetSourceName());
						//checkbox->SetCaption(nextSourceExplorer.GetSourceSynonym());
						checkbox->EnableWindow(nextSourceExplorer.IsEnabled());
						checkbox->VisibleWindow(nextSourceExplorer.IsVisible());
						checkbox->SetSource({ mainAttrId, nextSourceExplorer.GetSourceId() });
					}
					// ⭐ A COMPOSITION IS SHOWN BY A GRIDBOX, and that is what makes a report need no
					// form: the report declares a composer, the composer is a node here, and the
					// generated form comes up with the sheet the report composes into — plus the
					// gridbox's own Compose / Settings commands, which it carries because its
					// source is a composition (Max, 2026-08-20: "you add a composer, save, and you
					// do not even have to make a form").
					// A COMPOSER IS NOT A FIELD — it is not laid out one by one here. The report
					// itself is the grid's source (see above), and a second composer is reached by
					// hand (`Object.Composer2`, a grid of its own).
					else if (nextSourceExplorer.GetClsidList().size() == 1
						&& nextSourceExplorer.ContainType(g_valueDataCompositionCLSID)) {
						continue;
					}
					// (g_valueScheduleCLSID — backend/system/value/valueJob.h, included at the top)
					// A SCHEDULE is shown, not typed. There is nothing sensible to put in an edit
					// box — the value is fourteen fields — so the auto-built control is the static
					// text, which renders the schedule as its own sentence ("Every 10 minutes,
					// 02:00-05:00, Mon") and opens the four-tab editor when clicked.
					else if (nextSourceExplorer.GetClsidList().size() == 1
						&& nextSourceExplorer.ContainType(g_valueScheduleCLSID)) {
						ibValueStaticText* staticText =
							dynamic_cast<ibValueStaticText*>(ibValueForm::CreateControl(wxT("Statictext")));
						staticText->SetControlName(nextSourceExplorer.GetSourceName());
						// The caption comes from the METADATA — "Schedule", not the widget's own
						// "Static text" placeholder. That placeholder exists for a decoration
						// somebody dropped on a form; a bound control is named by what it shows,
						// exactly as a text box is.
						staticText->SetCaption(wxEmptyString);
						staticText->EnableWindow(nextSourceExplorer.IsEnabled());
						staticText->VisibleWindow(nextSourceExplorer.IsVisible());
						staticText->SetSource({ mainAttrId, nextSourceExplorer.GetSourceId() });
					}
					else {

						bool selButton = !nextSourceExplorer.ContainType(ibValueTypes::TYPE_BOOLEAN) &&
							!nextSourceExplorer.ContainType(ibValueTypes::TYPE_NUMBER) &&
							//!nextSourceExplorer.ContainType(ibValueTypes::TYPE_DATE) &&
							!nextSourceExplorer.ContainType(ibValueTypes::TYPE_STRING);

						if (nextSourceExplorer.GetClsidList().size() != 1)
							selButton = true;

						ibValueTextCtrl* textCtrl =
							dynamic_cast<ibValueTextCtrl*>(ibValueForm::CreateControl(wxT("Textctrl")));
						textCtrl->SetControlName(nextSourceExplorer.GetSourceName());
						//textCtrl->SetCaption(nextSourceExplorer.GetSourceSynonym());
						textCtrl->EnableWindow(nextSourceExplorer.IsEnabled());
						textCtrl->VisibleWindow(nextSourceExplorer.IsVisible());
						textCtrl->SetSource({ mainAttrId, nextSourceExplorer.GetSourceId() });

						textCtrl->SetSelectButton(selButton);
						textCtrl->SetOpenButton(false);
						textCtrl->SetClearButton(nextSourceExplorer.IsEnabled());
					}
				}
			}
		}
	}
	else {
		// Form-level toolbar is the form's command-bar chrome (m_commandBar).
	}
}

void ibValueForm::InitializeForm(const ibValueMetaObjectFormBase* creator,
	ibControlFrame* ownerControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid)
{
	if (ownerControl != nullptr) ownerControl->ControlIncrRef();
	if (m_controlOwner != nullptr) m_controlOwner->ControlDecrRef();

	// The source's ref lives in the MAIN attribute wrapper (SetSourceValue → SourceIncrRef, its
	// dtor → SourceDecrRef); during the build the caller's RAII guard (CreateAndBuildForm) keeps it
	// alive. The form holds NO separate ref — SourceIncrRef IS ibValue::IncrRef, so an IncrRef here
	// with no matching DecrRef would just leak.
	m_controlOwner = ownerControl;
	m_metaFormObject = creator;

	m_formKey = CreateFormUniqueKey(ownerControl, srcObject, formGuid);

	if (creator != nullptr)
		m_formType = creator->GetTypeForm();

	// Runtime-tree parent is determined here — srcObject is set only
	// in this method, and the form's metadata + module-manager root
	// are reachable through the creator. Form sits under the bound
	// data-record descriptor (catalog/document/external DP object) if
	// any; otherwise directly under the metadata's root. Subsequent
	// BindVariable / InitializeRuntime pick up this parent to wire
	// their compile / procUnit scope chain on creation.
	ibRuntimeModuleDataObject* sourceDesc =
		dynamic_cast<ibRuntimeModuleDataObject*>(srcObject);

	ibRuntimeModuleDataObject* descParent = sourceDesc;

	if (descParent == nullptr && creator != nullptr) {
		// No bound data object → parent under the metadata's module manager.
		// Through the seam so the Designer (which has no runtime root mm) parents
		// under its lightweight designer manager, same as every other edit-path
		// object. Null-folds for sessionless / no-cache hosts.
		descParent = ibSession::EditModuleManagerFor(creator->GetMetaData());
	}

	if (descParent != nullptr)
		ibRuntimeModuleDataObject::SetParent(descParent);

	// The form ALWAYS has a main attribute — declare it here (the ctor path) so
	// GetMainAttribute() is never null. WITH a source it reflects the source (List/Object
	// name by kind, the source Type, the seated value); WITHOUT one it is a bare default
	// (Object / empty Type) that a later load refills from the "MainAttribute" section.
	if (srcObject != nullptr) {
		// Auto-generated form (no designer form): declare the MAIN attribute the
		// source lands in. With no source the form stays generic (no list/tree
		// view) — that's why this lives under the source gate. Empty Type accepts
		// the incoming source; controls / source explorer work off this attribute.
		// List vs object = the source-class table fact via the factory (IsTableSource), not the
		// explorer flag.
		(void)AddMainAttribute(
			srcObject->IsTableSource() ? wxT("List") : wxT("Object"),
			srcObject->GetSourceClassType(), srcObject);
	}
}

#include "backend/system/systemManager.h"

// Form's meta-object drives lazy compile-module creation via
// BindContextVariable when m_compileModule is not yet wired.
const ibValueMetaObjectModuleBase* ibValueForm::GetMetaForCompile() const
{
	return m_metaFormObject;
}

bool ibValueForm::InitializeFormModule()
{
	// ⭐⭐ THE PERSON'S OWN ARRANGEMENT GOES ON HERE — after the control tree exists and BEFORE the
	// module runs. That order is the point: the author's form is the base, the person's arrangement
	// is laid over it, and the module has the LAST word. A module hides a control because of a right
	// or a value, and a preference saved months ago must not overrule that.
	//
	// Read from the base every time, never cached: the same person may be in another session and
	// have changed it there (frontend/settings/formSettings.h).
	//
	// 🛑 AND IT STANDS OUTSIDE THE `m_metaFormObject` BLOCK, which is where it was first put and
	// where it never ran: a form GENERATED from its source has no metaobject, so that whole block is
	// skipped — and generated forms are exactly the ones this is for. Saving worked (the dialog
	// calls it straight), restoring never happened, and the two are far enough apart that it read as
	// "the setting is not being saved".
	//
	// ⚠ BUT AFTER THE RIGHT TO SEE THE FORM AT ALL, which is why that check is hoisted out of the
	// block below and stands first: a person who may not open this form must not have anything
	// rearranged for them on the way to being refused.
	if (m_metaFormObject != nullptr && !m_metaFormObject->AccessRight_Show()) {
		ibBackendAccessException::Error();
		return false;
	}

	ibRestoreFormSettings(this);

	if (m_metaFormObject != nullptr) {

		// Parent is already wired in InitializeForm(). BindVariable +
		// InitializeRuntime lazily create compile module / ProcUnit
		// and pick up the parent's scope chain on creation. Run is
		// Designer-guarded; Compile internally too. Session linkage
		// flows through the parent chain (descriptor → root → session).
		BindContextVariable(thisForm, this);                                          // contextual
		BindExportVariable(wxT("Controls"), m_formCollectionControl);                 // exported
		// Bind each source attribute as a form-module variable: its value cell as a LOCAL named
		// <attrName>, and — for the MAIN — the exported DataSource. Same self-managed path that
		// designer add / become-main reuse (BindAttributeVariable), so the wiring is one place.
		for (const auto& av : m_attributes)
			BindAttributeVariable(av);

		InitializeRuntime();

		try {
			Compile();
			Run(true);
		}
		catch (const ibCoreException&) {
			if (!appData->DesignerMode())
				throw;
			return false;
		}

		InvalidateNames();
	}

#pragma region _control_guard_

	struct ibControlGuard {

		static bool Initialize(ibValueFrame* controlParent) {
			for (unsigned int idx = controlParent->GetChildCount(); idx > 0; idx--) {
				if (!Initialize(controlParent->GetChild(idx - 1)))
					return false;
			}
			return controlParent->InitializeControl();
		}
	};

	return ibControlGuard::Initialize(this);

#pragma endregion 
}

#include "backend/system/value/valueType.h"

void ibValueForm::NotifyCreate(const ibValue& vCreated)
{
	ibValueForm* ownerForm = m_controlOwner != nullptr ?
		m_controlOwner->GetOwnerForm() : nullptr;

	if (ownerForm != nullptr) {

		ownerForm->m_createdValue = vCreated;

		ownerForm->UpdateForm();
	}

	ibValueForm::UpdateForm();
	ibValueForm::Modify(false);
}

void ibValueForm::NotifyChange(const ibValue& vChanged)
{
	ibValueForm* ownerForm = m_controlOwner != nullptr ?
		m_controlOwner->GetOwnerForm() : nullptr;

	if (ownerForm != nullptr) {

		// A CHANGE ONLY MEANS "RE-READ". No position anchor travels with it any more (see tableBox's OnUpdated):
		// the row already exists and the list re-locates its own current row by row-key. Clearing a PENDING
		// create anchor stays — a save that follows a create must not re-fire the create's positioning.
		ownerForm->m_createdValue = wxEmptyValue;

		ownerForm->UpdateForm();
	}

	ibValueForm::UpdateForm();
	ibValueForm::Modify(false);
}

void ibValueForm::NotifyDelete(const ibValue& vChanged)
{
	ibValueForm* ownerForm = m_controlOwner != nullptr ?
		m_controlOwner->GetOwnerForm() : nullptr;

	if (ownerForm != nullptr) {

		ownerForm->m_createdValue = wxEmptyValue;

		ownerForm->UpdateForm();
	}

	ibValueForm::CloseForm(true);
}

void ibValueForm::NotifyChoice(ibValue& vSelected)
{
	ChoiceDocForm(vSelected);

	if (m_closeOnChoice)
		ibValueForm::CloseForm();
}

#include "backend/system/systemManager.h"

ibValue ibValueForm::CreateControl(const ibValueType* clsControl, const ibValue& vControl)
{
	if (appData->DesignerMode())
		return ibValue();

	if (!ibValue::IsRegisterCtor(clsControl->GetString(), ibCtorObjectType::ibCtorObjectType_object_control)) {
		ibBackendCoreException::Error(_("Error occurred while trying to create a form element!"));
	}

	//get parent obj
	ibValueFrame* parentControl = nullptr;

	if (!vControl.IsEmpty())
		parentControl = CastValue<ibValueFrame>(vControl);
	else
		parentControl = this;

	return ibValueForm::CreateControl(
		clsControl->GetString(),
		parentControl
	);
}

ibValue ibValueForm::FindControl(const ibValue& vControl)
{
	ibValueFrame* foundedControl = FindControlByName(vControl.GetString());
	if (foundedControl != nullptr)
		return foundedControl;
	return ibValue();
}

void ibValueForm::RemoveControl(const ibValue& vControl)
{
	if (appData->DesignerMode())
		return;

	//get parent obj
	ibValueControl* currentControl =
		CastValue<ibValueControl>(vControl);

	wxASSERT(currentControl);
	RemoveControl(currentControl);
}

//*************************************************************************************************
//*                                              Events                                           *
//*************************************************************************************************

bool ibValueForm::ShowForm(ibDocument* docParent, bool createContext)
{
	if (ibBackendException::IsEvalMode())
		return false;

	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();

	if (ownerDocForm != nullptr) {
		ActivateForm();
		return true;
	}

	if (m_controlOwner != nullptr &&
		docParent == nullptr) {
		docParent = m_controlOwner->GetVisualDocument();
	}

	if (!createContext || !appData->DesignerMode()) {
		// Soft-lock UX (docs/private/record-locks.md Phase B.3): try to acquire
		// the long-held sys_lock on the form's source, but DO NOT
		// block form open on conflict. Users can view / edit
		// in-memory even when another session holds the lock; the
		// Write path re-attempts the acquire and fails the save with
		// "X is locked by user Y" if conflict persists at save time.
		// This avoids over-restrictive "form refuses to open" UX
		// while still preventing lost updates.
		if (ibSourceDataObject* const src = GetSourceObject()) {
			try {
				src->TryAcquireFormLock();
			}
			catch (const ibBackendLockException& lockErr) {
				// Conflict — surface the blocking user as a caption badge
				// so the operator knows "view-only" status without
				// attempting to save. Form opens regardless; Write path
				// will re-throw if conflict persists at save time.
				if (lockErr.GetKind() == ibBackendLockException::Kind::LockConflict)
					SetLockBadge(lockErr.GetBlockingUser());
			}
			catch (const ibCoreException&) {
				// Non-conflict lock-infra error (DB transient etc.) —
				// silent. Write path will re-check at save time.
			}
			catch (...) {
				// Defensive — unknown exception, still open form.
			}
		}

		return CreateDocForm(docParent, createContext);
	}

	// Designer preview path — the form is not opened here.
	return false;
}

void ibValueForm::RefreshLockBadge()
{
	if (m_lockBadgeHolder.IsEmpty())
		return;   // not in soft-lock view-only state — nothing to refresh

	ibSourceDataObject* const src = GetSourceObject();
	if (src == nullptr)
		return;

	try {
		src->TryAcquireFormLock();
		// Acquire succeeded — lock is now ours, badge clears.
		m_lockBadgeHolder.clear();
	}
	catch (const ibBackendLockException& err) {
		// Still locked. Holder may have changed (one process released,
		// another took over) — keep the field in sync so UI surfaces
		// the current truth.
		if (err.GetKind() == ibBackendLockException::Kind::LockConflict
			&& !err.GetBlockingUser().IsEmpty()) {
			m_lockBadgeHolder = err.GetBlockingUser();
		}
	}
	catch (...) {
		// Transient DB error — leave badge as-is. Next tick re-tries.
	}
}

void ibValueForm::UpdateForm()
{
	if (ibBackendException::IsEvalMode())
		return;

	// Cross-user notifier tick is the natural pulse for lock-state
	// refresh too. Cheap when badge is empty (early return).
	RefreshLockBadge();

	// The next frame drawn reads the form as it now is — all but the tables' rows, which are fetched: counted, so each
	// table has its client read them again (GetUpdateCount).
	m_updateCount++;
	ibFormVisualDocument* const document = GetVisualDocument();
	if (ibFormVisualEditView* const host = document != nullptr ? document->GetFirstView() : nullptr)
		host->UpdateControl(this);
}

bool ibValueForm::CloseForm(bool force)
{
	if (ibBackendException::IsEvalMode())
		return false;

	if (!appData->DesignerMode()) {
		if (!force && !CloseDocForm()) {
			return false;
		}
	}

	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();

	// A form bound to a HOST document (a cell of the home page) cannot close: the window is
	// the host's, not the form's. ONLY the close is suppressed — everything the command did
	// before reaching here already happened (Save-and-close wrote the object, beforeClose /
	// onClose ran), and the cell keeps the very same form. Nothing is asked, nothing is
	// replaced. force=true (the teardown path) is never suppressed, or the window could not
	// shut down.
	if (!force && ownerDocForm != nullptr && ownerDocForm->IsEmbedded())
		return false;

	if (ownerDocForm != nullptr) {
		// The document goes with its views — but not now: the close may have come from a control of this very
		// form (Save and close), whose call is still on the stack, and DeleteAllViews takes every control with
		// it. The view's frame closes it once that call has unwound. A form without a frame of its own (one
		// composed into its parent, reaching here only under force) goes at once: nothing of it is on the stack.
		// A forced close has nothing to save — its object is gone — so its frame asks nothing; one the person
		// refused («save changes?» answered Cancel) keeps the form.
		if (force)
			ownerDocForm->Modify(false);
		ibFormVisualEditView* const view = ownerDocForm->GetFirstView();
		ibDocChildFrameAnyBase* const frame = view != nullptr ? view->GetDocChildFrame() : nullptr;
		if (frame == nullptr)
			ownerDocForm->DeleteAllViews();
		else if (!frame->Close())
			return false;
	}

	return true;
}

#include "frmserver/win/dlgs/formEditor.h"        // ibDialogFormEditor — the person's own arrangement of the form
#include "frmserver/win/dlgs/generation.h"        // ibDialogGeneration — the object to base a new one on
#include "protocol/protocol.h"       // ibProtocolRequestKind::Help

void ibValueForm::HelpForm()
{
	// ⭐ THE TEXT IS THE OBJECT'S OWN. Every metaobject carries help
	// (ibValueMetaObject::GetHelpContent), written in the designer's tree; this is the reading
	// end of it. Asked of the object the form is FOR — "what is this document" is a question
	// about the document, not about the form showing it.
	const ibValueMetaObjectGenericData* metaObject = GetMetaObject();

	const wxString help = metaObject != nullptr
		? metaObject->GetHelpContent() : wxString(wxEmptyString);

	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return;

	// NOTHING WRITTEN IS AN ANSWER, and a better one than an empty page: an empty page leaves a
	// person wondering whether the help failed to load.
	if (help.IsEmpty()) {
		frame->ShowModalMessage(_("Nothing has been written about this yet."),
			GetControlTitle(), wxOK | wxICON_INFORMATION);
		return;
	}

	// A text to read: the title and the text, read-only — the user's copy of what a developer wrote.
	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibProtocolRequestKind::Help));
	request.SetValue(wxT("Title"), metaObject != nullptr
		? wxString::Format(_("Help: %s"), metaObject->GetSynonym()) : wxString(_("Help")));
	request.SetValue(wxT("Text"), help);

	ibDataNode response;
	frame->Request(request, response);
}

void ibValueForm::ChangeForm()
{
	ibDialogFormEditor dlg(this);
	dlg.ShowModal();
}

bool ibValueForm::GenerateForm(ibValueRecordDataObjectRef* obj) const
{
	const ibValueMetaObjectRecordDataMutableRef* metaObject = obj->GetMetaObject();
	wxASSERT(metaObject);
	const ibMetaData* metaData = metaObject->GetMetaData();
	wxASSERT(metaData);

	ibDialogGeneration selectDataType(metaData, metaObject->GetGenerationDescription());

	ibMetaID sel_id = 0;
	if (selectDataType.ShowModal(sel_id)) {
		const ibValueMetaObjectRecordDataMutableRef* meta = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataMutableRef>(sel_id);
		if (meta != nullptr) {
			const ibValuePtr<ibValueRecordDataObjectRef> genObj = meta->CreateObjectValue(obj, true);
			if (genObj != nullptr) {
				genObj->ShowFormValue();
				return true;
			}
		}
	}
	return false;
}

//**********************************************************************************
//*                                   Other                                        *
//**********************************************************************************

ibValueFrame* ibValueForm::CreateControl(const wxString& clsControl, ibValueFrame* control)
{
	//get parent obj
	ibValueFrame* parentControl = nullptr;

	if (control != nullptr)
		parentControl = control;
	else
		parentControl = this;

	// furthermore, the object is inserted right after the selected object
	ibValueFrame* newControl = ibValueForm::CreateObject(clsControl, parentControl);
	wxASSERT(newControl);
	// Live-tree insertion: on an open form the new control is created by the host that holds the others.
	if (!ibBackendException::IsEvalMode()) {
		ibFormVisualDocument* const ownerDocForm = GetVisualDocument();
		if (ownerDocForm != nullptr) {
			ibFormVisualEditView* const visualView = ownerDocForm->GetFirstView();
			if (visualView != nullptr)
				visualView->CreateControl(newControl);
		}
	}

	// Control added — the next frame drawn has it; both the form's own attribute surface (FillMembers loops
	// GetControlList) and the Controls collection's surface are stale.
	InvalidateNames();
	m_formCollectionControl->InvalidateNames();

	//return value
	if (newControl->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		return newControl->GetChild(0);

	return newControl;
}

void ibValueForm::RemoveControl(ibValueFrame* control)
{
	//get parent obj
	ibValueFrame* currentControl = control;
	wxASSERT(currentControl);
	// Symmetric to CreateControl — on an open form the host cleans the control up first.
	if (!ibBackendException::IsEvalMode()) {
		ibFormVisualDocument* const ownerDocForm = GetVisualDocument();
		if (ownerDocForm != nullptr) {
			ibFormVisualEditView* const visualView = ownerDocForm->GetFirstView();
			if (visualView != nullptr)
				visualView->RemoveControl(currentControl);
		}
	}

	ibValueFrame* parentControl = currentControl->GetParent();

	if (parentControl->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		// The sizer-item wraps currentControl; removing the wrapper from its owner
		// releases the owning handle, cascading down to currentControl.
		ibValueFrame* parentOwner = parentControl->GetParent();
		if (parentOwner != nullptr)
			parentOwner->RemoveChild(parentControl);
	}
	else {
		ibValueFrame* parentOwner = currentControl->GetParent();
		if (parentOwner != nullptr)
			parentOwner->RemoveChild(currentControl); // owning handle releases → destroys
	}

	// Control removed → form attribute surface + Controls collection surface stale.
	InvalidateNames();
	m_formCollectionControl->InvalidateNames();
}

void ibValueForm::RunIdleHandlers()
{
	if (m_procUnit == nullptr)
		return;

	// The due ones are taken first and run after: a handler may attach or detach handlers — itself included —
	// and the map must not change under a walk over it.
	const auto now = std::chrono::steady_clock::now();
	std::vector<wxString> due;
	for (auto& pair : m_idleHandlerArray) {
		if (pair.second.due > now)
			continue;
		due.push_back(pair.first);
		pair.second.due = now + pair.second.interval;
	}
	for (const wxString& procedureName : due) {
		auto it = m_idleHandlerArray.find(procedureName);
		if (it == m_idleHandlerArray.end())
			continue;   // detached by a handler that ran before it
		if (it->second.single)
			m_idleHandlerArray.erase(it);
		CallAsEvent(procedureName);
	}

	// A handler ran — what it changed nobody asked for: the client is told its frame changed (RefreshFrame).
	if (!due.empty()) {
		if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
			frame->RefreshFrame();
	}
}

void ibValueForm::AttachIdleHandler(const wxString& procedureName, int interval, bool single)
{
	if (appData->DesignerMode())
		return;

	for (unsigned int i = 0; i < procedureName.length(); i++) {
		if (!((procedureName[i] >= 'A' && procedureName[i] <= 'Z') || (procedureName[i] >= 'a' && procedureName[i] <= 'z') ||
			(procedureName[i] >= L'\u0410' && procedureName[i] <= L'\u042F') || (procedureName[i] >= L'\u0430' && procedureName[i] <= L'\u044F') ||
			(procedureName[i] >= '0' && procedureName[i] <= '9')))
		{
			ibBackendCoreException::Error(_("Procedure can enter only numbers, letters and the symbol \"_\""));
			return;
		}
	}

	// Recorded, not started: the client's tick runs it when it is due (RunIdleHandlers). Attaching one that is
	// attached already changes nothing, as it did when each was a timer of its own.
	if (m_procUnit != nullptr && m_procUnit->FindMethod(procedureName, true) && interval > 0) {
		if (m_idleHandlerArray.find(procedureName) == m_idleHandlerArray.end()) {
			const std::chrono::seconds every(interval);
			m_idleHandlerArray.emplace(procedureName,
				ibIdleHandler{ every, single, std::chrono::steady_clock::now() + every });
		}
	}
}

void ibValueForm::DetachIdleHandler(const wxString& procedureName)
{
	if (appData->DesignerMode())
		return;

	for (unsigned int i = 0; i < procedureName.length(); i++) {
		if (!((procedureName[i] >= 'A' && procedureName[i] <= 'Z') || (procedureName[i] >= 'a' && procedureName[i] <= 'z') ||
			(procedureName[i] >= L'\u0410' && procedureName[i] <= L'\u042F') || (procedureName[i] >= L'\u0430' && procedureName[i] <= L'\u044F') ||
			(procedureName[i] >= '0' && procedureName[i] <= '9')))
		{
			ibBackendCoreException::Error(_("Procedure can enter only numbers, letters and the symbol \"_\""));
			return;
		}
	}

	if (m_procUnit != nullptr && m_procUnit->FindMethod(procedureName, true))
		m_idleHandlerArray.erase(procedureName);
}