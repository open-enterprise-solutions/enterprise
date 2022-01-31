////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base - events 
////////////////////////////////////////////////////////////////////////////

#include "baseControl.h"
#include "compiler/methods.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IValueFrame::CValueEventContainer, CValue);


//////////////////////////////////////////////////////////////////////

IValueFrame::CValueEventContainer::CValueEventContainer() : CValue(eValueTypes::TYPE_VALUE, true),
m_controlEvent(NULL), m_methods(NULL)
{
}

IValueFrame::CValueEventContainer::CValueEventContainer(IValueFrame *controlEvent) : CValue(eValueTypes::TYPE_VALUE, true),
m_controlEvent(controlEvent), m_methods(new CMethods())
{
}

#include "compiler/valueMap.h"
#include "frontend/visualView/special/valueEvent.h"
#include "utils/stringUtils.h"

IValueFrame::CValueEventContainer::~CValueEventContainer()
{
	if (m_methods)
		delete m_methods;
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
	
	Event *event = m_controlEvent->GetEvent(idx);
	if (!event) {
		return CValue();
	}

	return new CValueEvent(event->GetValue());
}

#include "appData.h"

void IValueFrame::CValueEventContainer::SetAt(const CValue &cKey, CValue &cVal)
{
	number_t number = cKey.GetNumber();

	CValueEvent *eventValue = NULL;

	if (m_controlEvent->GetEventCount() < number.ToUInt()) {
		return;
	}

	Event *event = m_controlEvent->GetEvent(number.ToUInt());

	if (cVal.ConvertToValue(eventValue)) {
		event->SetValue(eventValue->GetString());
	}
	else {
		event->SetValue(wxEmptyString);
	}
}

CValue IValueFrame::CValueEventContainer::GetAt(const CValue &cKey)
{
	number_t number = cKey.GetNumber();

	if (m_controlEvent->GetEventCount() < number.ToUInt())
		return CValue();

	Event *event = m_controlEvent->GetEvent(number.ToUInt());
	if (!event) {
		return CValue();
	}

	wxString eventValue = event->GetValue();

	if (eventValue.IsEmpty())
		return CValue();

	return new CValueEvent(eventValue);
}

bool IValueFrame::CValueEventContainer::Property(const CValue &cKey, CValue &cValueFound)
{
	wxString key = cKey.GetString();

	for (unsigned int idx = 0; idx < m_controlEvent->GetEventCount(); idx++) {

		Event *event = m_controlEvent->GetEvent(idx); 	
		if (!event)
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
	std::vector<SEng> aMethods = {
	{"property", "property(key, valueFound)"},
	{"count", "count()"}
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());

	std::vector<SEng> aAttributes;

	for (unsigned int idx = 0; idx < m_controlEvent->GetEventCount(); idx++) {
		Event *event = m_controlEvent->GetEvent(idx);
		if (!event)
			continue;
		aAttributes.push_back(event->GetName());
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void IValueFrame::CValueEventContainer::SetAttribute(attributeArg_t& aParams, CValue &cVal)
{
	CValueEvent *eventValue = NULL;

	if (m_controlEvent->GetEventCount() < (unsigned int)aParams.GetIndex())
		return;

	Event *event = m_controlEvent->GetEvent(aParams.GetIndex());

	if (cVal.ConvertToValue(eventValue)) {
		event->SetValue(eventValue->GetString());
	}
	else {
		event->SetValue(wxEmptyString);
	}
}

CValue IValueFrame::CValueEventContainer::GetAttribute(attributeArg_t &aParams)
{
	if (m_controlEvent->GetEventCount() < (unsigned int)aParams.GetIndex())
		return CValue();

	Event *event = m_controlEvent->GetEvent(aParams.GetIndex());

	if (!event)
		return CValue();

	wxString eventValue = event->GetValue(); 

	if (eventValue.IsEmpty())
		return CValue();

	return new CValueEvent(eventValue);
}

#include "compiler/valueType.h"

CValue IValueFrame::CValueEventContainer::Method(methodArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enControlProperty:
		return Property(aParams[0], aParams.GetParamCount() > 1 ? aParams[1] : CValue());
	case enControlCount:
		return Count();
	}

	return CValue();
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(IValueFrame::CValueEventContainer, "eventContainer", CValueEventContainer, TEXT2CLSID("VL_EVCT"));
