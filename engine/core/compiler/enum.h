#ifndef ENUM_BASE_H__
#define ENUM_BASE_H__

#include "value.h"

//***************************************************************************************************
//*                                       Base collection variant                                   *
//***************************************************************************************************

class IEnumerationMethods : public CValue {
	CMethods *m_methods;
public:

	IEnumerationMethods(bool createInstance = false);
	virtual ~IEnumerationMethods();

	virtual CMethods* GetPMethods() const { return m_methods; }
	virtual void PrepareNames() const;

	virtual wxString GetTypeString() const = 0;
	virtual wxString GetString() const = 0;

protected:
	std::vector<wxString> m_aEnumsString;
};

template <typename valT>
class IEnumerationValue : public IEnumerationMethods
{
public:

	IEnumerationValue(bool createInstance = false) : IEnumerationMethods(createInstance) {}

	virtual valT GetEnumValue() = 0;
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

	virtual valT GetEnumValue() = 0;
	virtual void SetEnumValue(valT val) = 0;
};

#endif 