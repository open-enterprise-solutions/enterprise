#ifndef _VALUE_EVENT_H__
#define _VALUE_EVENT_H__

#include "backend/compiler/value/value.h"

//Поддержка массивов
class CValueEvent : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueEvent);
public:
	wxString m_eventName;
public:

	CValueEvent();
	CValueEvent(const wxString &eventName);

	virtual bool Init(CValue **paParams, const long lSizeArray);

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	virtual inline bool IsEmpty() const {
		return m_eventName.IsEmpty(); 
	}

	virtual ~CValueEvent();
};

#endif