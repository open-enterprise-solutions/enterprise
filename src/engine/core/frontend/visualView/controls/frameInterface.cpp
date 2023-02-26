////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "controlInterface.h"
#include "form.h"
#include "core/compiler/procUnit.h"
#include "utils/typeconv.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueFrame, CValue);

//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

IValueFrame::IValueFrame() : IPropertyObject(), CValue(eValueTypes::TYPE_VALUE),
m_methodHelper(new CMethodHelper()), m_controlId(0)
{
}

IValueFrame::~IValueFrame()
{
	wxDELETE(m_methodHelper);
}

IValueFrame* IValueFrame::GetChild(unsigned int idx) const
{
	return dynamic_cast<IValueFrame*>(IPropertyObject::GetChild(idx));
}

IValueFrame* IValueFrame::GetChild(unsigned int idx, const wxString& type) const
{
	return dynamic_cast<IValueFrame*>(IPropertyObject::GetChild(idx, type));
}

bool IValueFrame::LoadControl(const IMetaFormObject* metaForm, CMemoryReader& dataReader)
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

bool IValueFrame::SaveControl(const IMetaFormObject* metaForm, CMemoryWriter& dataWritter)
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

wxString IValueFrame::GetTypeString() const
{
	return GetObjectTypeName() << wxT(".") << GetClassName();
}

wxString IValueFrame::GetString() const
{
	return GetTypeString();
}

//*******************************************************************

#include "metadata/metadata.h"
#include "metadata/singleClass.h"

bool IValueFrame::HasQuickChoice() const {
	IMetadata* metaData = GetMetaData();
	if (metaData == NULL)
		return false;
	CValue selValue; GetControlValue(selValue);
	IObjectValueAbstract* so = metaData->GetAvailableObject(selValue.GetTypeClass());
	if (so != NULL && so->GetObjectType() == eObjectType_simple) {
		return true;
	}
	else if (so != NULL && so->GetObjectType() == eObjectType_value_metadata) {
		IMetaTypeObjectValueSingle* meta_so = dynamic_cast<IMetaTypeObjectValueSingle*>(so);
		if (meta_so != NULL) {
			IMetaObjectRecordDataRef* metaObject = dynamic_cast<IMetaObjectRecordDataRef*>(meta_so->GetMetaObject());
			if (metaObject != NULL)
				return metaObject->HasQuickChoice();
		}
	}
	return false;
}

//*******************************************************************

#include "frontend/visualView/visualEditor.h"

CVisualDocument* IValueFrame::GetVisualDocument() const
{
	CValueForm* const valueForm = GetOwnerForm();
	if (valueForm == NULL)
		return NULL;
	return valueForm->GetVisualDocument();
}

CVisualEditorNotebook* IValueFrame::FindVisualEditor() const
{
	return CVisualEditorNotebook::FindEditorByForm(GetOwnerForm());
}

//*******************************************************************
//*                          Runtime                                *
//*******************************************************************

#include "frontend/visualView/visualHost.h"

wxObject* IValueFrame::GetWxObject() const
{
	CValueForm* const valueForm = GetOwnerForm();
	if (valueForm == NULL)
		return NULL;

	//if run designer form search in own visualHost 
	if (g_visualHostContext != NULL) {
		IVisualHost* visualEditor =
			g_visualHostContext->GetVisualHost();
		return visualEditor->GetWxObject((IValueFrame*)this);
	}

	CVisualDocument* const visualDoc = valueForm->GetVisualDocument();
	if (visualDoc == NULL)
		return NULL;

	CVisualHost* const visualView = visualDoc->GetVisualView();
	if (visualView == NULL)
		return NULL;

	return visualView->GetWxObject((IValueFrame*)this);
}

#include "core/metadata/metaObjects/objects/object.h"
#include "core/common/srcExplorer.h"

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
		if (property == NULL)
			continue;
		m_methodHelper->AppendProp(property->GetName(), idx, eProperty);
	}
	//if we have sizerItem then call him  
	IValueFrame* sizeritem = GetParent();
	if (sizeritem != NULL && sizeritem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		for (unsigned int idx = 0; idx < sizeritem->GetPropertyCount(); idx++) {
			Property* property = sizeritem->GetProperty(idx);
			if (property == NULL)
				continue;
			m_methodHelper->AppendProp(property->GetName(), idx, eSizerItem);
		}
	}
	m_methodHelper->AppendProp(wxT("events"), true, false, 0, eEvent);
}

bool IValueFrame::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eProperty) {
		unsigned int idx = m_methodHelper->GetPropData(lPropNum);
		Property* property = GetPropertyByIndex(idx);
		if (property != NULL)
			property->SetDataValue(varPropVal);
	}
	else if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		IValueFrame* sizerItem = GetParent();
		if (sizerItem != NULL &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			Property* property = sizerItem->GetPropertyByIndex(lPropNum);
			if (property != NULL)
				property->SetDataValue(varPropVal);
		}
	}

	CValueForm* ownerForm = GetOwnerForm();
	if (ownerForm == NULL)
		return false;

	CVisualDocument* visualDoc = ownerForm->GetVisualDocument();
	if (visualDoc == NULL)
		return false;

	CVisualHost* visualView = visualDoc->GetVisualView();
	if (visualView == NULL)
		return false;

	wxObject* wxobject = visualView->GetWxObject(this);

	if (wxobject != NULL) {
		wxWindow* wxparent = NULL;
		Update(wxobject, visualView);
		IValueFrame* nextParent = GetParent();
		while (wxparent == NULL && nextParent != NULL) {
			if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW) {
				wxObject* wxobject = visualView->GetWxObject(nextParent);
				wxparent = dynamic_cast<wxWindow*>(wxobject);
				break;
			}
			nextParent = nextParent->GetParent();
		}
		if (wxparent == NULL) wxparent = visualView->GetBackgroundWindow();
		OnUpdated(wxobject, wxparent, visualView);
		if (wxparent != NULL) wxparent->Layout();
	}
	return true;
}

bool IValueFrame::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		IValueFrame* sizerItem = GetParent();
		if (sizerItem != NULL &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			unsigned int idx = m_methodHelper->GetPropData(lPropNum);
			Property* property = sizerItem->GetPropertyByIndex(idx);
			if (property != NULL)
				pvarPropVal = property->GetDataValue();
			return true;
		}
	}
	else if (lPropAlias == eEvent) {
		pvarPropVal = new IValueFrame::CValueEventContainer(this);
		return true;
	}
	else {
		unsigned int idx = m_methodHelper->GetPropData(lPropNum);
		Property* property = GetPropertyByIndex(idx);
		if (property != NULL)
			pvarPropVal = property->GetDataValue();
		return true;
	}

	return false;
}