////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of accounts manager - FindByCode/FindByDescription
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccountsManager.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — FindBy* via WhereLike

namespace {
// First row whose `attribute LIKE pattern`, as a reference (empty on no match).
// Routes through the L3 door; FB FIRST / others LIMIT fork closed by L2.
ibValueReferenceDataObject* FindByAttributeLike(const ibValueMetaObjectRecordDataMutableRef* meta,
                                                ibValueMetaObjectAttributePredefined* attr,
                                                const ibValue& cParam)
{
	if (attr == nullptr || cParam.IsEmpty())
		return ibValueReferenceDataObject::Create(meta);
	try {
		ibDataQueryBuilder q;
		q.From(meta->GetQueryable()).WhereLike(attr->GetQueryColumn(), attr->AdjustValue(cParam));
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult sel = q.Execute(page);
		if (sel.Next()) {
			// The identity column by NAME, and the guid from the reference itself — see the same read in
			// catalogManager_impl.cpp for what the two guesses on this line used to cost.
			const ibValue rowValue = sel.GetValue(meta->GetDataReference()->GetQueryColumn());
			if (const ibValueReferenceDataObject* const found = rowValue.ConvertToType<ibValueReferenceDataObject>())
				return ibValueReferenceDataObject::Create(meta, found->GetGuid().GetGuid());
		}
	}
	catch (...) { /* fall through to an empty reference */ }
	return ibValueReferenceDataObject::Create(meta);
}
} // namespace

ibValueReferenceDataObject* ibValueManagerDataObjectChartOfAccounts::FindByCode(const ibValue& cParam) const
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);
	return FindByAttributeLike(m_metaObject, m_metaObject->GetDataCode(), cParam);
}

ibValueReferenceDataObject* ibValueManagerDataObjectChartOfAccounts::FindByDescription(const ibValue& cParam) const
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);
	return FindByAttributeLike(m_metaObject, m_metaObject->GetDataDescription(), cParam);
}
