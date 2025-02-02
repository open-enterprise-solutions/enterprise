////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame control
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "frontend/visualView/special/enums/valueOrient.h"
#include "backend/metaCollection/partial/object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueForm, IValueFrame);

//****************************************************************************
//*                              Frame                                       *
//****************************************************************************

CValueForm::CValueForm(IControlFrame* ownerControl, IMetaObjectForm* metaForm,
	ISourceDataObject* ownerSrc, const CUniqueKey& formGuid, bool readOnly) : IValueFrame(), IModuleInfo(),
	m_controlOwner(nullptr), m_sourceObject(nullptr), m_metaFormObject(nullptr), m_valueFormDocument(nullptr),
	m_defaultFormType(defaultFormType), m_formModified(false)
{
	//init default params
	CValueForm::InitializeForm(ownerControl, metaForm, ownerSrc, formGuid, readOnly);

	//set default params
	m_controlId = defaultFormId;

	//init frame controls
	m_formControls = CValue::CreateAndConvertObjectValueRef<CValueFormControl>(this);
	m_formControls->IncrRef();

	//init attributes controls
	m_formData = CValue::CreateAndConvertObjectValueRef<CValueFormData>(this);
	m_formData->IncrRef();
}

CValueForm::~CValueForm()
{
	for (auto pair : m_aIdleHandlers) {
		wxTimer* timer = pair.second;
		if (timer->IsRunning()) {
			timer->Stop();
		}
		timer->Unbind(wxEVT_TIMER, &CValueForm::OnIdleHandler, this);
		delete timer;
	}

	m_formControls->DecrRef();
	m_formData->DecrRef();
	
	for (unsigned int idx = GetChildCount(); idx > 0; idx--) {
		IValueFrame* controlChild =
			dynamic_cast<IValueFrame*>(GetChild(idx - 1));
		ClearRecursive(controlChild);
		if (controlChild) {
			controlChild->DecrRef();
		}
	}

	if (m_sourceObject) {
		m_sourceObject->DecrRef();
	}
}

void CValueForm::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	UpdateForm();
}

void CValueForm::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
	//lay out parent window 
	wxWindow* wndParent = visualHost->GetParent();
	if (wndParent) {
		wndParent->Layout();
	}
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool CValueForm::LoadData(CMemoryReader& reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyCaption->SetValue(propValue);
	m_propertyOrient->SetValue(reader.r_s32());
	reader.r_stringZ(propValue);
	m_propertyFG->SetValue(typeConv::StringToColour(propValue));
	reader.r_stringZ(propValue);
	m_propertyBG->SetValue(typeConv::StringToColour(propValue));
	m_propertyEnabled->SetValue((bool)reader.r_u8());
	return IValueFrame::LoadData(reader);
}

bool CValueForm::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_propertyCaption->GetValueAsString());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());

	writer.w_stringZ(
		m_propertyFG->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyBG->GetValueAsString()
	);
	writer.w_u8(m_propertyEnabled->GetValueAsBoolean());
	return IValueFrame::SaveData(writer);
}

//**********************************************************************************
//*                                   Other                                        *
//**********************************************************************************

IMetaData* CValueForm::GetMetaData() const
{
	if (m_sourceObject != nullptr) {
		const IMetaObject* metaObject = m_sourceObject->GetSourceMetaObject();
		if (metaObject != nullptr) {
			return metaObject->GetMetaData();
		}
	}

	return m_metaFormObject ?
		m_metaFormObject->GetMetaData() :
		nullptr;
}

form_identifier_t CValueForm::GetTypeForm() const
{
	return m_metaFormObject ?
		m_metaFormObject->GetTypeForm() :
		m_defaultFormType;
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

enum Prop {
	eThisForm = 0,
	eControls,
	eDataSources,
	eModified,
	eFormOwner,
	eUniqueKey
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

void CValueForm::PrepareNames() const
{
	//default element
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendProp(thisForm, true, false, eThisForm, eSystem);
	m_methodHelper->AppendProp(wxT("controls"), true, false, eControls, eSystem);
	m_methodHelper->AppendProp(wxT("dataSources"), true, false, eDataSources, eSystem);
	m_methodHelper->AppendProp(wxT("modified"), eModified, eSystem);
	m_methodHelper->AppendProp(wxT("formOwner"), eFormOwner, eSystem);
	m_methodHelper->AppendProp(wxT("uniqueKey"), eUniqueKey, eSystem);

	m_methodHelper->AppendProc(wxT("show"), "show()");
	m_methodHelper->AppendProc(wxT("activate"), "activate()");
	m_methodHelper->AppendProc(wxT("update"), "update()");
	m_methodHelper->AppendProc(wxT("close"), "close()");
	m_methodHelper->AppendFunc(wxT("isShown"), "isShown()");
	m_methodHelper->AppendProc(wxT("attachIdleHandler"), 3, "attachIdleHandler(procedureName, interval, single)");
	m_methodHelper->AppendProc(wxT("detachIdleHandler"), 1, "detachIdleHandler(procedureName)");
	m_methodHelper->AppendProc(wxT("notifyChoice"), 1, "notifyChoice(value)");

	//from property 
	for (unsigned int idx = 0; idx < IPropertyObject::GetPropertyCount(); idx++) {
		Property* property = IPropertyObject::GetProperty(idx);
		if (property == nullptr)
			continue;
		m_methodHelper->AppendProp(property->GetName(), idx, eProperty);
	}

	if (m_procUnit != nullptr) {
		CByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportVariable : byteCode->m_aExportVarList)
				m_methodHelper->AppendProp(
					exportVariable.first,
					exportVariable.second,
					eProcUnit
				);
			for (auto exportFunction : byteCode->m_aExportFuncList)
				m_methodHelper->AppendMethod(
					exportFunction.first,
					byteCode->GetNParams(exportFunction.second),
					byteCode->HasRetVal(exportFunction.second),
					exportFunction.second,
					eProcUnit
				);
		}
	}

	m_formControls->PrepareNames();
	m_formData->PrepareNames();
}

bool CValueForm::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(GetPropName(lPropNum), varPropVal);
		}
	}
	else if (lPropAlias == eProperty) {
		return IValueFrame::SetPropVal(lPropNum, varPropVal);
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum)) {
		case eModified:
			Modify(varPropVal.GetBoolean());
			return true;
		}
	}
	return false;
}

bool CValueForm::GetPropVal(const long lPropNum, CValue& pvarPropVal)
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
		return IValueFrame::GetPropVal(lPropNum, pvarPropVal);
	}
	else if (lPropAlias == eSystem) {
		switch (m_methodHelper->GetPropData(lPropNum))
		{
		case eThisForm:
			pvarPropVal = GetValue();
			return true;
		case eControls:
			pvarPropVal = m_formControls;
			return true;
		case eDataSources:
			pvarPropVal = m_formData;
			return true;
		case eModified:
			pvarPropVal = IsModified();
			return true;
		case eFormOwner:
			pvarPropVal = m_controlOwner ? m_controlOwner : CValue();
			return m_controlOwner != nullptr;
		case eUniqueKey:
			pvarPropVal = CValue::CreateAndConvertObjectValueRef<CValueGuid>(m_formKey);
			return true;
		}
	}

	return false;
}

bool CValueForm::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
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

	return IModuleInfo::ExecuteProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool CValueForm::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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

	return IModuleInfo::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(CValueForm, "form", "form", g_controlFormCLSID);