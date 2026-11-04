////////////////////////////////////////////////////////////////////////////
//	Description : the metaobjects of one kind, by name — managers, metadata, data sources
////////////////////////////////////////////////////////////////////////////

#include "metaObjectCollection.h"

#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject::GetName
#include "backend/backend_exception.h"

// Count and Get, and nothing more: the objects are the configuration's.
void ibValueMetaObjectCollection_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	ibValueMetaObjectCollection::FillCollection(helper, ibT("Get(index | name)"));
}

ibValueMetaObjectCollection::ibValueMetaObjectCollection(ibMetaData* metaData)
	: ibValueCollection(ibValueTypes::TYPE_VALUE, true), m_metaData(metaData)
{
}

long ibValueMetaObjectCollection::FindProp(const ibString& strPropName) const
{
	for (size_t at = 0; at < m_listValue.size(); ++at)
		if (stringUtils::IsSameAs(m_listValue[at]->GetName(), strPropName))
			return (long)at;
	return wxNOT_FOUND;
}

const ibString& ibValueMetaObjectCollection::GetPropName(const long lPropNum) const
{
	static const ibString s_absent;
	if (lPropNum < 0 || lPropNum >= GetNProps())
		return s_absent;
	// A metaobject MAKES its name rather than holding it, so there is nothing to lend: it goes into this thread's one
	// slot, as a map's key of another kind does (valueMap.cpp), and every caller copies it on the spot.
	static thread_local ibString s_made;
	s_made = m_listValue[lPropNum]->GetName();
	return s_made;
}

bool ibValueMetaObjectCollection::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (lPropNum < 0 || lPropNum >= GetNProps())
		return false;
	pvarPropVal = ElementValue(m_listValue[lPropNum]);
	return true;
}

bool ibValueMetaObjectCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING)
		return ibValueCollection::GetAt(varKeyValue, pvarValue);
	const long at = FindProp(varKeyValue.GetString());
	if (at == wxNOT_FOUND)
		ibBackendCoreException::Error(_("Key '%s' not found!"), varKeyValue.GetString());
	pvarValue = ElementValue(m_listValue[at]);
	return true;
}
