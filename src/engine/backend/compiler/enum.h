#ifndef ENUM_BASE_H__
#define ENUM_BASE_H__

#include "value/value.h"

//***************************************************************************************************
//*                                       Base collection variant                                   *
//***************************************************************************************************

class BACKEND_API IEnumerationWrapper : public CValue {
	CMethodHelper* m_methodHelper;
public:

	IEnumerationWrapper(bool createInstance = false);
	virtual ~IEnumerationWrapper();

	virtual CMethodHelper* GetPMethods() const {
		return m_methodHelper;
	}

	virtual void PrepareNames() const;

	virtual CValue* GetEnumVariantValue() const = 0;

	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

protected:
	std::vector<wxString> m_aEnumsString;
};

template <typename valT>
class IEnumerationValue : public IEnumerationWrapper
{
public:

	IEnumerationValue(bool createInstance = false) :
		IEnumerationWrapper(createInstance)
	{
	}

	virtual bool Init() {
		return true;
	}

	virtual bool Init(CValue** paParams, const long lSizeArray) {
		SetEnumValue(
			static_cast<valT>(paParams[0]->GetInteger())
		);
		return true;
	}

	virtual valT GetEnumValue() const = 0;
	virtual void SetEnumValue(valT val) = 0;
};

//***************************************************************************************************
//*                                 Current variant from IEnumerationValue                          *
//***************************************************************************************************

template <typename valT>
class IEnumerationVariant : public CValue
{
public:

	IEnumerationVariant() : CValue(eValueTypes::TYPE_ENUM, true) {}

	virtual valT GetEnumValue() const = 0;
	virtual void SetEnumValue(valT val) = 0;
};

#endif 