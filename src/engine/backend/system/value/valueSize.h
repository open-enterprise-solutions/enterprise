#ifndef __VALUE_SIZE_H__
#define __VALUE_SIZE_H__

#include "backend/compiler/value.h"

class BACKEND_API ibValueSize : public ibValue {
	public:

	ibValueSize();
	ibValueSize(const wxSize& size);
	virtual ~ibValueSize() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray);
	virtual wxString GetString() const {
		return typeConv::SizeToString(m_size);
	}

	virtual bool IsEmpty() const {
		return m_size == wxDefaultSize;
	}

	static ibValueMethodHelper m_methodHelper;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return &m_methodHelper;
	}
	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.

	operator wxSize() const { return m_size; }

public:
	wxSize m_size;
};

#endif