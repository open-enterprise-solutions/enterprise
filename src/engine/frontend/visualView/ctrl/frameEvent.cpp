////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base - events 
////////////////////////////////////////////////////////////////////////////

#include "control.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(ibValueFrame::ibValueEventContainer, ibValue);


//////////////////////////////////////////////////////////////////////

ibValueFrame::ibValueEventContainer::ibValueEventContainer() : ibValue(ibValueTypes::TYPE_VALUE, true),
m_controlEvent(nullptr), m_methodHelper(nullptr)
{
}

ibValueFrame::ibValueEventContainer::ibValueEventContainer(ibValueFrame* controlEvent) : ibValue(ibValueTypes::TYPE_VALUE, true),
m_controlEvent(controlEvent), m_methodHelper(new ibValueMethodHelper())
{
}

#include "backend/system/value/valueMap.h"
//#include "backend/system/value/valueEvent.h"

ibValueFrame::ibValueEventContainer::~ibValueEventContainer()
{
	if (m_methodHelper)
		delete m_methodHelper;
}

ibValue ibValueFrame::ibValueEventContainer::GetIteratorEmpty()
{
	return ibValue();
}

ibValue ibValueFrame::ibValueEventContainer::GetIteratorAt(unsigned int idx)
{
	if (m_controlEvent->GetEventCount() < idx) {
		return ibValue();
	}

	ibEvent* event = m_controlEvent->GetEvent(idx);
	if (event == nullptr) return ibValue();
	//return ibValue::CreateAndPrepareValueRef<ibValueEvent>(event->GetValue());

	ibValue retEvent;
	event->GetDataValue(retEvent);
	return retEvent;
}

#include "backend/appData.h"

bool ibValueFrame::ibValueEventContainer::SetAt(const ibValue& varKeyValue, const ibValue& varValue)
{
	const ibNumber number = varKeyValue.GetNumber();
	if (m_controlEvent->GetEventCount() < number.ToUInt())
		return false;
	ibEvent* event = m_controlEvent->GetEvent(number.ToUInt());
	if (event == nullptr) return false;
	//ibValueEvent* eventValue = nullptr;
	//if (varValue.ConvertToValue(eventValue)) {
	//	event->SetValue(eventValue->GetString());
	//}
	//else {
	//	event->SetValue(wxEmptyString);
	//}
	//return true;
	return event->SetDataValue(varValue);
}

bool ibValueFrame::ibValueEventContainer::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	ibNumber number = varKeyValue.GetNumber();
	if (m_controlEvent->GetEventCount() < number.ToUInt())
		return false;
	ibEvent* event = m_controlEvent->GetEvent(number.ToUInt());
	if (event == nullptr) return false;
	//wxString eventValue = event->GetValue();
	//if (eventValue.IsEmpty())
	//	return false;
	//pvarValue = ibValue::CreateAndPrepareValueRef<ibValueEvent>(eventValue);
	//return true;
	return event->GetDataValue(pvarValue);
}

bool ibValueFrame::ibValueEventContainer::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	const wxString& key = varKeyValue.GetString();
	for (unsigned int idx = 0; idx < m_controlEvent->GetEventCount(); idx++) {
		ibEvent* event = m_controlEvent->GetEvent(idx);
		if (event == nullptr) continue;
		if (stringUtils::CompareString(key, event->GetName())) {
			//cValueFound = ibValue::CreateAndPrepareValueRef<ibValueEvent>(event->GetName());
			//return true;
			return event->GetDataValue(cValueFound);
		}
	}
	return false;
}

enum
{
	enControlProperty,
	enControlCount
};

void ibValueFrame::ibValueEventContainer::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc(wxT("Property"), 2, wxT("Property(key : string, valueFound : event)"));
	m_methodHelper->AppendFunc(wxT("Count"), wxT("Count()"));

	for (unsigned int idx = 0; idx < m_controlEvent->GetEventCount(); idx++) {
		ibEvent* event = m_controlEvent->GetEvent(idx);
		if (event == nullptr)
			continue;
		m_methodHelper->AppendProp(event->GetName());
	}
}

bool ibValueFrame::ibValueEventContainer::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	//ibValueEvent* eventValue = nullptr;
	if (m_controlEvent->GetEventCount() < (unsigned int)lPropNum)
		return false;
	ibEvent* event = m_controlEvent->GetEvent(lPropNum);
	if (event == nullptr) return false;
	//if (varPropVal.ConvertToValue(eventValue)) {
	//	event->SetValue(eventValue->GetString());
	//}
	//else {
	//	event->SetValue(wxEmptyString);
	//}
	//return true;
	return event->SetDataValue(varPropVal);
}

bool ibValueFrame::ibValueEventContainer::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (m_controlEvent->GetEventCount() < (unsigned int)lPropNum)
		return false;

	ibEvent* event = m_controlEvent->GetEvent(lPropNum);
	if (event == nullptr) return false;

	const wxString& eventValue = event->GetValue();
	if (eventValue.IsEmpty()) return true;
	//pvarPropVal = ibValue::CreateAndPrepareValueRef<ibValueEvent>(eventValue);
	return event->GetDataValue(pvarPropVal);
}

#include "backend/system/value/valueType.h"

bool ibValueFrame::ibValueEventContainer::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enControlProperty:
	{	ibValue defaultVal;
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : defaultVal); }
		return true;
	case enControlCount:
		pvarRetValue = Count();
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueFrame::ibValueEventContainer, "EventContainer", string_to_clsid("VL_EVCT"));
