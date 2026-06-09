////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — FindByNumber via WhereLike / WhereCompare

ibValueReferenceDataObject* ibValueManagerDataObjectDocument::FindByNumber(const ibValue& vNumber, const ibValue& vPeriod)
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);

	ibValueMetaObjectAttributePredefined* attributeNumber = m_metaObject->GetDocumentNumber();
	ibValueMetaObjectAttributePredefined* attributeDate   = m_metaObject->GetDocumentDate();
	wxASSERT(attributeNumber && attributeDate);

	// Find by number (LIKE), optionally constrained to documents on/before the
	// given period (date <= period). The L3 door closes the FB FIRST / others
	// LIMIT fork; values ride as bound Consts. (The legacy path appended the
	// period predicate AFTER "LIMIT 1;" against the WRONG attribute — that bug
	// dies with the move to a structured predicate.)
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable()).WhereLike(attributeNumber, attributeNumber->AdjustValue(vNumber));
		if (!vPeriod.IsEmpty())
			q.WhereCompare(attributeDate, ibQueryFilterOp::LessEqual, attributeDate->AdjustValue(vPeriod));

		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult sel = q.Select(page);
		if (sel.Next()) {
			const ibGuid foundedGuid = sel.GetValue(m_metaObject->GetQueryable()->GetIdentitySort().back().m_col).GetString();   // uuid identity column
			if (foundedGuid.isValid())
				return ibValueReferenceDataObject::Create(m_metaObject, foundedGuid);
		}
	}
	catch (...) { /* fall through to an empty reference */ }
	return ibValueReferenceDataObject::Create(m_metaObject);
}
