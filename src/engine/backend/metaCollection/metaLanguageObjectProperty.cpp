#include "metaLanguageObject.h"

bool ibValueMetaObjectLanguage::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	//if (m_propertyCode == property && m_propertyCode->IsEmptyProperty())
	//	return false;

	if (m_propertyCode == property && !IsValidCode(newValue.GetString()))
		return false;

	return ibValueMetaObject::OnPropertyChanging(property, newValue);
}