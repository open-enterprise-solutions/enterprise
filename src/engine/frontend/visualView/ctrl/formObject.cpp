////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame object
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "formAttribute.h"
#include "backend/appData.h"
#include "backend/metaData.h"
#include "frontend/docView/docView.h"
#include "backend/srcExplorer.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/session/session.h"
#include "frontend/visualView/visualHostClient.h"
#ifdef OES_USE_WEB
// ibWebTimer full type for `new ibFrontendTimer()` in AttachIdleHandler.
#include "frontend/web/webTimer.h"
#else
#include <wx/timer.h>
#endif

//*************************************************************************************************
//*                                    System attribute                                           *
//*************************************************************************************************

// BuildForm now runs on both builds — both control families are needed.
// tableBox.h compiles cleanly under OES_USE_WEB (wx-heavy includes are
// already ifdef'd inside it).
#include "toolBar.h"
#include "tableBox.h"
#ifdef OES_USE_WEB
#include "frontend/web/webApplication.h"
#include "frontend/web/webFrame.h"
#include "backend/backend_mainFrame.h"
#endif

void ibValueForm::BuildForm(const ibFormID& formType)
{
	// Auto-build runs on both desktop and web. Desktop renders the full
	// wxDataView tablebox; web emits a placeholder via ibWebStubControl
	// (tableBox.cpp web stub block). Toolbar + Tool + ToolSeparator +
	// Checkbox + Textctrl all have real web renderers — only the
	// tablebox/column visuals are pending.
	m_formType = formType;

	ibFormAttributeValue* mainAttr = GetMainAttribute();
	const ibSourceDataObject* sourceObject = mainAttr != nullptr ? mainAttr->GetSourceValue() : nullptr;

	if (sourceObject != nullptr) {

		// Everything binds THROUGH the main attribute (the gate): control source
		// paths start with its id, then walk the metadata. The incoming source
		// object is only used to lay the controls out, then copied into the main
		// attribute (InitializeForm) and forgotten — reads go via the attribute.
		const ibMetaID mainAttrId = mainAttr->GetAttributeId();

		ibValue* prevSrcData = nullptr;

		ibValueToolbar* mainToolBar =
			dynamic_cast<ibValueToolbar*>(ibValueForm::CreateControl(wxT("Toolbar")));

		mainToolBar->SetControlName(wxT("MainToolbar"));
		mainToolBar->SetActionSrc(FORM_ACTION);

		const ibValueMetaObjectGenericData* metaObjectValue = sourceObject->GetSourceMetaObject();

		ibValueModelTableBox* mainTableBox = nullptr;

		const ibActionCollection& actionData = ibValueForm::GetActionCollection(formType);
		for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
			const ibActionID& action_id = actionData.GetID(idx);
			if (action_id != wxNOT_FOUND) {
				ibValue* currSrcData = actionData.GetSourceDataByID(action_id);
				if (currSrcData != prevSrcData
					&& prevSrcData != nullptr) {
					ibValueForm::CreateControl(wxT("ToolSeparator"), mainToolBar);
				}
				ibValueToolBarItem* toolBarItem =
					dynamic_cast<ibValueToolBarItem*>(ibValueForm::CreateControl(wxT("Tool"), mainToolBar));
				toolBarItem->SetControlName(mainToolBar->GetControlName() + actionData.GetNameByID(action_id));
				//toolBarItem->SetCaption(actionData.GetCaptionByID(action_id));
				//toolBarItem->SetToolTip(actionData.GetCaptionByID(action_id));
				toolBarItem->SetAction(action_id);
				prevSrcData = currSrcData;
			}
			else {
				ibValueForm::CreateControl(wxT("ToolSeparator"), mainToolBar);
			}
		}

		const ibSourceExplorer& sourceExplorer = sourceObject->GetSourceExplorer();

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
			// The MAIN attribute IS the list (its Type is CatalogList.<X>) — its source is
			// just the attribute itself, shown as "List". The extra source-id hop (the row
			// catalog) was redundant here and rendered "List.Catalog1".
			mainTableBox->SetSource({ mainAttrId });
		}

		for (unsigned int idx = 0; idx < sourceExplorer.GetHelperCount(); idx++) {

			const ibSourceExplorer& nextSourceExplorer = sourceExplorer.GetHelper(idx);

			if (isTableSource) {
				ibValueModelTableBoxColumn* tableBoxColumn =
					dynamic_cast<ibValueModelTableBoxColumn*>(ibValueForm::CreateControl(wxT("TableboxColumn"), mainTableBox));
				tableBoxColumn->SetControlName(mainTableBox->GetControlName() + nextSourceExplorer.GetSourceName());
				tableBoxColumn->SetVisibleColumn(nextSourceExplorer.IsVisible() || sourceExplorer.GetHelperCount() == 1);
				// Column = [mainAttr, field] → "List.Field". The row-type hop (the catalog/document)
				// is implicit in the list-typed main attribute — no "List.Document1.Field".
				tableBoxColumn->SetSource({ mainAttrId, nextSourceExplorer.GetSourceId() });
			}
			else
			{
				prevSrcData = nullptr;

				if (nextSourceExplorer.IsTableSection()) {

					ibValueToolbar* toolBar =
						dynamic_cast<ibValueToolbar*>(ibValueForm::CreateControl(wxT("Toolbar")));

					toolBar->SetControlName(wxT("Toolbar") + nextSourceExplorer.GetSourceName());

					ibValueModelTableBox* tableBox =
						dynamic_cast<ibValueModelTableBox*>(ibValueForm::CreateControl(wxT("Tablebox")));

					tableBox->SetControlName(nextSourceExplorer.GetSourceName());
					tableBox->SetSource({ mainAttrId, nextSourceExplorer.GetSourceId() });

					toolBar->SetActionSrc(tableBox->GetControlID());

					ibActionCollection actionData = tableBox->GetActionCollection(formType);
					for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
						const ibActionID& action_id = actionData.GetID(idx);
						if (action_id != wxNOT_FOUND) {
							ibValue* currSrcData = actionData.GetSourceDataByID(action_id);
							if (currSrcData != prevSrcData
								&& prevSrcData != nullptr) {
								ibValueForm::CreateControl(wxT("ToolSeparator"), toolBar);
							}
							ibValueToolBarItem* toolBarItem =
								dynamic_cast<ibValueToolBarItem*>(ibValueForm::CreateControl(wxT("Tool"), toolBar));
							toolBarItem->SetControlName(toolBar->GetControlName() + actionData.GetNameByID(action_id));
							//toolBarItem->SetCaption(actionData.GetCaptionByID(action_id));
							//toolBarItem->SetToolTip(actionData.GetCaptionByID(action_id));
							toolBarItem->SetAction(action_id);
							prevSrcData = currSrcData;
						}
						else {
							ibValueForm::CreateControl(wxT("ToolSeparator"), toolBar);
						}
					}

					for (unsigned int col = 0; col < nextSourceExplorer.GetHelperCount(); col++) {
						const ibSourceExplorer& colSourceExplorer = nextSourceExplorer.GetHelper(col);

						ibValueModelTableBoxColumn* tableBoxColumn =
							dynamic_cast<ibValueModelTableBoxColumn*>(ibValueForm::CreateControl(wxT("TableboxColumn"), tableBox));
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

		ibValueToolbar* mainToolBar =
			dynamic_cast<ibValueToolbar*>(ibValueForm::CreateControl(wxT("Toolbar")));

		mainToolBar->SetControlName(wxT("MainToolbar"));
		mainToolBar->SetActionSrc(FORM_ACTION);

		ibValueModelTableBox* mainTableBox = nullptr;

		const ibActionCollection& actionData = ibValueForm::GetActionCollection(formType);
		for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {

			const ibActionID& id = actionData.GetID(idx);

			if (id != wxNOT_FOUND) {
				ibValueToolBarItem* toolBarItem =
					dynamic_cast<ibValueToolBarItem*>(ibValueForm::CreateControl(wxT("Tool"), mainToolBar));
				toolBarItem->SetControlName(mainToolBar->GetControlName() + actionData.GetNameByID(id));
				//toolBarItem->SetCaption(actionData.GetCaptionByID(id));
				//toolBarItem->SetToolTip(actionData.GetCaptionByID(id));
				toolBarItem->SetAction(id);
			}
			else {
				ibValueForm::CreateControl(wxT("ToolSeparator"), mainToolBar);
			}
		}
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
	if (m_metaFormObject != nullptr) {

		if (!m_metaFormObject->AccessRight_Show()) {
			ibBackendAccessException::Error();
			return false;
		}

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
		catch (const ibBackendException&) {
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
		ownerForm->m_changedValue = wxEmptyValue;

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

		ownerForm->m_createdValue = wxEmptyValue;
		ownerForm->m_changedValue = vChanged;

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
		ownerForm->m_changedValue = wxEmptyValue;

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

void ibValueForm::ShowForm(ibBackendMetaDocument* doc, bool createContext)
{
	ibDocument* docParent = static_cast<ibMetaDocument*>(doc);

	if (ibBackendException::IsEvalMode())
		return;

	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();

	if (ownerDocForm != nullptr) {
		ActivateForm();
		return;
	}

	if (m_controlOwner != nullptr &&
		doc == nullptr) {
		docParent = m_controlOwner->GetVisualDocument();
	}

	if (!createContext || !appData->DesignerMode()) {
		// Soft-lock UX (docs/record-locks.md Phase B.3): try to acquire
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
			catch (const ibBackendException&) {
				// Non-conflict lock-infra error (DB transient etc.) —
				// silent. Write path will re-check at save time.
			}
			catch (...) {
				// Defensive — unknown exception, still open form.
			}
		}

		CreateDocForm(docParent, createContext);
	}
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

	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();

	if (ownerDocForm != nullptr) {

		ibVisualHostClient* visualView = ownerDocForm->GetFirstView() ?
			ownerDocForm->GetFirstView()->GetVisualHost() : nullptr;

		if (visualView != nullptr) {
#ifndef OES_USE_WEB
			// Freeze/Thaw suppress wxWindow repaints during the rebuild.
			// Web build serialises a fresh JSON tree on every request,
			// so there's no mid-render flicker to hide — call the host
			// walker directly.
			visualView->Freeze();
#endif
			visualView->UpdateVisualHost();
#ifndef OES_USE_WEB
			visualView->Thaw();
#endif
		}
	}

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

	if (ownerDocForm != nullptr) {
#ifdef OES_USE_WEB
		// Defer the ibDocument::DeleteAllViews — it would delete the
		// view, host, AND every control (including the toolbar that
		// just fired the OnTool we're in). Mark the tab; the
		// session's Dispatch epilogue drains pending closes AFTER
		// the wxEvent chain unwinds.
		if (auto* webFrame = dynamic_cast<ibWebFrame*>(ibSession::CurrentFrame())) {
			webFrame->MarkTabForCloseByForm(this);
		}
		return true;
#else
		// Same hazard on desktop — ibDocument::DeleteAllViews deletes
		// the view (a wxEvtHandler) plus every control synchronously.
		// If CloseForm was invoked from within the toolbar's tool
		// event (Save-and-close command), control returns to
		// wxAuiToolBar::OnLeftUp on freed memory → UAF in
		// wxEvtHandler::TryHereOnly. Defer the deletion through
		// CallAfter so the click event fully unwinds first.
		ownerDocForm->CallAfter([doc = ownerDocForm] { doc->DeleteAllViews(); });
		return true;
#endif
	}

	return true;
}

void ibValueForm::HelpForm()
{
#ifndef OES_USE_WEB
	// Modal message box — desktop-only. Web would surface help through
	// an HTTP response instead; wiring is deferred.
	wxMessageBox(
		_("Help will appear here sometime, but not today.")
	);
#endif
}

#ifndef OES_USE_WEB
#include "frontend/win/dlgs/formEditor.h"
#endif

void ibValueForm::ChangeForm()
{
#ifndef OES_USE_WEB
	// Designer-mode modal form-editor dialog. No web counterpart — the
	// designer doesn't run in wfrontend.dll.
	ibDialogFormEditor dlg(this);
	dlg.ShowModal();
#endif
}

#ifndef OES_USE_WEB
#include "frontend/win/dlgs/generation.h"
#endif

bool ibValueForm::GenerateForm(ibValueRecordDataObjectRef* obj) const
{
#ifdef OES_USE_WEB
	(void)obj;
	return false;
#else
	const ibValueMetaObjectRecordDataMutableRef* metaObject = obj->GetMetaObject();
	wxASSERT(metaObject);
	const ibMetaData* metaData = metaObject->GetMetaData();
	wxASSERT(metaData);

	ibDialogGeneration* selectDataType = new ibDialogGeneration(metaData, metaObject->GetGenerationDescription());

	ibMetaID sel_id = 0;
	if (selectDataType->ShowModal(sel_id)) {
		const ibValueMetaObjectRecordDataMutableRef* meta = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataMutableRef>(sel_id);
		if (meta != nullptr) {
			ibValueRecordDataObjectRef* genObj = meta->CreateObjectValue(obj, true);
			if (genObj != nullptr) {
				genObj->ShowFormValue();
				selectDataType->Destroy();
				return true;
			}

		}
		selectDataType->Destroy();
		return false;
	}
	selectDataType->Destroy();
	return false;
#endif
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
	// Live-tree insertion: feed the new ibValueFrame into the host.
	// Desktop's CreateControl does the incremental wx-tree edit; web's
	// is a no-op and the next HTTP response rebuilds. Shared call site.
	if (!ibBackendException::IsEvalMode()) {
		ibFormVisualDocument* const ownerDocForm = GetVisualDocument();
		if (ownerDocForm != nullptr) {
			ibVisualHostClient* visualView = ownerDocForm->GetFirstView() ?
				ownerDocForm->GetFirstView()->GetVisualHost() : nullptr;
			if (visualView != nullptr)
				visualView->CreateControl(newControl);
		}
	}

	// Control added → both the form's own attribute surface (FillMembers loops
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
	// Symmetric to CreateControl — desktop does the wx-tree removal,
	// web is a no-op and the next HTTP response rebuilds.
	if (!ibBackendException::IsEvalMode()) {
		ibFormVisualDocument* const ownerDocForm = GetVisualDocument();
		if (ownerDocForm != nullptr) {
			ibVisualHostClient* visualView = ownerDocForm->GetFirstView() ?
				ownerDocForm->GetFirstView()->GetVisualHost() : nullptr;
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

void ibValueForm::OnIdleHandler(wxTimerEvent& event)
{
	if (m_procUnit != nullptr) {
		// Upcast the map's shared_ptr<ibFrontendTimer>::get() to wxObject*
		// for the comparison — event.GetEventObject() returns wxObject*
		// and both wxTimer / ibWebTimer derive from wxObject, so the
		// upcast is well-defined. Body lives here (not inline in the
		// header) so the complete type of ibWebTimer is visible on
		// the web build.
		auto iterator = std::find_if(m_idleHandlerArray.begin(), m_idleHandlerArray.end(),
			[&event](const auto& pair) {
				return static_cast<wxObject*>(pair.second.get()) == event.GetEventObject();
			}
		);

		if (iterator != m_idleHandlerArray.end())
			CallAsEvent(iterator->first);
	}

	event.Skip();
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

	// Unified body across desktop and web via ibFrontendTimer typedef.
	// Desktop: wxTimer fires inside wxApp's main loop; web: ibWebTimer's
	// std::thread + PostWork drives ProcessPendingEvents on the session
	// worker. Either way the wxEVT_TIMER dispatches to the same
	// ibValueForm::OnIdleHandler, which matches event.GetEventObject()
	// against m_idleHandlerArray entries — one code path, two tick
	// sources.
	if (m_procUnit != nullptr && m_procUnit->FindMethod(procedureName, true)) {
		auto it = m_idleHandlerArray.find(procedureName);
		if (it == m_idleHandlerArray.end()) {
			auto timer = std::make_shared<ibFrontendTimer>();
			timer->Bind(wxEVT_TIMER, &ibValueForm::OnIdleHandler, this);
			if (timer->Start(interval * 1000, single))
				m_idleHandlerArray.insert_or_assign(procedureName, timer);
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

	// Unified teardown — both wxTimer and ibWebTimer expose Stop + Unbind;
	// shared_ptr's dtor (on map erase) disposes the timer after we've
	// stopped its thread / message-pump hook.
	if (m_procUnit != nullptr && m_procUnit->FindMethod(procedureName, true)) {
		auto it = m_idleHandlerArray.find(procedureName);
		if (it != m_idleHandlerArray.end()) {
			std::shared_ptr<ibFrontendTimer> timer = it->second;
			m_idleHandlerArray.erase(it);
			if (timer && timer->IsRunning())
				timer->Stop();
			if (timer)
				timer->Unbind(wxEVT_TIMER, &ibValueForm::OnIdleHandler, this);
		}
	}
}