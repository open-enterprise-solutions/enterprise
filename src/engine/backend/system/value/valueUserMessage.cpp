#include "valueUserMessage.h"

#include "backend/system/systemManager.h"

enum {
	eText,
	eField,
	eDataPath,
	eDataKey
};

enum {
	eMessage
};

ibValueUserMessage::ibValueUserMessage()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

bool ibValueUserMessage::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray > 0 && paParams != nullptr && paParams[0] != nullptr)
		m_text = paParams[0]->GetString();
	if (lSizeArray > 1 && paParams[1] != nullptr)
		m_field = paParams[1]->GetString();
	if (lSizeArray > 2 && paParams[2] != nullptr)
		m_dataPath = paParams[2]->GetString();
	if (lSizeArray > 3 && paParams[3] != nullptr)
		m_dataKey = *paParams[3];
	return true;
}

void ibValueUserMessage_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Text"));
	helper.AppendProp(wxT("Field"));
	helper.AppendProp(wxT("DataPath"));
	helper.AppendProp(wxT("DataKey"));
	helper.AppendConstructor(4, wxT("UserMessage(text, field, dataPath, dataKey)"));
	helper.AppendProc(wxT("Message"), wxT("Message()"));
}

bool ibValueUserMessage::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum) {
	case eText:     m_text = varPropVal.GetString(); return true;
	case eField:    m_field = varPropVal.GetString(); return true;
	case eDataPath: m_dataPath = varPropVal.GetString(); return true;
	case eDataKey:  m_dataKey = varPropVal; return true;
	}
	return false;
}

bool ibValueUserMessage::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case eText:     pvarPropVal = m_text; return true;
	case eField:    pvarPropVal = m_field; return true;
	case eDataPath: pvarPropVal = m_dataPath; return true;
	case eDataKey:  pvarPropVal = m_dataKey; return true;
	}
	return false;
}

bool ibValueUserMessage::CallAsProc(const long lMethodNum, ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	if (lMethodNum != eMessage)
		return false;
	ibValueSystemFunction::Message(m_text);
	return true;
}

VALUE_TYPE_REGISTER(ibValueUserMessage, "UserMessage", value_to_clsid("VL_UMSG"));
