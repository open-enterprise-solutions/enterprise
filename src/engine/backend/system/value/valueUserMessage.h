#ifndef __VALUE_USER_MESSAGE_H__
#define __VALUE_USER_MESSAGE_H__

#include "backend/compiler/value.h"

void ibValueUserMessage_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// A message meant for one field. Text is what is said. Field, DataPath and
// DataKey name where it belongs. Message() posts the text. A form does not
// yet paint it beside the control: nothing on the form reads these fields.
class BACKEND_API ibValueUserMessage : public ibValueStaticMembers<&ibValueUserMessage_BindNames>
{
public:
	ibValueUserMessage();
	virtual ~ibValueUserMessage() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray) override;
	virtual ibString GetString() const override { return m_text; }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

private:
	wxString m_text;
	wxString m_field;
	wxString m_dataPath;
	ibValue  m_dataKey;
};

#endif
