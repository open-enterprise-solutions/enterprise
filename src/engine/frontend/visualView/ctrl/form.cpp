////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame control
////////////////////////////////////////////////////////////////////////////

#include "form.h"
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
	m_controlOwner(nullptr), m_sourceObject(nullptr), m_metaFormObject(nullptr),
	m_formCollectionControl(new ibValueFormCollectionControl(this)),
	m_formType(defaultFormType), m_closeOnChoice(true), m_closeOnOwnerClose(true), m_formModified(false)
{
	// Frame surface (properties + Events) comes from ibValueFrame::FillMembers, bound
	// by the base ctor. The form ADDS its own members on top (FillFormMembers); module
	// exports autobind as the helper's tail (ibRuntimeModuleDataObject ctor).
	m_members.Bind(this, &ibValueForm::FillFormMembers);

	//init default params
	ibValueForm::InitializeForm(creator, ownerControl, srcObject, formGuid);

	//set default params
	m_controlId = defaultFormId;
}

ibValueForm::~ibValueForm()
{
#ifdef OES_USE_WEB
	std::cerr << "[life] ~ibValueForm " << this << std::endl;
#endif
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
	if (m_sourceObject != nullptr) m_sourceObject->SourceDecrRef();
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

bool ibValueForm::LoadData(ibReaderMemory& reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyTitle->SetValue(propValue);
	m_propertyOrient->SetValue(reader.r_s32());
	reader.r_stringZ(propValue);
	m_propertyFG->SetValue(typeConv::StringToColour(propValue));
	reader.r_stringZ(propValue);
	m_propertyBG->SetValue(typeConv::StringToColour(propValue));
	m_propertyEnabled->SetValue((bool)reader.r_u8());
	return ibValueFrame::LoadData(reader);
}

bool ibValueForm::SaveData(ibWriterMemory& writer)
{
	writer.w_stringZ(m_propertyTitle->GetValueAsString());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());

	writer.w_stringZ(
		m_propertyFG->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyBG->GetValueAsString()
	);
	writer.w_u8(m_propertyEnabled->GetValueAsBoolean());
	return ibValueFrame::SaveData(writer);
}

//**********************************************************************************
//*                                   Other                                        *
//**********************************************************************************

const ibMetaData* ibValueForm::GetMetaData() const
{
	if (m_sourceObject != nullptr) {
		const ibValueMetaObject* metaObject = m_sourceObject->GetSourceMetaObject();
		if (metaObject != nullptr)
			return metaObject->GetMetaData();
	}

	return m_metaFormObject != nullptr ?
		m_metaFormObject->GetMetaData() :
		nullptr;
}

ibFormID ibValueForm::GetTypeForm() const
{
	return m_metaFormObject != nullptr ?
		m_metaFormObject->GetTypeForm() :
		m_formType;
}

bool ibValueForm::IsEditable() const
{
	if (m_sourceObject != nullptr) {
		const ibValueMetaObject* metaObject = m_sourceObject->GetSourceMetaObject();
		if (metaObject != nullptr)
			return metaObject->IsEditable();
	}

	return m_metaFormObject != nullptr ?
		m_metaFormObject->IsEditable() :
		true;
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
	eCloseOnOwnerClose
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
	return false;
}

bool ibValueForm::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr &&
			m_procUnit->GetPropVal(GetPropName(lPropNum), pvarPropVal))
			return true;
		// Bound handle (Controls / DataSource) — resolve the live bind value
		// directly. Works in the Designer (no ProcUnit) and as a runtime fallback.
		if (ibValue* bound = GetBoundValue(GetPropName(lPropNum))) {
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