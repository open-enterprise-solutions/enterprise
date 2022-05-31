////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "baseControl.h"
#include "form.h"
#include "compiler/procUnit.h"
#include "compiler/methods.h"
#include "utils/typeconv.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueFrame, CValue)

//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

IValueFrame::IValueFrame() : IObjectBase(), CValue(eValueTypes::TYPE_VALUE),
m_methods(new CMethods()), m_controlId(0)
{
}

IValueFrame::~IValueFrame()
{
	wxDELETE(m_methods);
}

IValueFrame *IValueFrame::GetChild(unsigned int idx)
{
	return dynamic_cast<IValueFrame *>(IObjectBase::GetChild(idx));
}

IValueFrame *IValueFrame::GetChild(unsigned int idx, const wxString & type)
{
	return dynamic_cast<IValueFrame *>(IObjectBase::GetChild(idx, type));
}

bool IValueFrame::LoadControl(IMetaFormObject *metaForm, CMemoryReader &dataReader)
{
	//Save meta version 
	const version_identifier_t &version = dataReader.r_u32(); //reserved 

	//Load unique guid 
	wxString strGuid;
	dataReader.r_stringZ(strGuid);
	m_controlGuid = strGuid;

	//Load meta id
	m_controlId = dataReader.r_u32();

	//Load standart fields
	dataReader.r_stringZ(m_controlName);

	//default value 
	m_expanded = dataReader.r_u8();

	//load events 
	if (!LoadEvent(dataReader)) {
		return false;
	}

	if (LoadData(dataReader)) {
		ReadProperty();
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

bool IValueFrame::SaveControl(IMetaFormObject *metaForm, CMemoryWriter &dataWritter)
{
	//Save meta version 
	dataWritter.w_u32(version_oes_last); //reserved 

	//Save unique guid
	dataWritter.w_stringZ(m_controlGuid);

	//Save meta id 
	dataWritter.w_u32(m_controlId);

	//Save standart fields
	dataWritter.w_stringZ(m_controlName);

	//default value 
	dataWritter.w_u8(m_expanded);

	//save events 
	if (!SaveEvent(dataWritter)) {
		return false; 
	}

	//save other data
	if (SaveData(dataWritter)) {
		SaveProperty();
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

bool IValueFrame::CallEvent(const wxString &sEventName)
{
	if (sEventName.IsEmpty())
		return false;

	Event *event = IObjectBase::GetEvent(sEventName);

	if (!event)
		return false;

	wxString m_sEventValue = event->GetValue();
	CProcUnit *formProcUnit = GetFormProcUnit();

	CValue eventCancel = false;

	ReadProperty();

	if (formProcUnit) {
		try
		{
			CValue m_controlElement = this;
			formProcUnit->CallFunction(m_sEventValue, m_controlElement, eventCancel);
		}
		catch (...)
		{
			return false;
		}
	}

	return eventCancel.GetBoolean();
}

bool IValueFrame::CallEvent(const wxString &sEventName, CValue &value1)
{
	if (sEventName.IsEmpty())
		return false;

	Event *event = IObjectBase::GetEvent(sEventName);

	if (!event)
		return false;

	wxString m_sEventValue = event->GetValue();
	CProcUnit *formProcUnit = GetFormProcUnit();

	CValue eventCancel = false;

	ReadProperty();

	if (formProcUnit) {
		try
		{
			formProcUnit->CallFunction(m_sEventValue, value1, eventCancel);
		}
		catch (...)
		{
			return false;
		}
	}

	return eventCancel.GetBoolean();
}

bool IValueFrame::CallEvent(const wxString &sEventName, CValue &value1, CValue &value2)
{
	if (sEventName.IsEmpty())
		return false;

	Event *event = IObjectBase::GetEvent(sEventName);

	if (!event)
		return false;

	wxString m_sEventValue = event->GetValue();
	CProcUnit *formProcUnit = GetFormProcUnit();

	CValue eventCancel = false;

	ReadProperty();

	if (formProcUnit) {
		try
		{
			formProcUnit->CallFunction(m_sEventValue, value1, value2, eventCancel);
		}
		catch (...)
		{
			return false;
		}
	}

	return eventCancel.GetBoolean();
}

bool IValueFrame::CallEvent(const wxString &sEventName, CValue &value1, CValue &value2, CValue &value3)
{
	if (sEventName.IsEmpty())
		return false;

	Event *event = IObjectBase::GetEvent(sEventName);

	if (!event)
		return false;

	wxString m_sEventValue = event->GetValue();
	CProcUnit *formProcUnit = GetFormProcUnit();

	CValue eventCancel = false;

	ReadProperty();

	if (formProcUnit) {
		try
		{
			formProcUnit->CallFunction(m_sEventValue, value1, value2, value3, eventCancel);
		}
		catch (...)
		{
			return false;
		}
	}

	return eventCancel.GetBoolean();
}

void IValueFrame::CallFunction(const wxString &functionName)
{
	CProcUnit *formProcUnit = GetFormProcUnit();

	if (formProcUnit) {
		try
		{
			formProcUnit->CallFunction(functionName);
		}
		catch (...)
		{
		}
	}
}

void IValueFrame::CallFunction(const wxString &functionName, CValue &value1)
{
	CProcUnit *formProcUnit = GetFormProcUnit();

	if (formProcUnit) {
		try
		{
			formProcUnit->CallFunction(functionName, value1);
		}
		catch (...)
		{
		}
	}
}

void IValueFrame::CallFunction(const wxString &functionName, CValue &value1, CValue &value2)
{
	CProcUnit *formProcUnit = GetFormProcUnit();

	if (formProcUnit) {
		try
		{
			formProcUnit->CallFunction(functionName, value1, value2);
		}
		catch (...)
		{
		}
	}
}

void IValueFrame::CallFunction(const wxString &functionName, CValue &value1, CValue &value2, CValue &value3)
{
	CProcUnit *formProcUnit = GetFormProcUnit();
	if (formProcUnit) {
		try
		{
			formProcUnit->CallFunction(functionName, value1, value2, value3);
		}
		catch (...)
		{
		}
	}
}

#include "frontend/visualView/visualHost.h"

wxObject *IValueFrame::GetWxObject()
{
	CValueForm *m_valueForm = dynamic_cast<CValueForm *>(GetOwnerForm());
	if (!m_valueForm) return NULL;

	//if run designer form search in own visualHost 
	if (m_visualHostContext)
	{
		CVisualEditorContextForm::CVisualEditorHost *visualEditor =
			m_visualHostContext->GetVisualEditor();
		return visualEditor->GetWxObject(this);
	}
	CVisualDocument *m_visualDoc = m_valueForm->GetVisualDocument();
	if (!m_visualDoc)
		return NULL;
	CVisualHost *m_visualView = m_visualDoc->GetVisualView();
	if (!m_visualView)
		return NULL;
	return m_visualView->GetWxObject(this);
}

//*******************************************************************
//*                           Attributes                            *
//*******************************************************************

#include "compiler/methods.h"

void IValueFrame::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	wxString sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("attribute")) {
		unsigned int idx = 
			m_methods->GetAttributePosition(aParams.GetIndex());
		Property *property =
			GetPropertyByIndex(idx);
		if (property) {
			SetPropertyData(property, cVal);
		}
	}
	else if (sSynonym == wxT("sizerItem")) {
		//if we have sizerItem then call him savepropery 
		IValueFrame *m_sizeritem = GetParent();
		if (m_sizeritem && m_sizeritem->GetClassName() == wxT("sizerItem"))
		{
			Property *property =
				m_sizeritem->GetPropertyByIndex(aParams.GetIndex());
			if (property) {
				SetPropertyData(property, cVal);
			}
		}
	}
	else if (sSynonym == wxT("events")) {
	}

	SaveProperty();

	CValueForm *ownerForm = GetOwnerForm();

	if (!ownerForm)
		return;

	CVisualDocument *visualDoc = ownerForm->GetVisualDocument();

	if (!visualDoc)
		return;

	CVisualHost *visualView = visualDoc->GetVisualView();

	if (!visualView)
		return;

	wxObject *object = visualView->GetWxObject(this);

	if (object) {
		wxWindow *parentWnd = NULL;
		Update(object, visualView);
		IValueFrame* nextParent = GetParent();
		while (!parentWnd && nextParent) {
			if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW || 
				nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW_TABLE) {
				parentWnd = dynamic_cast<wxWindow *>(visualView->GetWxObject(nextParent));
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

CValue IValueFrame::GetAttribute(attributeArg_t &aParams)
{
	wxString sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());
	if (sSynonym == wxT("sizerItem")) {
		//if we have sizerItem then call him savepropery 
		IValueFrame *m_sizeritem = GetParent();
		if (m_sizeritem && m_sizeritem->GetClassName() == wxT("sizerItem"))
		{
			unsigned int idx = m_methods->GetAttributePosition(aParams.GetIndex());
			Property *property =
				m_sizeritem->GetPropertyByIndex(idx);
			if (property)
				return GetPropertyData(property);
		}
	}
	else if (sSynonym == wxT("events")) {
		return new IValueFrame::CValueEventContainer(this);
	}
	else {
		unsigned int idx = 
			m_methods->GetAttributePosition(aParams.GetIndex());
		Property *property =
			GetPropertyByIndex(idx);
		if (property) {
			return GetPropertyData(property);
		}
	}

	return CValue();
}

int IValueFrame::FindAttribute(const wxString &sName) const
{
	return GetPropertyIndex(sName) + 1;
}

#include "metadata/metaObjects/objects/baseObject.h"
#include "common/srcExplorer.h"

bool IValueFrame::FilterSource(const CSourceExplorer &src, const meta_identifier_t &id)
{
	return !src.IsTableSection();
}

//*******************************************************************
//*                           Property                              *
//*******************************************************************

void IValueFrame::ReadProperty()
{
}

void IValueFrame::SaveProperty()
{
}