////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "baseControl.h"
#include "form.h"
#include "compiler/procUnit.h"
#include "compiler/methods.h"
#include "utils/typeconv.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueFrame, CValue);

//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

IValueFrame::IValueFrame() : IPropertyObject(), CValue(eValueTypes::TYPE_VALUE),
m_methods(new CMethods()), m_controlId(0)
{
}

IValueFrame::~IValueFrame()
{
	wxDELETE(m_methods);
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
//*                          Runtime                                *
//*******************************************************************

bool IValueFrame::CallEvent(const Event* event)
{
	if (event == NULL)
		return false;

	CProcUnit* formProcUnit = GetFormProcUnit();
	CValue eventCancel = false;

	if (formProcUnit != NULL) {
		try {
			CValue controlElement = this;
			formProcUnit->CallFunction(event->GetValue(), controlElement, eventCancel);
		}
		catch (...) {
			return false;
		}
	}

	return eventCancel.GetBoolean();
}

bool IValueFrame::CallEvent(const Event* event, CValue& value1)
{
	if (event == NULL)
		return false;

	CProcUnit* formProcUnit = GetFormProcUnit();
	CValue eventCancel = false;

	if (formProcUnit != NULL) {
		try {
			formProcUnit->CallFunction(event->GetValue(), value1, eventCancel);
		}
		catch (...) {
			return false;
		}
	}

	return eventCancel.GetBoolean();
}

bool IValueFrame::CallEvent(const Event* event, CValue& value1, CValue& value2)
{
	if (event == NULL)
		return false;

	wxString eventValue = event->GetValue();
	CProcUnit* formProcUnit = GetFormProcUnit();

	CValue eventCancel = false;

	if (formProcUnit != NULL) {
		try {
			formProcUnit->CallFunction(eventValue, value1, value2, eventCancel);
		}
		catch (...) {
			return false;
		}
	}

	return eventCancel.GetBoolean();
}

bool IValueFrame::CallEvent(const Event* event, CValue& value1, CValue& value2, CValue& value3)
{
	if (event == NULL)
		return false;

	CProcUnit* formProcUnit = GetFormProcUnit();
	CValue eventCancel = false;

	if (formProcUnit != NULL) {
		try {
			formProcUnit->CallFunction(event->GetValue(), value1, value2, value3, eventCancel);
		}
		catch (...) {
			return false;
		}
	}

	return eventCancel.GetBoolean();
}

void IValueFrame::CallFunction(const wxString& functionName)
{
	CProcUnit* formProcUnit = GetFormProcUnit();

	if (formProcUnit) {
		try {
			formProcUnit->CallFunction(functionName);
		}
		catch (...)
		{
		}
	}
}

void IValueFrame::CallFunction(const wxString& functionName, CValue& value1)
{
	CProcUnit* formProcUnit = GetFormProcUnit();

	if (formProcUnit) {
		try {
			formProcUnit->CallFunction(functionName, value1);
		}
		catch (...)
		{
		}
	}
}

void IValueFrame::CallFunction(const wxString& functionName, CValue& value1, CValue& value2)
{
	CProcUnit* formProcUnit = GetFormProcUnit();

	if (formProcUnit) {
		try {
			formProcUnit->CallFunction(functionName, value1, value2);
		}
		catch (...)
		{
		}
	}
}

void IValueFrame::CallFunction(const wxString& functionName, CValue& value1, CValue& value2, CValue& value3)
{
	CProcUnit* formProcUnit = GetFormProcUnit();
	if (formProcUnit) {
		try {
			formProcUnit->CallFunction(functionName, value1, value2, value3);
		}
		catch (...)
		{
		}
	}
}

#include "frontend/visualView/visualHost.h"

wxObject* IValueFrame::GetWxObject() const
{
	CValueForm* valueForm = dynamic_cast<CValueForm*>(GetOwnerForm());
	if (valueForm == NULL)
		return NULL;

	//if run designer form search in own visualHost 
	if (g_visualHostContext) {
		CVisualEditorContextForm::CVisualEditorHost* visualEditor =
			g_visualHostContext->GetVisualEditor();
		return visualEditor->GetWxObject((IValueFrame*)this);
	}

	CVisualDocument* visualDoc = valueForm->GetVisualDocument();
	if (visualDoc == NULL)
		return NULL;

	CVisualHost* visualView = visualDoc->GetVisualView();
	if (visualView == NULL)
		return NULL;

	return visualView->GetWxObject((IValueFrame*)this);
}

//*******************************************************************
//*                           Attributes                            *
//*******************************************************************

#include "compiler/methods.h"

void IValueFrame::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	wxString sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("attribute")) {
		unsigned int idx = m_methods->GetAttributePosition(aParams.GetIndex());
		Property* property = GetPropertyByIndex(idx);
		if (property != NULL)
			property->SetDataValue(cVal);
	}
	else if (sSynonym == wxT("sizerItem")) {
		//if we have sizerItem then call him savepropery 
		IValueFrame* sizerItem = GetParent();
		if (sizerItem &&
			sizerItem->GetClassName() == wxT("sizerItem")) {
			Property* property = sizerItem->GetPropertyByIndex(aParams.GetIndex());
			if (property != NULL)
				property->SetDataValue(cVal);
		}
	}
	else if (sSynonym == wxT("events")) {
	}

	CValueForm* ownerForm = GetOwnerForm();
	if (ownerForm == NULL)
		return;

	CVisualDocument* visualDoc = ownerForm->GetVisualDocument();
	if (visualDoc == NULL)
		return;

	CVisualHost* visualView = visualDoc->GetVisualView();
	if (visualView == NULL)
		return;

	wxObject* object = visualView->GetWxObject(this);

	if (object != NULL) {
		wxWindow* parentWnd = NULL;
		Update(object, visualView);
		IValueFrame* nextParent = GetParent();
		while (!parentWnd && nextParent) {
			if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW ||
				nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW_TABLE) {
				parentWnd = dynamic_cast<wxWindow*>(visualView->GetWxObject(nextParent));
				break;
			}
			nextParent = nextParent->GetParent();
		}
		if (!parentWnd) {
			parentWnd = visualView->GetBackgroundWindow();
		}
		OnUpdated(object, parentWnd, visualView);
	}
}

CValue IValueFrame::GetAttribute(attributeArg_t& aParams)
{
	wxString sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());
	if (sSynonym == wxT("sizerItem")) {
		//if we have sizerItem then call him savepropery 
		IValueFrame* sizerItem = GetParent();
		if (sizerItem && sizerItem->GetClassName() == wxT("sizerItem")) {
			unsigned int idx = m_methods->GetAttributePosition(aParams.GetIndex());
			Property* property = sizerItem->GetPropertyByIndex(idx);
			if (property != NULL)
				return property->GetDataValue();
		}
	}
	else if (sSynonym == wxT("events")) {
		return new IValueFrame::CValueEventContainer(this);
	}
	else {
		unsigned int idx = m_methods->GetAttributePosition(aParams.GetIndex());
		Property* property = GetPropertyByIndex(idx);
		if (property != NULL)
			return property->GetDataValue();
	}

	return CValue();
}

int IValueFrame::FindAttribute(const wxString& sName) const
{
	return GetPropertyIndex(sName) + 1;
}

#include "metadata/metaObjects/objects/object.h"
#include "common/srcExplorer.h"

bool IValueFrame::FilterSource(const CSourceExplorer& src, const meta_identifier_t& id)
{
	return !src.IsTableSection();
}