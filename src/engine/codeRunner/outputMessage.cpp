#include "outputMessage.h"
#include "mainApp.h"

void ibValueOutput::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Message"), 1, "Message(str : string)");
}

bool ibValueOutput::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
		//--- Special:
	case 0:
		static_cast<ibAppCodeRunner*>(wxApp::GetInstance())->AppendOutput(paParams[0]->GetString());
		return true;
	}

	return false;
}

CONTEXT_TYPE_REGISTER(ibValueOutput, "ValueOutput", context_to_clsid("IN_OUTP"))
