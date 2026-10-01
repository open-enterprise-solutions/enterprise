#include "propertyList.h"
#include "backend/serialize/dataBuilder.h"


//base property for "list"
bool ibPropertyList::SetDataValue(const ibValue& varPropVal)
{
	if (!m_functor->Invoke(this))
		return false;

	for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
		const ibValue* selValue = m_listPropValue.GetItemValue(idx);
		if ((selValue != nullptr && *selValue == varPropVal) || (selValue == nullptr && varPropVal == wxEmptyValue)) {
			SetValue(stringUtils::IntToStr(m_listPropValue.GetItemId(idx)));
			return true;
		}
	}
	return false;
};

bool ibPropertyList::GetDataValue(ibValue& pvarPropVal) const
{
	if (!m_functor->Invoke(const_cast<ibPropertyList*>(this)))
		return false;

	for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
		if (m_listPropValue.GetItemId(idx) == GetValueAsInteger()) {
			pvarPropVal = m_listPropValue.GetItemValue(idx);
			return true;
		}
	}
	return false;
};

bool ibPropertyList::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyList::SetValue((long)value.AsInt());
	return true;
}

bool ibPropertyList::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::Int(ibPropertyList::GetValueAsInteger());
	return true;
}