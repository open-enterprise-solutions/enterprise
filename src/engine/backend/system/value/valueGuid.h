#ifndef _VALUE_GUID_H__
#define _VALUE_GUID_H__

#include "backend/compiler/value.h"

class BACKEND_API ibValueGuid : public ibValue {
	wxDECLARE_DYNAMIC_CLASS(ibValueGuid);
public:

	operator ibGuid() const {
		return m_guid;
	}

	ibValueGuid();
	ibValueGuid(const ibGuid &guid);

	virtual bool Init();
	virtual bool Init(ibValue **paParams, const long lSizeArray);

	virtual wxString GetString() const {
		return wxString(m_guid.str());
	}

	//check is empty
	virtual bool IsEmpty() const { 
		return !m_guid.isValid(); 
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibValue &cParam) const {
		return m_guid == cParam.GetString(); 
	}
	
	//operator '!='
	virtual bool CompareValueNE(const ibValue &cParam) const { 
		return m_guid != cParam.GetString(); 
	}

private:
	ibGuid m_guid;
};

#endif // !_VALUEUUID_H__
