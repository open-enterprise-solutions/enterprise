////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of characteristic types manager
////////////////////////////////////////////////////////////////////////////

#include "chartOfCharacteristicTypesManager.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — FindBy* via WhereLike

namespace {
// Shared FindBy*: first row whose `attribute LIKE pattern`, returned as a
// reference. Routes through the L3 door (the FB FIRST / others LIMIT fork and
// the raw statement are gone; the value rides as a bound Const). Empty
// reference on no match — mirrors the catalog / chart-of-accounts managers.
ibValueReferenceDataObject* FindByAttributeLike(const ibValueMetaObjectRecordDataMutableRef* meta,
                                                ibValueMetaObjectAttributePredefined* attr,
                                                const ibValue& cParam)
{
	if (attr == nullptr || cParam.IsEmpty())
		return ibValueReferenceDataObject::Create(meta);
	try {
		ibDataQueryBuilder q;
		q.From(meta->GetQueryable()).WhereLike(attr, attr->AdjustValue(cParam));
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult sel = q.Select(page);
		if (sel.Next()) {
			const ibGuid foundedGuid = sel.GetValue(meta->GetQueryable()->GetIdentitySort().back().m_col).GetString();   // uuid identity column
			if (foundedGuid.isValid())
				return ibValueReferenceDataObject::Create(meta, foundedGuid);
		}
	}
	catch (...) { /* fall through to an empty reference */ }
	return ibValueReferenceDataObject::Create(meta);
}
} // namespace

ibValueReferenceDataObject* ibValueManagerDataObjectChartOfCharacteristicTypes::FindByCode(const ibValue& cParam) const
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);
	return FindByAttributeLike(m_metaObject, m_metaObject->GetDataCode(), cParam);
}

ibValueReferenceDataObject* ibValueManagerDataObjectChartOfCharacteristicTypes::FindByDescription(const ibValue& cParam) const
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);
	return FindByAttributeLike(m_metaObject, m_metaObject->GetDataDescription(), cParam);
}
