////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base - events 
////////////////////////////////////////////////////////////////////////////

#include "controlInterface.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IValueFrame::CValueEventContainer, CValue);


//////////////////////////////////////////////////////////////////////

IValueFrame::CValueEventContainer::CValueEventContainer() : CValue(eValueTypes::TYPE_VALUE, true),
m_controlEvent(NULL), m_methodHelper(NULL)
{
}

IValueFrame::CValueEventContainer::CValueEventContainer(IValueFrame* controlEvent) : CValue(eValueTypes::TYPE_VALUE, true),
m_controlEvent(controlEvent), m_methodHelper(new CMethodHelper())
{
}

#include "core/compiler/valueMap.h"
#include "frontend/visualView/special/valueEvent.h"
#include "utils/stringUtils.h"

IValueFrame::CValueEventContainer::~CValueEventContainer()
{
	if (m_methodHelper)
		delete m_methodHelper;
}

CValue IValueFrame::CValueEventContainer::GetItEmpty()
{
	return CValue();
}

CValue IValueFrame::CValueEventContainer::GetItAt(unsigned int idx)
{
	if (m_controlEvent->GetEventCount() < idx) {
		return CValue();
	}

	Event* event = m_controlEvent->GetEvent(idx);
	if (!event) {
		return CValue();
	}

	return new CValueEvent(event->GetValue());
}

#include "appData.h"

bool IValueFrame::CValueEventContainer::SetAt(const CValue& varKeyValue, const CValue& varValue)
{
	number_t number = varKeyValue.GetNumber();
	if (m_controlEvent->GetEventCount() < number.GetUInteger())
		return false;
	Event* event = m_controlEvent->GetEvent(number.GetUInteger());
	CValueEvent* eventValue = NULL;
	if (varValue.ConvertToValue(eventValue)) {
		event->SetValue(eventValue->GetString());
	}
	else {
		event->SetValue(wxEmptyString);
	}
	return true;
}

bool IValueFrame::CValueEventContainer::GetAt(const CValue& varKeyValue, CValue& pvarValue)
{
	number_t number = varKeyValue.GetNumber();
	if (m_controlEvent->GetEventCount() < number.GetUInteger())
		return false;
	Event* event = m_controlEvent->GetEvent(number.GetUInteger());
	if (!event)
		return false;
	wxString eventValue = event->GetValue();
	if (eventValue.IsEmpty())
		return false;
	pvarValue = new CValueEvent(eventValue);
	return true;
}

bool IValueFrame::CValueEventContainer::Property(const CValue& varKeyValue, CValue& cValueFound)
{
	const wxString& key = varKeyValue.GetString();
	for (unsigned int idx = 0; idx < m_controlEvent->GetEventCount(); idx++) {
		Event* event = m_controlEvent->GetEvent(idx);
		if (event == NULL)
			continue;
		if (StringUtils::CompareString(key, event->GetName())) {
			cValueFound = new CValueEvent(event->GetName());
			return true;
		}
	}
	return false;
}

enum
{
	enControlProperty,
	enControlCount
};

void IValueFrame::CValueEventContainer::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc("property", 2, "property(key, valueFound)");
	m_methodHelper->AppendFunc("count", "count()");

	for (unsigned int idx = 0; idx < m_controlEvent->GetEventCount(); idx++) {
		Event* event = m_controlEvent->GetEvent(idx);
		if (event == NULL)
			continue;
		m_methodHelper->AppendProp(event->GetName());
	}
}

bool IValueFrame::CValueEventContainer::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	CValueEvent* eventValue = NULL;
	if (m_controlEvent->GetEventCount() < (unsigned int)lPropNum)
		return false;
	Event* event = m_controlEvent->GetEvent(lPropNum);
	if (varPropVal.ConvertToValue(eventValue)) {
		event->SetValue(eventValue->GetString());
	}
	else {
		event->SetValue(wxEmptyString);
	}
	return true;
}

bool IValueFrame::CValueEventContainer::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	if (m_controlEvent->GetEventCount() < (unsigned int)lPropNum)
		return false;

	Event* event = m_controlEvent->GetEvent(lPropNum);
	if (!event)
		return false;

	const wxString& eventValue = event->GetValue();
	if (eventValue.IsEmpty())
		return false;

	pvarPropVal = new CValueEvent(eventValue);
	return true;
}

#include "core/compiler/valueType.h"

bool IValueFrame::CValueEventContainer::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enControlProperty:
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : CValue());
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

SO_VALUE_REGISTER(IValueFrame::CValueEventContainer, "eventContainer", TEXT2CLSID("VL_EVCT"));
