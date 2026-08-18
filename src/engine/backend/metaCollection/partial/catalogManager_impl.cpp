////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — FindBy* via WhereLike

namespace {
// Shared FindBy*: locate the first row whose `attribute LIKE pattern` and return
// its reference. Routes through the L3 door (the FB FIRST / others LIMIT fork is
// closed by L2; the value rides as a bound Const). Empty reference on no match.
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
		ibDataQueryResult sel = q.Execute(page);
		if (sel.Next()) {
			// ⭐⭐ THE IDENTITY COLUMN IS NAMED, AND THE REFERENCE IS ASKED FOR ITS GUID.
			//
			// Two guesses used to stand on this line. The column was taken as the LAST item of the
			// identity sort, true only while that sort ended with the key — an object with an ordering
			// of its own (an enumeration sorts by Order first) puts something else there. And the guid
			// was read out of the value's TEXT, which for a reference is its PRESENTATION: a
			// description, or "Not found <…>". Identity by appearance is not identity.
			const ibValue rowValue = sel.GetValue(meta->GetDataReference());
			if (const ibValueReferenceDataObject* const found = rowValue.ConvertToType<ibValueReferenceDataObject>())
				return ibValueReferenceDataObject::Create(meta, found->GetGuid().GetGuid());
		}
	}
	catch (...) { /* fall through to an empty reference */ }
	return ibValueReferenceDataObject::Create(meta);
}
} // namespace

ibValueReferenceDataObject* ibValueManagerDataObjectCatalog::FindByCode(const ibValue& cParam) const
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);
	return FindByAttributeLike(m_metaObject, m_metaObject->GetDataCode(), cParam);
}

ibValueReferenceDataObject* ibValueManagerDataObjectCatalog::FindByDescription(const ibValue& cParam) const
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);
	return FindByAttributeLike(m_metaObject, m_metaObject->GetDataDescription(), cParam);
}