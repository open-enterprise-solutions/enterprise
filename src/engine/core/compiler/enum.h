#ifndef ENUM_BASE_H__
#define ENUM_BASE_H__

#include "value.h"

//***************************************************************************************************
//*                                       Base collection variant                                   *
//***************************************************************************************************

class IEnumerationWrapper : public CValue {
	CMethods* m_methods;
public:

	IEnumerationWrapper(bool createInstance = false);
	virtual ~IEnumerationWrapper();

	virtual CMethods* GetPMethods() const {
		return m_methods;
	}

	virtual void PrepareNames() const;

	virtual CValue* GetEnumVariantValue() const = 0;

	virtual wxString GetTypeString() const = 0;
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

	virtual bool Init(CValue** aParams) {
		SetEnumValue(
			static_cast<valT>(aParams[0]->ToInt())
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