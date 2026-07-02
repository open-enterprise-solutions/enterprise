#include "objectSelector.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — selector reads

/////////////////////////////////////////////////////////////////////////
// Shared keyset-cursor step — see ibValueSelectorDataObject. The statement
// is built ONCE; each call fetches the single row after the anchor.
/////////////////////////////////////////////////////////////////////////

bool ibValueSelectorDataObject::FetchNext()
{
	if (!m_query) {
		// Build the statement once: From the source + the effective ORDER BY (identity
		// tail). Reused across every Next() — only the anchor (a bound param) changes.
		m_query = std::make_unique<ibDataQueryBuilder>();
		m_query->From(GetQueryable());
		m_effective = ibDataQueryBuilder::EffectiveSort(GetQueryable(),
			std::vector<ibQuerySortItem>());
	}

	ibReadPageRequest page;
	page.m_direction = ibFetchDirection::Forward;
	page.m_count     = 1;
	ApplyAnchor(page);   // sets the keyset anchor (no-op on the first row)

	ibDataQueryResult selection = m_query->Execute(page);
	if (!selection.Next())
		return false;   // exhausted / empty — anchor untouched

	CaptureAnchor(selection);    // re-anchor on the fetched row (before MaterialiseRow uses the key)
	MaterialiseRow(selection);
	return true;
}

/////////////////////////////////////////////////////////////////////////
// Record selector (catalog / document / chart) — anchor = m_objGuid.
/////////////////////////////////////////////////////////////////////////

void ibValueSelectorRecordDataObject::Reset()
{
	// Anchor = the start (m_objGuid invalid). The statement / page cache persist.
	m_objGuid.reset(); m_newObject = false;
	m_listObjectValue.clear();
	// Designer mode never iterates (Next() returns false); GetPropVal reads these
	// designer values directly, so populate them up front.
	if (appData->DesignerMode()) {
		for (const auto object : m_metaObject->GetAttributeArrayObject())
			m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
		for (const auto object : m_metaObject->GetTableArrayObject())
			m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObjectRef(this, object));
	}
}

bool ibValueSelectorRecordDataObject::ApplyAnchor(ibReadPageRequest& page) const
{
	if (!m_objGuid.isValid())
		return false;   // first row — no keyset clause
	page.m_hasAnchor  = true;   // the keyset cursor rides in m_anchorSortValues (the selector's effective sort)
	return true;
}

void ibValueSelectorRecordDataObject::CaptureAnchor(const ibDataQueryResult& selection)
{
	// The anchor IS the row's uuid — the identity tail's last (unique) column. The
	// driver trims the CHAR pad tail now, so the uuid reads back clean (no padded-guid).
	m_objGuid = ibGuid(selection.GetValue(m_effective.back().m_col).GetString());
}

void ibValueSelectorRecordDataObject::MaterialiseRow(const ibDataQueryResult& selection)
{
	// Self reference (keyed by the object's metaID, the way GetPropVal expects) + every
	// non-reference attribute + the row's tabular sections.
	m_listObjectValue.clear();
	m_listObjectValue.insert_or_assign(m_metaObject->GetMetaID(),
		selection.GetValue(m_metaObject->GetDataReference()));
	for (const auto object : m_metaObject->GetAttributeArrayObject())
		if (!m_metaObject->IsDataReference(object->GetMetaID()))
			m_listObjectValue[object->GetMetaID()] = selection.GetValue(object);
	for (const auto object : m_metaObject->GetTableArrayObject()) {
		ibValueTabularSectionDataObjectRef* tabularSection = new ibValueTabularSectionDataObjectRef(this, object);
		tabularSection->LoadData(m_objGuid);
		m_listObjectValue.insert_or_assign(object->GetMetaID(), tabularSection);
	}
}

/////////////////////////////////////////////////////////////////////////
// Register selector (info / accum / accounting) — anchor = effective-sort values.
/////////////////////////////////////////////////////////////////////////

void ibValueSelectorRegisterDataObject::Reset()
{
	// Anchor = the start. No buffer, no key->row map — just the current row + anchor.
	m_keyValues.clear();
	m_current.clear();
	m_anchorSort.clear();
}

bool ibValueSelectorRegisterDataObject::ApplyAnchor(ibReadPageRequest& page) const
{
	if (m_anchorSort.empty())
		return false;   // first row — no keyset clause
	page.m_hasAnchor        = true;
	page.m_anchorSortValues = m_anchorSort;   // effective-sort order; no row-key guid
	return true;
}

void ibValueSelectorRegisterDataObject::CaptureAnchor(const ibDataQueryResult& selection)
{
	// A register has no single row-key — anchor on every effective-sort column, bound
	// in EXACTLY this order by the door's BuildAnchorPredicate.
	m_anchorSort.clear();
	m_anchorSort.reserve(m_effective.size());
	for (const auto& c : m_effective)
		if (c.m_col != nullptr)
			m_anchorSort.push_back(selection.GetValue(c.m_col));
}

void ibValueSelectorRegisterDataObject::MaterialiseRow(const ibDataQueryResult& selection)
{
	// Identity (the shape GetRecordManager / GetPropVal expect) + the row's attributes.
	m_keyValues.clear();
	if (m_metaObject->HasRecorder()) {
		ibValueMetaObjectAttributePredefined* attrRecorder = m_metaObject->GetRegisterRecorder();
		ibValueMetaObjectAttributePredefined* attrLine     = m_metaObject->GetRegisterLineNumber();
		wxASSERT(attrRecorder && attrLine);
		m_keyValues[attrRecorder->GetMetaID()] = selection.GetValue(attrRecorder);
		m_keyValues[attrLine->GetMetaID()]     = selection.GetValue(attrLine);
	}
	else {
		for (const auto object : m_metaObject->GetGenericDimensionArrayObject())
			m_keyValues[object->GetMetaID()] = selection.GetValue(object);
	}

	m_current.clear();
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
		m_current[object->GetMetaID()] = selection.GetValue(object);
}
