#include "variantSource.h"
#include "backend/metaData.h"

////////////////////////////////////////////////////////////////////////////

void ibVariantDataAttributeSource::DoSetFromMetaId(const ibMetaID& id)
{
	if (m_ownerSrcProperty != nullptr && id != wxNOT_FOUND) {
		const ibSourceObject* srcData = m_ownerSrcProperty->GetSourceObject();
		if (srcData != nullptr) {
			const ibValueMetaObjectCompositeData* metaObject = srcData->GetSourceMetaObject();
			if (metaObject != nullptr && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				m_typeDesc.SetDefaultMetaType(srcData->GetSourceClassType());
				return;
			}
		}
	}

	ibVariantDataAttribute::DoSetFromMetaId(id);
}

#include "backend/objCtor.h"

void ibVariantDataAttributeSource::DoRefreshTypeDesc()
{
	if (m_ownerSrcProperty != nullptr) {

		std::set<ibClassID> clear_list;

		const ibMetaData* metaData = m_ownerSrcProperty->GetMetaData();
		wxASSERT(metaData);
		const ibSourceObject* srcObject = m_ownerSrcProperty->GetSourceObject();

		for (auto clsid : m_typeDesc.GetClsidList()) {
			const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
			if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType_TabularSection) {
				if (srcObject != nullptr) {
					const ibValueMetaObject* metaTable = typeCtor->GetMetaObject();
					if (metaTable == nullptr) clear_list.insert(clsid);
					else if (metaTable->GetParent() != srcObject->GetSourceMetaObject()) clear_list.insert(clsid);
				}
				else if (srcObject == nullptr)clear_list.insert(clsid);
			}
		}

		for (auto clsid : clear_list) { m_typeDesc.ClearMetaType(clsid); }
		if (!m_typeDesc.IsOk()) SetDefaultMetaType();
	}

	ibVariantDataAttribute::DoRefreshTypeDesc();
}

////////////////////////////////////////////////////////////////////////////

// Resolve a column id / guid CONFIG-WIDE through the metadata's own recursive search
// (use_child_filter = true) — a dotted reference path's leaf (and its intermediate
// references) live in OTHER metaobjects, so a source-scoped lookup would miss them and
// the binding would read back as "<not selected>".
template <typename TKey>
static const ibValueMetaObject* ResolveMetaWide(const ibBackendTypeSourceFactory* owner, const TKey& key)
{
	const ibMetaData* metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	return metaData != nullptr ? metaData->FindAnyObjectByFilter(key, true) : nullptr;
}

wxString ibVariantDataSource::MakeString() const
{
	const std::vector<ibMetaID>& path = m_sourceDesc.GetPath();
	if (path.empty()) return _("<not selected>");

	// Full dotted path (e.g. "Producer.Region"), first hop first, leaf last.
	wxString text;
	for (size_t i = 0; i < path.size(); ++i) {
		const ibValueMetaObject* seg = ResolveMetaWide(m_ownerProperty, path[i]);
		if (seg == nullptr || !seg->IsAllowed()) return _("<not selected>");
		if (i > 0) text += wxT(".");
		text += seg->GetName();
	}
	return text;
}

////////////////////////////////////////////////////////////////////////////

ibMetaID ibVariantDataSource::GetIdByGuid(const ibGuid& guid) const
{
	if (!guid.isValid()) return wxNOT_FOUND;
	const ibValueMetaObject* metaObject = ResolveMetaWide(m_ownerProperty, guid);
	return metaObject != nullptr && metaObject->IsAllowed() ? metaObject->GetMetaID() : wxNOT_FOUND;
}

ibGuid ibVariantDataSource::GetGuidByID(const ibMetaID& id) const
{
	if (id == wxNOT_FOUND) return wxNullGuid;
	const ibValueMetaObject* metaObject = ResolveMetaWide(m_ownerProperty, id);
	return metaObject != nullptr && metaObject->IsAllowed() ? metaObject->GetCommonGuid() : wxNullGuid;
}

////////////////////////////////////////////////////////////////////////////

bool ibVariantDataSource::IsEmptySource() const
{
	const ibMetaID leaf = m_sourceDesc.GetLeaf();
	if (leaf == wxNOT_FOUND) return true;
	// Removed / unresolved leaf reads as empty (the attribute may have been deleted from
	// the metadata while a control was still bound to it).
	const ibValueMetaObject* meta = ResolveMetaWide(m_ownerProperty, leaf);
	return meta == nullptr || !meta->IsAllowed();
}

const ibValueMetaObjectAttributeBase* ibVariantDataSource::GetSourceAttributeObject() const
{
	const ibMetaID leaf = m_sourceDesc.GetLeaf();
	if (leaf == wxNOT_FOUND) return nullptr;
	// Read-only resolve off the const GetMetaData chain — no const_cast needed.
	return dynamic_cast<const ibValueMetaObjectAttributeBase*>(ResolveMetaWide(m_ownerProperty, leaf));
}

////////////////////////////////////////////////////////////////////////////

void ibVariantDataSource::SetSource(const ibMetaID& id, bool fillTypeDesc)
{
	// Selecting a direct source resets the path to a single leaf.
	if (id != wxNOT_FOUND)
		m_sourceDesc.SetDefaultSource(id);
	else
		m_sourceDesc.ClearSource();

	if (fillTypeDesc) {
		m_attributeSource->SetFromMetaDesc(id);
		m_typeDescLeaf = id;
	}
}

////////////////////////////////////////////////////////////////////////////

void ibVariantDataSource::SetSourceGuid(const ibGuid& guid, bool fillTypeDesc)
{
	SetSource(GetIdByGuid(guid), fillTypeDesc);
}

////////////////////////////////////////////////////////////////////////////

void ibVariantDataSource::SetSourceTypeDesc(const ibTypeDescription& td)
{
	ResetSource();
	m_attributeSource->SetFromTypeDesc(td);
}

ibTypeDescription& ibVariantDataSource::GetSourceTypeDesc(bool fillTypeDesc) const
{
	// The type is derived from the bound leaf, not serialised. Sync the helper only when
	// the leaf actually changes (e.g. first access after a load) — cheap on the control's
	// per-Update path, and authoritative over any earlier type filter.
	const ibMetaID leaf = m_sourceDesc.GetLeaf();
	if (fillTypeDesc && leaf != wxNOT_FOUND && leaf != m_typeDescLeaf) {
		m_attributeSource->SetFromMetaDesc(leaf);
		m_typeDescLeaf = leaf;
	}
	return m_attributeSource->GetTypeDesc();
}

////////////////////////////////////////////////////////////////////////////

void ibVariantDataSource::ResetSource()
{
	m_attributeSource->SetFromMetaDesc(wxNOT_FOUND);
	m_sourceDesc.ClearSource();
	m_typeDescLeaf = wxNOT_FOUND;
}

////////////////////////////////////////////////////////////////////////////

bool ibVariantDataSource::IsPropAllowed() const
{
	const ibMetaID leaf = m_sourceDesc.GetLeaf();
	if (leaf == wxNOT_FOUND) return true;
	const ibValueMetaObject* metaObject = ResolveMetaWide(m_ownerProperty, leaf);
	if (metaObject != nullptr)
		return !metaObject->IsAllowed();
	return true;
}
