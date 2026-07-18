////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame control
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "formAttribute.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/metaCollection/partial/commonObject.h"
#ifdef OES_USE_WEB
// ibWebTimer full type needed for the dtor's delete in the idle-handler
// cleanup loop — ibFrontendTimer resolves to ibWebTimer on this build.
#include "frontend/web/webTimer.h"
#else
#include <wx/timer.h>
#endif

#ifdef OES_USE_WEB
#include <iostream>
#endif


//****************************************************************************
//*                              Frame                                       *
//****************************************************************************

ibValueForm::ibValueForm(const ibValueMetaObjectFormBase* creator, ibControlFrame* ownerControl,
	ibSourceDataObject* srcObject, const ibUniqueKey& formGuid) : ibValueFrame(),
	ibRuntimeModuleDataObject(m_members, this),
	m_controlOwner(nullptr), m_metaFormObject(nullptr),
	m_formCollectionControl(new ibValueFormCollectionControl(this)),
	m_formType(defaultFormType), m_closeOnChoice(true), m_closeOnOwnerClose(true), m_formModified(false)
{
	// Frame surface (properties + Events) comes from ibValueFrame::FillMembers, bound
	// by the base ctor. The form ADDS its own members on top (FillFormMembers); module
	// exports autobind as the helper's tail (ibRuntimeModuleDataObject ctor).
	m_members.Bind(this, &ibValueForm::FillFormMembers);

	// This control carries a command bar — the frame owns its STORE and points it
	// back at itself; the visual host reads it to render the toolbar separately.
	m_commandBar = new ibValueCommandBar();
	m_commandBar->SetOwner(this);

	//init default params
	ibValueForm::InitializeForm(creator, ownerControl, srcObject, formGuid);

	//set default params
	m_controlId = defaultFormId;
}

ibValueForm::~ibValueForm()
{
	// Idle-handler timers unified via ibFrontendTimer typedef + shared_ptr
	// ownership (wxTimer on desktop, ibWebTimer on web). Stop + Unbind
	// synchronously; the shared_ptr's dtor finishes the disposal when
	// the map destructs below.
	for (auto& pair : m_idleHandlerArray) {
		auto& timer = pair.second;
		if (!timer) continue;
		if (timer->IsRunning()) timer->Stop();
		timer->Unbind(wxEVT_TIMER, &ibValueForm::OnIdleHandler, this);
	}

	// Child controls are released by the ibPropertyObjectHelper base cascade (and the
	// m_formCollectionControl member). Control teardown no longer touches form state —
	// SetOwnerForm just clears m_formOwner now (no m_listControl set to erase from), so
	// the post-member-destruction order is safe; no explicit teardown needed here.

	if (m_controlOwner != nullptr) m_controlOwner->ControlDecrRef();
	// The source's ref is released by the MAIN attribute wrapper's dtor (SourceDecrRef), not here.
	// sys_lock release intentionally tied to source LIFETIME, not form
	// lifetime — m_formLockHandle on the source RAII-DELETEs the row
	// when the source's last ref drops (form's DecrRef above is one
	// such ref; scripts holding the value contribute others). While
	// any holder is alive the source is still "in use" by someone, so
	// keeping the lock matches user-visible semantics. See
	// docs/record-locks.md Phase B.3.
}

void ibValueForm::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	UpdateForm();
}

void ibValueForm::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	// Parent-layout pass — Web has no live wxWindow to re-layout; the
	// browser handles it on the next JSON response.
	wxWindow* wndParent = visualHost->GetParent();
	if (wndParent) {
		wndParent->Layout();
	}
#endif
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool ibValueForm::ReadData(const ibDataNode& node)
{
	m_propertyTitle->ReadNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyOrient->ReadNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	m_propertyFG->ReadNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->ReadNodeValue(node.GetProperty(m_propertyBG->GetName()));
	m_propertyEnabled->ReadNodeValue(node.GetProperty(m_propertyEnabled->GetName()));

	if (!ReadAttributes(node))
		return false;
	return ibValueFrame::ReadData(node);
}

bool ibValueForm::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());

	if (!WriteAttributes(node))
		return false;
	return ibValueFrame::WriteData(node);
}

// Copy/paste carries ONLY the attribute store — the form's own properties (Title/Orient/…)
// already ride the generic property walk in Copy/PasteNode, so re-emitting them here (as
// WriteData does) would double-write. Attributes are the one form-level datum that walk skips.
bool ibValueForm::CopyData(ibDataNode& node) const
{
	return WriteAttributes(node);
}

bool ibValueForm::PasteData(const ibDataNode& node)
{
	return ReadAttributes(node);
}


//**********************************************************************************
//*                                   Other                                        *
//**********************************************************************************

ibSourceDataObject* ibValueForm::GetSourceObject() const
{
	// Prefer the source held by the MAIN attribute (the owner). The source
	// object passed on open was assigned into it (InitializeForm); controls and
	// their dot-walk read it from here. Fall back to the legacy field when no
	// main attribute holds a source (sourceless / designer / not yet assigned).
	if (ibFormAttributeValue* mainAttr = GetMainAttribute()) {
		return mainAttr->GetSourceValue();
	}

	return nullptr;
}

const ibMetaData* ibValueForm::GetMetaData() const
{
	// The form metaobject (creator) is the SKELETON — its config is authoritative, so consult it FIRST. The
	// SOURCE object provides the metadata ONLY as a fallback, when the form has NO metaobject (a dynamically-
	// built form with no skeleton). Order matters: asking the SOURCE first recursed — a form-local dynamic-list
	// source resolves its OWN metadata back THROUGH this form (attach-owner holder → attribute → owner form →
	// here), so source-first looped GetMetaData → GetSourceMetaData → … → stack overflow. Metaobject-first
	// short-circuits that for every form that has a creator (which is all metadata-defined forms).
	if (m_metaFormObject != nullptr)
		return m_metaFormObject->GetMetaData();

	const ibSourceDataObject* sourceObject = GetSourceObject();
	return sourceObject != nullptr ? sourceObject->GetSourceMetaData() : nullptr;
}

ibFormID ibValueForm::GetTypeForm() const
{
	return m_metaFormObject != nullptr ?
		m_metaFormObject->GetTypeForm() :
		m_formType;
}

bool ibValueForm::IsEditable() const
{
	const ibSourceDataObject* sourceObject = GetSourceObject();

	if (sourceObject != nullptr) {
		const ibValueMetaObject* metaObject = sourceObject->GetSourceMetaObject();
		if (metaObject != nullptr)
			return metaObject->IsEditable();
	}

	return m_metaFormObject != nullptr ?
		m_metaFormObject->IsEditable() :
		true;
}

bool ibValueForm::IsViewOnly() const
{
	if (m_viewOnly)
		return true;   // opened explicitly view-only

	// The OUTER matryoshka shell, TWO gates from outer to inner. AccessRight_Modify is the GENERIC "can change"
	// predicate (twin of AccessRight_Show), read polymorphically; either gate denying makes the whole form
	// view-only (browse / open / copy, no edit). The finer PER-SOURCE right (a writable form carrying a
	// read-only NON-main object) lives in IsWritableBinding, keyed on each control's own binding.
	//
	// Gate 1 — the FORM metaobject (the metaobject that spawned this form). It may be absent (a form built
	// straight from a source); then fall through to the source.
	if (m_metaFormObject != nullptr && !m_metaFormObject->AccessRight_Modify())
		return true;

	// Gate 2 — the MAIN source's metaobject. A record maps Modify to its Write role; an object with no modify
	// concept (data processor / report) stays true (never gated).
	const ibSourceDataObject* sourceObject = GetSourceObject();
	if (sourceObject != nullptr) {
		const ibValueMetaObjectGenericData* metaObject = sourceObject->GetSourceMetaObject();
		if (metaObject != nullptr && !metaObject->AccessRight_Modify())
			return true;
	}
	return false;
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

enum Prop {
	eThisForm = 0,
	eControls,
	eDataSource,
	eModified,
	eFormOwner,
	eUniqueKey,
	eCloseOnChoice,
	eCloseOnOwnerClose,
	eReadOnly
};

enum Func
{
	enShow = 0,
	enActivate,
	enUpdate,
	enClose,
	enIsShown,
	enAttachIdleHandler,
	enDetachIdleHandler,

	enNotifyChoice,
};

void ibValueForm::FillFormMembers(ibMemberTable& helper) const
{
	// Properties (eProperty) + Events come from ibValueFrame::FillMembers (base bind).
	// Here the form adds only its OWN members.
	// ThisForm / Controls / DataSource are BOUND in formObject (context / export).
	// The bind is the single source; surfacing to editor + runtime is the
	// infrastructure's job — no manual AppendProp here.
	helper.AppendProp(wxT("Modified"), eModified, eSystem);
	helper.AppendProp(wxT("FormOwner"), eFormOwner, eSystem);
	helper.AppendProp(wxT("UniqueKey"), eUniqueKey, eSystem);

	helper.AppendProp(wxT("CloseOnChoice"), eCloseOnChoice, eSystem);
	helper.AppendProp(wxT("CloseOnOwnerClose"), eCloseOnOwnerClose, eSystem);
	helper.AppendProp(wxT("ReadOnly"), eReadOnly, eSystem);   // runtime read/write — open a form read-only (or read the mode)

	helper.AppendProc(wxT("Show"), wxT("Show()"));
	helper.AppendProc(wxT("Activate"), wxT("Activate()"));
	helper.AppendProc(wxT("Update"), wxT("Update()"));
	helper.AppendProc(wxT("Close"), wxT("Close()"));
	helper.AppendFunc(wxT("IsShown"), wxT("IsShown()"));
	helper.AppendProc(wxT("AttachIdleHandler"), 3, wxT("AttachIdleHandler(procedureName : string, interval : number, single : boolean)"));
	helper.AppendProc(wxT("DetachIdleHandler"), 1, wxT("DetachIdleHandler(procedureName : string)"));
	helper.AppendProc(wxT("NotifyChoice"), 1, wxT("NotifyChoice(value)"));

	// Module exports AND the context binds (ThisForm.Controls / DataSource) are
	// surfaced as the helper's tail by the descriptor's ExportThunk (autobind under
	// eProcUnit == g_aliasExport) — NOT here. Doing it manually made an unguarded
	// virtual GetCompileModule() on a possibly half-alive `this` (crash on teardown /
	// debugger-thread lazy build); the descriptor thunk is dynamic_cast-guarded.

	for (auto control : GetControlList()) {
		if (!control->HasValueInControl())
			continue;

		helper.AppendProp(
			control->GetControlName(),
			control->GetControlID(),
			eAttribute
		);
	}

	// Form source attributes — ThisForm.<attrName>. Data = the attribute's STABLE id (not the
	// store index): the index shifts on delete, which would make ThisForm.<attr> resolve to a
	// different attribute (controls bind by GetControlID for the same reason).
	for (const auto& av : m_attributes) {
		helper.AppendProp(
			av->GetName(),
			av->GetId(),
			eFormAttribute
		);
	}
}

bool ibValueForm::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(GetPropName(lPropNum), varPropVal);
		}
	}
	else if (lPropAlias == eProperty || lPropAlias == eEvent) {
		return ibValueFrame::SetPropVal(lPropNum, varPropVal);
	}
	else if (lPropAlias == eSystem) {
		switch (m_members.GetPropData(lPropNum)) {
		case eModified:
			Modify(varPropVal.GetBoolean());
			return true;
		case eCloseOnChoice:
			m_closeOnChoice = varPropVal.GetBoolean();
			return true;
		case eCloseOnOwnerClose:
			m_closeOnOwnerClose = varPropVal.GetBoolean();
			return true;
		case eReadOnly:
			SetViewOnly(varPropVal.GetBoolean());
			RefreshForm();   // re-render live — controls re-Update (re-read writability) and command bars rebuild,
			                 // so a runtime ThisForm.ReadOnly = True takes effect WITHOUT reopening the form
			return true;
		}
	}
	else if (lPropAlias == eAttribute) {
		unsigned int id = m_members.GetPropData(lPropNum);
		const std::vector<ibValueControl*> list = GetControlList();
		auto it = std::find_if(list.begin(), list.end(),
			[id](const ibValueFrame* control) {
				return id == control->GetControlID();
			}
		);
		if (it != list.end())
			return (*it)->SetControlValue(varPropVal);
	}
	else if (lPropAlias == eFormAttribute) {
		if (ibFormAttributeValue* attr = FindAttributeById(m_members.GetPropData(lPropNum))) {
			attr->SetValue(varPropVal);
			return true;
		}
	}
	return false;
}

bool ibValueForm::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr &&
			m_procUnit->GetPropVal(GetPropName(lPropNum), pvarPropVal))
			return true;
		// Bound handle (Controls / DataSource) — resolve the live bind value directly. Works in the
		// Designer (no ProcUnit) and as a runtime fallback.
		//
		// The bound cell is an EMBEDDED member, NOT a heap object with its own ref count: ThisForm =
		// the form itself; Controls = m_formCollectionControl; DataSource / <attr> = &m_value of the
		// attribute wrapper. Its ref count is 0. So take `bound` as a CONST pointer — the assignment
		// then binds a NON-owning reference (operator=(const ibValue*) -> TYPE_CONST_REFFER). An owning
		// ibValue* (IncrRef now, DecrRef -> delete this on destruct) would drive the member to 0 and
		// `delete` a member pointer, corrupting the heap (crash: expanding thisForm.DataSource in the
		// debugger watch).
		if (const ibValue* bound = GetBoundValue(GetPropName(lPropNum))) {
			pvarPropVal = bound;
			return true;
		}
	}
	else if (lPropAlias == eProperty || lPropAlias == eEvent) {
		// Properties + the Events container come from ibValueFrame's surface.
		return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
	}
	else if (lPropAlias == eSystem) {
		switch (m_members.GetPropData(lPropNum))
		{
		case eModified:
			pvarPropVal = IsModified();
			return true;
		case eFormOwner:
			pvarPropVal = dynamic_cast<ibValue*>(m_controlOwner);
			return true;
		case eUniqueKey:
			pvarPropVal = new ibValueGuid(m_formKey);
			return true;
		case eCloseOnChoice:
			pvarPropVal = m_closeOnChoice;
			return true;
		case eCloseOnOwnerClose:
			pvarPropVal = m_closeOnOwnerClose;
			return true;
		case eReadOnly:
			pvarPropVal = IsViewOnly();   // reflects the explicit flag OR the rights-derived mode
			return true;
		}
	}
	else if (lPropAlias == eAttribute) {
		unsigned int id = m_members.GetPropData(lPropNum);
		const std::vector<ibValueControl*> list = GetControlList();
		auto it = std::find_if(list.begin(), list.end(),
			[id](const ibValueFrame* control) {
				return id == control->GetControlID();
			}
		);
		if (it != list.end())
			return (*it)->GetControlValue(pvarPropVal);
	}
	else if (lPropAlias == eFormAttribute) {
		if (ibFormAttributeValue* attr = FindAttributeById(m_members.GetPropData(lPropNum))) {
			attr->GetValue(pvarPropVal);
			return true;
		}
	}

	return false;
}

bool ibValueForm::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enShow: ShowForm();
		return true;
	case enActivate: ActivateForm();
		return true;
	case enUpdate: UpdateForm();
		return true;
	case enAttachIdleHandler: AttachIdleHandler(paParams[0]->GetString(), paParams[1]->GetInteger(), paParams[2]->GetBoolean());
		return true;
	case enDetachIdleHandler: DetachIdleHandler(paParams[0]->GetString());
		return true;
	case enNotifyChoice:
		NotifyChoice(*paParams[0]);
		return true;
	}

	return ibRuntimeModuleDataObject::ExecAsProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool ibValueForm::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enClose:
		pvarRetValue = CloseForm();
		return true;
	case enIsShown:
		pvarRetValue = IsShown();
		return true;
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueForm, "ClientForm", "Form", g_controlFormCLSID);