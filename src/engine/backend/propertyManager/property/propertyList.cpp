#include "propertyList.h"
#include "backend/serialize/dataBuilder.h"


//base property for "list"
bool ibPropertyList::SetDataValue(const ibValue& varPropVal)
{
	// Matched against the choices as the owner offers them now…
	long id = wxNOT_FOUND;
	bool found = false;
	{
		std::unique_lock<std::shared_mutex> lock(m_listMutex);
		if (!FillListLocked())
			return false;
		for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount() && !found; idx++) {
			const ibValue* selValue = m_listPropValue.GetItemValue(idx);
			if ((selValue != nullptr && *selValue == varPropVal) || (selValue == nullptr && varPropVal == wxEmptyValue)) {
				id = m_listPropValue.GetItemId(idx);
				found = true;
			}
		}
	}
	// …and set outside the lock: setting marks the list for its next fill (DoSetValue).
	if (found)
		SetValue(stringUtils::IntToStr(id));
	return found;
};

bool ibPropertyList::GetDataValue(ibValue& pvarPropVal) const
{
	const long sel = GetValueAsInteger();   // the list filled, if it never was
	std::shared_lock<std::shared_mutex> lock(m_listMutex);
	if (!m_listOffered)
		return false;
	for (unsigned int idx = 0; idx < m_listPropValue.GetItemCount(); idx++) {
		if (m_listPropValue.GetItemId(idx) == sel) {
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
	// What is written is checked against the owner as it is NOW — a list filled before the designer changed what it
	// offers (a form's type, say) would keep a choice that is no longer one.
	const_cast<ibPropertyList*>(this)->FillList();
	value = ibDataValue::Int(ibPropertyList::GetValueAsInteger());
	return true;
}