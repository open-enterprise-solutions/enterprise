#include "outputMessage.h"
#include "mainApp.h"

void ibValueOutput::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc(wxT("Message"), 1, "Message(str : string)");
}

bool ibValueOutput::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
		//--- Специальные:
	case 0:
		static_cast<ibAppCodeRunner*>(wxApp::GetInstance())->AppendOutput(paParams[0]->GetString());
		return true;
	}

	return false;
}

CONTEXT_TYPE_REGISTER(ibValueOutput, "ValueOutput", string_to_clsid("IN_OUTP"))