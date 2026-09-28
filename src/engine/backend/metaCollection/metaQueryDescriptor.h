#ifndef __META_QUERY_DESCRIPTOR_H__
#define __META_QUERY_DESCRIPTOR_H__

// The QUERY descriptor — the metaobject-coupled source-descriptor template every query source is registered
// with. Beside genericData.h rather than in it: this template names ibSourceDataObject::ibSourceExplorer, and
// srcDataObject.h itself includes genericData.h, so putting it there would close a cycle. The L4 factory
// header stays metadata-agnostic (base descriptor + factory only); the list-source half, which needs the
// record machinery, stays with it (ibMetaCommandDescriptor, partial/commonObject.h).

#include "backend/query/queryableFactory.h"   // ibQueryableSourceDescriptor + ibSourceDataObject::ibSourceExplorer + ibBackendQueryable

// The QUERY descriptor — the query-identity HALF, TEMPLATED on the queryable type TQueryable + the metaobject type
// TMeta. It CONTAINS the metaobject's queryable (built from it) AND carries the L4 source identity (GetNamespace /
// GetName / CreateQueryable). This is ALL a pure QUERY source needs — a constant is registered only so From(constant)
// resolves; it is NEVER shown as a list, so it leaves the command + row surface at the base's neutral defaults.
template <typename TQueryable, typename TMeta>
class ibMetaQueryDescriptor : public ibQueryableSourceDescriptor
{
public:
	explicit ibMetaQueryDescriptor(TMeta* meta) : m_meta(meta), m_queryable(meta) {}

	wxString GetNamespace() const override { return ibValue::GetNameObjectFromID(m_meta->GetClassType()); }
	wxString GetName() const override { return m_meta->GetName(); }
	const ibBackendQueryable* CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/) override { return &m_queryable; }

	// The contained queryable — the metaobject's GetQueryable() forwards here (stable for the object's life).
	const ibBackendQueryable* GetQueryable() const { return &m_queryable; }

	// WHAT COLUMNS THIS SOURCE HAS — forwarded to the metaobject, which is the only one that knows.
	//
	// ⚠ THIS BELONGS TO THE QUERY HALF, not to the command one. It used to live on
	// ibMetaCommandDescriptor, so every source that is ONLY queryable — a constant, first among
	// them — answered the base's empty default: it appeared in a catalogue as a table with no
	// fields, could be added to a query and offered nothing to select. Asking what a source holds
	// has nothing to do with whether it can be shown as a list.
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override {
		m_meta->FillSourceExplorer(explorer);
	}

	// Restore ROW-KEY from a row's identity VALUE — read the source's PRIMARY-KEY columns off it (the SAME columns
	// the fetch stamps into a node's m_rowKey, so the restore stub matches). UNIVERSAL via the source hop gate: a
	// record's reference yields its self-reference, a register's record-manager decomposes into its composite key.
	// Empty value / no PK columns → the base fallback (the value IS the key).
	std::vector<ibValue> GetRowKeyByValue(const ibValue& value) const override {
		ibSourceDataObject* src = nullptr;
		if (value.IsEmpty() || !value.ConvertToValue(src) || src == nullptr)
			return ibQueryableSourceDescriptor::GetRowKeyByValue(value);
		std::vector<ibValue> rowKey;
		for (const ibBackendQueryColumn* kc : m_queryable.GetPrimaryKeyColumns()) {
			if (kc == nullptr) continue;
			ibValue v;
			src->GetValueBySourceHop(ibSourceHop{ kc->GetColumnId() }, v);
			rowKey.push_back(v);
		}
		return rowKey.empty() ? ibQueryableSourceDescriptor::GetRowKeyByValue(value) : rowKey;
	}

protected:
	TMeta*     m_meta;        // for name / clsid + to build the queryable (non-const, like the old `this`)
	TQueryable m_queryable;   // the contained queryable (ibRecordQueryable / ibRegisterDataQueryable / ibConstantQueryable / …)
};

#endif // !__META_QUERY_DESCRIPTOR_H__
