#ifndef _VALUE_GUID_H__
#define _VALUE_GUID_H__

#include "backend/compiler/value/value.h"

class BACKEND_API CValueGuid : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueGuid);
public:

	operator Guid() const {
		return m_guid;
	}

	CValueGuid();
	CValueGuid(const Guid &guid);

	virtual bool Init();
	virtual bool Init(CValue **paParams, const long lSizeArray);

	virtual wxString GetString() const { 
		return m_guid; 
	}

	//check is empty
	virtual inline bool IsEmpty() const { 
		return !m_guid.isValid(); 
	}

	//operator '=='
	virtual inline bool CompareValueEQ(const CValue &cParam) const {
		return m_guid == cParam.GetString(); 
	}
	
	//operator '!='
	virtual inline bool CompareValueNE(const CValue &cParam) const { 
		return m_guid != cParam.GetString(); 
	}

private:
	Guid m_guid;
};

#endif // !_VALUEUUID_H__
