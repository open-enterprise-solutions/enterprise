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

wxIMPLEMENT_DYNAMIC_CLASS(ibValueForm, ibValueFrame);

//****************************************************************************
//*                              Frame                                       *
//****************************************************************************

ibValueForm::ibValueForm(const ibValueMetaObjectFormBase* creator, ibControlFrame* ownerControl,
	ibSourceDataObject* srcObject, const ibUniqueKey& formGuid) : ibValueFrame(), ibRuntimeModuleDataObject(),
	m_controlOwner(nullptr), m_sourceObject(nullptr), m_metaFormObject(nullptr),
	m_formCollectionControl(ibValue::CreateAndPrepareValueRef<ibValueFormCollectionControl>(this)),
	m_formType(defaultFormType), m_closeOnChoice(true), m_closeOnOwnerClose(true), m_formModified(false)
{
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

	for (unsigned int idx = GetChildCount(); idx > 0; idx--) {
		ibValueFrame* controlChild = GetChild(idx - 1);
		ClearRecursive(controlChild);
		if (controlChild != nullptr) controlChild->DecrRef();
	}

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

ibMetaData* ibValueForm::GetMetaData() const
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

void ibValueForm::PrepareNames() const
{
	//default element
	m_methodHelper->ClearHelper();

	// ThisForm is a bc-internal self-prop (ThisForm of binding "ThisForm") —
	// keep scoped so IntelliSense / debug-watch / eval don't surface it.
	// Controls / DataSource are real form properties — must stay visible.
	m_methodHelper->AppendProp(thisForm, true, false, true, eThisForm, eSystem);
	m_methodHelper->AppendProp(wxT("Controls"), true, false, false, eControls, eSystem);
	m_methodHelper->AppendProp(wxT("DataSource"), true, false, false, eDataSource, eSystem);
	m_methodHelper->AppendProp(wxT("Modified"), eModified, eSystem);
	m_methodHelper->AppendProp(wxT("FormOwner"), eFormOwner, eSystem);
	m_methodHelper->AppendProp(wxT("UniqueKey"), eUniqueKey, eSystem);

	m_methodHelper->AppendProp(wxT("CloseOnChoice"), eCloseOnChoice, eSystem);
	m_methodHelper->AppendProp(wxT("CloseOnOwnerClose"), eCloseOnOwnerClose, eSystem);

	m_methodHelper->AppendProc(wxT("Show"), wxT("Show()"));
	m_methodHelper->AppendProc(wxT("Activate"), wxT("Activate()"));
	m_methodHelper->AppendProc(wxT("Update"), wxT("Update()"));
	m_methodHelper->AppendProc(wxT("Close"), wxT("Close()"));
	m_methodHelper->AppendFunc(wxT("IsShown"), wxT("IsShown()"));
	m_methodHelper->AppendProc(wxT("AttachIdleHandler"), 3, wxT("AttachIdleHandler(procedureName : string, interval : number, single : boolean)"));
	m_methodHelper->AppendProc(wxT("DetachIdleHandler"), 1, wxT("DetachIdleHandler(procedureName : string)"));
	m_methodHelper->AppendProc(wxT("NotifyChoice"), 1, wxT("NotifyChoice(value)"));

	//from property 
	for (unsigned int idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++) {
		ibProperty* property = ibPropertyObject::GetProperty(idx);
		if (property == nullptr)
			continue;
		m_methodHelper->AppendProp(property->GetName(), idx, eProperty);
	}

	ExportNamesToHelper(m_methodHelper, eProcUnit);

	for (auto& control : m_listControl) {
		if (!control->HasValueInControl())
			continue;

		m_methodHelper->AppendProp(
			control->GetControlName(),
			control->GetControlID(),
			eAttribute
		);
	}

	m_formCollectionControl->PrepareNames();
}

bool ibValueForm::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(GetPropName(lPropNum), varPropVal);
		}
	}
	else if (lPropAlias == eProperty) {
		return ibValueFrame::SetPropVal(lPropNum, varPropVal);
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum)) {
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
		unsigned int id = m_methodHelper->GetPropData(lPropNum);
		auto it = std::find_if(m_listControl.begin(), m_listControl.end(),
			[id](const ibValueFrame* control) {
				return id == control->GetControlID();
			}
		);
		if (it != m_listControl.end())
			return (*it)->SetControlValue(varPropVal);
	}
	return false;
}

bool ibValueForm::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
	}
	else if (lPropAlias == eProperty) {
		return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum))
		{
		case eThisForm:
			pvarPropVal = GetValue();
			return true;
		case eControls:
			pvarPropVal = m_formCollectionControl;
			return true;
		case eDataSource:
			pvarPropVal = dynamic_cast<ibValue*>(m_sourceObject);
			return true;
		case eModified:
			pvarPropVal = IsModified();
			return true;
		case eFormOwner:
			pvarPropVal = dynamic_cast<ibValue*>(m_controlOwner);
			return true;
		case eUniqueKey:
			pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueGuid>(m_formKey);
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
		unsigned int id = m_methodHelper->GetPropData(lPropNum);
		auto it = std::find_if(m_listControl.begin(), m_listControl.end(),
			[id](const ibValueFrame* control) {
				return id == control->GetControlID();
			}
		);
		if (it != m_listControl.end())
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