#ifndef _VALUE_GUID_H__
#define _VALUE_GUID_H__

#include "backend/compiler/value.h"

class BACKEND_API ibValueGuid : public ibValue {
	public:

	operator ibGuid() const {
		return m_guid;
	}

	ibValueGuid();
	ibValueGuid(const ibGuid &guid);

	virtual bool Init();
	virtual bool Init(ibValue **paParams, const long lSizeArray);

	virtual wxString GetString() const {
		return m_guid;
	}

	//check is empty
	virtual bool IsEmpty() const { 
		return !m_guid.isValid(); 
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibValue &cParam) const {
		return m_guid == ibGuid(cParam.GetString());
	}

	//operator '!='
	virtual bool CompareValueNE(const ibValue &cParam) const {
		return m_guid != ibGuid(cParam.GetString());
	}

private:
	ibGuid m_guid;
};

// ⭐⭐ THE GUID A VALUE CARRIES — asked, not spelled out and read back.
//
// A guid value prints itself, so `ibGuid(value.GetString())` compiles and works and is two conversions
// of something that was already the answer: 36 characters written and parsed for every row of every
// scan. Worse, it reads as if the text were the identity, which is the belief that put a guid's
// SPELLING into a sixteen-byte key column.
//
// The fallback stays, and is the whole reason this is a function rather than a cast at each site: a
// value that genuinely holds a guid's TEXT (a script variable, a parameter someone typed) still
// answers correctly, and the sites that call this do not each decide how to handle that.
BACKEND_API ibGuid GuidOf(const ibValue& value);

#endif // !_VALUEUUID_H__
