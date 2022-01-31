#ifndef _VALUEGUID_H__

#include "compiler/value.h"
#include "guid/guid.h"

class CValueGuid : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValueGuid);

public:

	CValueGuid();
	CValueGuid(const Guid &guid);

	virtual bool Init();
	virtual bool Init(CValue **aParams);

	virtual wxString GetTypeString() const { return wxT("guid"); }
	virtual wxString GetString() const { return m_guid; }

	operator Guid() { return m_guid; }

	//check is empty
	virtual inline bool IsEmpty() const override { return !m_guid.isValid(); }
	//operator '=='
	virtual inline bool CompareValueEQ(const CValue &cParam) const { return m_guid == cParam.GetString(); }
	//operator '!='
	virtual inline bool CompareValueNE(const CValue &cParam) const { return m_guid != cParam.GetString(); }

	virtual ~CValueGuid();

private:

	Guid m_guid;
};

#endif // !_VALUEUUID_H__
