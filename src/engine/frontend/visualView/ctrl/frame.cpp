////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "control.h"
#include "form.h"
#include "backend/compiler/procUnit.h"


wxIMPLEMENT_ABSTRACT_CLASS(IValueFrame, CValue);

//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

IValueFrame::IValueFrame() : IPropertyObject(), CValue(eValueTypes::TYPE_VALUE),
m_methodHelper(new CMethodHelper()), m_valEventContainer(nullptr), m_controlId(0)
{
	m_valEventContainer = CValue::CreateAndConvertObjectValueRef<CValueEventContainer>(this);
	m_valEventContainer->IncrRef();
}

IValueFrame::~IValueFrame()
{
	m_valEventContainer->DecrRef();
	wxDELETE(m_methodHelper);
}

wxString IValueFrame::GetClassName() const
{
	const class_identifier_t& clsid = GetClassType();
	if (clsid == 0)
		return _("Not founded in wxClassInfo!");
	IAbstractTypeCtor* typeCtor = CValue::GetAvailableCtor(clsid);
	if (typeCtor != nullptr) {
		return typeCtor->GetClassName();
	}
	return _("Not founded in wxClassInfo!");
}

wxString IValueFrame::GetObjectTypeName() const
{
	const class_identifier_t& clsid = GetClassType();
	if (clsid == 0)
		return _("Not founded in wxClassInfo!");
	IControlTypeCtor* typeCtor = dynamic_cast<IControlTypeCtor*>(CValue::GetAvailableCtor(clsid));
	if (typeCtor != nullptr) {
		return typeCtor->GetTypeControlName();
	}
	return _("Not founded in wxClassInfo!");
}

bool IValueFrame::LoadControl(const IMetaObjectForm* metaForm, CMemoryReader& dataReader)
{
	//Save meta version 
	const version_identifier_t& version = dataReader.r_u32(); //reserved 

	//Load unique guid 
	m_controlGuid = dataReader.r_stringZ();

	//Load meta id
	m_controlId = dataReader.r_u32();

	//Load standart fields
	SetControlName(dataReader.r_stringZ());

	//default value 
	m_expanded = dataReader.r_u8();

	//load events 
	if (!LoadEvent(dataReader)) {
		return false;
	}

	if (LoadData(dataReader)) {
		return true;
	}

	return false;
}

bool IValueFrame::LoadEvent(CMemoryReader& dataReader)
{
	//load events 
	unsigned int eventCount = dataReader.r_u32();

	// ...and the event handlers
	for (unsigned i = 0; i < eventCount; i++) {
		wxString eventName, eventValue;
		dataReader.r_stringZ(eventName);
		dataReader.r_stringZ(eventValue);
		Event* objEvent = GetEvent(eventName);
		if (objEvent) {
			objEvent->SetValue(eventValue);
		}
	}

	return true;
}

bool IValueFrame::SaveControl(const IMetaObjectForm* metaForm, CMemoryWriter& dataWritter)
{
	//Save meta version 
	dataWritter.w_u32(version_oes_last); //reserved 

	//Save unique guid
	dataWritter.w_stringZ(m_controlGuid);

	//Save meta id 
	dataWritter.w_u32(m_controlId);

	//Save standart fields
	dataWritter.w_stringZ(GetControlName());

	//default value 
	dataWritter.w_u8(m_expanded);

	//save events 
	if (!SaveEvent(dataWritter)) {
		return false;
	}

	//save other data
	if (SaveData(dataWritter)) {
		return true;
	}

	return false;
}

bool IValueFrame::SaveEvent(CMemoryWriter& dataWritter)
{
	//save events 
	unsigned int eventCount = GetEventCount();
	dataWritter.w_u32(eventCount);
	// ...and the event handlers
	for (unsigned i = 0; i < eventCount; i++) {
		Event* objEvent = GetEvent(i);
		wxASSERT(objEvent);
		dataWritter.w_stringZ(objEvent->GetName());
		dataWritter.w_stringZ(objEvent->GetValue());
	}

	return true;
}

//*******************************************************************

bool IValueFrame::Init()
{
	// always false
	return false;
}

bool IValueFrame::Init(CValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 2) 
		return false;
	CValueForm* ownerForm = nullptr;
	IValueFrame* controlParent = nullptr;
	if (paParams[0]->ConvertToValue(ownerForm) &&
		paParams[1]->ConvertToValue(controlParent)) {
		if (controlParent != nullptr) {
			controlParent->AddChild(this);
			SetParent(controlParent);
		}
		SetReadOnly(ownerForm ? ownerForm->IsEditable() : true);
		SetOwnerForm(ownerForm);
		ownerForm->ResolveNameConflict(this);
		if (paParams[2]->GetBoolean()) {
			GenerateGuid();
			GenerateNewID();
		}
		return true;
	}
	return false;
}

//*******************************************************************

#include "backend/metaData.h"
#include "backend/objCtor.h"

bool IValueFrame::HasQuickChoice() const {
	IMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return false;
	CValue selValue; GetControlValue(selValue);
	IAbstractTypeCtor* so = metaData->GetAvailableCtor(selValue.GetClassType());
	if (so != nullptr && so->GetObjectTypeCtor() == eCtorObjectType_object_primitive) {
		return true;
	}
	else if (so != nullptr && so->GetObjectTypeCtor() == eCtorObjectType_object_meta_value) {
		IMetaValueTypeCtor* meta_so = dynamic_cast<IMetaValueTypeCtor*>(so);
		if (meta_so != nullptr) {
			IMetaObjectRecordDataRef* metaObject = dynamic_cast<IMetaObjectRecordDataRef*>(meta_so->GetMetaObject());
			if (metaObject != nullptr)
				return metaObject->HasQuickChoice();
		}
	}
	return false;
}

//*******************************************************************

CVisualDocument* IValueFrame::GetVisualDocument() const
{
	CValueForm* const valueForm = GetOwnerForm();
	if (valueForm == nullptr)
		return nullptr;
	return valueForm->GetVisualDocument();
}

IVisualEditorNotebook* IValueFrame::FindVisualEditor() const
{
	return IVisualEditorNotebook::FindEditorByForm(GetOwnerForm());
}

//*******************************************************************
//*                          Runtime                                *
//*******************************************************************

#include "frontend/visualView/visualHost.h"

wxObject* IValueFrame::GetWxObject() const
{
	CValueForm* const valueForm = GetOwnerForm();
	if (valueForm == nullptr)
		return nullptr;

	//if run designer form search in own visualHost 
	if (g_visualHostContext != nullptr) {
		IVisualHost* visualEditor =
			g_visualHostContext->GetVisualHost();
		return visualEditor->GetWxObject((IValueFrame*)this);
	}

	CVisualDocument* const visualDoc = valueForm->GetVisualDocument();
	if (visualDoc == nullptr)
		return nullptr;

	CVisualHost* const visualView = visualDoc->GetVisualView();
	if (visualView == nullptr)
		return nullptr;

	return visualView->GetWxObject((IValueFrame*)this);
}

#include "backend/metaCollection/partial/object.h"
#include "backend/srcExplorer.h"
#include "frame.h"

bool IValueFrame::FilterSource(const CSourceExplorer& src, const meta_identifier_t& id)
{
	return !src.IsTableSection();
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void IValueFrame::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	for (unsigned int idx = 0; idx < IPropertyObject::GetPropertyCount(); idx++) {
		Property* property = IPropertyObject::GetProperty(idx);
		if (property == nullptr)
			continue;
		m_methodHelper->AppendProp(property->GetName(), idx, eProperty);
	}
	//if we have sizerItem then call him  
	IValueFrame* sizeritem = GetParent();
	if (sizeritem != nullptr && sizeritem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		for (unsigned int idx = 0; idx < sizeritem->GetPropertyCount(); idx++) {
			Property* property = sizeritem->GetProperty(idx);
			if (property == nullptr)
				continue;
			m_methodHelper->AppendProp(property->GetName(), idx, eSizerItem);
		}
	}
	m_methodHelper->AppendProp(wxT("events"), true, false, 0, eEvent);
	if (m_valEventContainer != nullptr) {
		m_valEventContainer->PrepareNames();
	}
}

bool IValueFrame::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProperty) {
		unsigned int idx = m_methodHelper->GetPropData(lPropNum);
		Property* property = GetPropertyByIndex(idx);
		if (property != nullptr)
			property->SetDataValue(varPropVal);
	}
	else if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		IValueFrame* sizerItem = GetParent();
		if (sizerItem != nullptr &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			Property* property = sizerItem->GetPropertyByIndex(lPropNum);
			if (property != nullptr)
				property->SetDataValue(varPropVal);
		}
	}

	CValueForm* ownerForm = GetOwnerForm();
	if (ownerForm == nullptr)
		return false;

	CVisualDocument* visualDoc = ownerForm->GetVisualDocument();
	if (visualDoc == nullptr)
		return false;

	CVisualHost* visualView = visualDoc->GetVisualView();
	if (visualView == nullptr)
		return false;

	wxObject* wxobject = visualView->GetWxObject(this);

	if (wxobject != nullptr) {
		wxWindow* wxparent = nullptr;
		Update(wxobject, visualView);
		IValueFrame* nextParent = GetParent();
		while (wxparent == nullptr && nextParent != nullptr) {
			if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW) {
				wxObject* wxobject = visualView->GetWxObject(nextParent);
				wxparent = dynamic_cast<wxWindow*>(wxobject);
				break;
			}
			nextParent = nextParent->GetParent();
		}
		if (wxparent == nullptr) wxparent = visualView->GetBackgroundWindow();
		OnUpdated(wxobject, wxparent, visualView);
		if (wxparent != nullptr) wxparent->Layout();
	}
	return true;
}

bool IValueFrame::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		IValueFrame* sizerItem = GetParent();
		if (sizerItem != nullptr &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			unsigned int idx = m_methodHelper->GetPropData(lPropNum);
			Property* property = sizerItem->GetPropertyByIndex(idx);
			if (property != nullptr)
				pvarPropVal = property->GetDataValue();
			return true;
		}
	}
	else if (lPropAlias == eEvent) {
		pvarPropVal = m_valEventContainer;
		return true;
	}
	else {
		unsigned int idx = m_methodHelper->GetPropData(lPropNum);
		Property* property = GetPropertyByIndex(idx);
		if (property != nullptr)
			pvarPropVal = property->GetDataValue();
		return true;
	}

	return false;
}