#include "variantSource.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/commonObject.h"        // ibValueMetaObjectCompositeData
#include "backend/metaCollection/attribute/metaAttributeObject.h" // ibValueMetaObjectAttributeBase
#include "backend/objCtor.h"                                       // ibCtorMetaValueType (DoRefreshTypeDesc tabular-section check)

// Defined further down; forward-declared so the type refresh (above them) can use it.
static const ibValueMetaObject* ResolveGateMeta(const ibBackendTypeSourceFactory* owner, const ibSourceDescription& srcDesc);

////////////////////////////////////////////////////////////////////////////

void ibVariantDataAttributeSource::DoSetFromMetaId(const ibMetaID& id)
{
	// The binding head is a FORM attribute (the gate): its Type IS the attribute's own Type, not
	// a metadata field lookup. A whole-attribute binding's leaf id equals the attribute id (a
	// form-local id), so without this the Type cannot resolve and falls back to a generic Table.
	// Deeper field-id leaves fall through to the base metadata resolve below.
	if (m_ownerSrcProperty != nullptr && id != wxNOT_FOUND) {
		ibBackendFormAttributeValue* holder = m_ownerSrcProperty->FindSourceHolder(id);
		if (holder != nullptr) {
			m_typeDesc = holder->GetTypeDesc();
			return;
		}
	}

	ibVariantDataAttribute::DoSetFromMetaId(id);
}

void ibVariantDataAttributeSource::DoRefreshTypeDesc()
{
	if (m_ownerSrcProperty != nullptr) {

		const ibMetaData* metaData = m_ownerSrcProperty->GetMetaData();
		wxASSERT(metaData);

		// Validate tabular-section types against the binding's GATE metaobject: the START attribute's
		// type (from the control's own source path) walked to the table — NOT a single runtime source
		// object (null at designer, blind to custom object attributes). On copy / section deletion the
		// gate no longer owns the stale section → it is cleared. Gate unknown → keep (can't validate).
		const ibValueMetaObject* gateMeta = ResolveGateMeta(m_ownerSrcProperty, m_ownerSrcProperty->GetSourceDesc());

		std::set<ibClassID> clear_list;
		if (gateMeta != nullptr) {
			for (auto clsid : m_typeDesc.GetClsidList()) {
				const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
				if (typeCtor != nullptr && ::IsTabularSection(clsid)) {
					const ibValueMetaObject* metaTable = typeCtor->GetMetaObject();
					if (metaTable == nullptr || metaTable->GetParent() != gateMeta)
						clear_list.insert(clsid);
				}
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

// Each binding starts at a form ATTRIBUTE (the GATE), then resolves its deeper
// hops in the metadata. The canonical split: path[0] is gated to the FORM's own
// attribute table (a form-local id — scoped so a COPIED form binds its OWN
// attribute, never config-wide); path[i>0] are real config field / reference
// metaIds resolved CONFIG-WIDE. The ids are config-unique and stored copy-aware
// (copy-guids), so a metaobject COPY remaps the deeper hops here WITHOUT relying
// on the attribute's stored Type (which a copy does not remap).

// The binding's GATE metaobject = the START attribute's TYPE metaobject (the object whose tabular
// sections a section-binding references). Used to validate section types per BINDING attribute
// (main OR custom object) rather than a single runtime source object — and it resolves at designer
// (no runtime source) since it comes from the attribute's Type, not a live object.
static const ibValueMetaObject* ResolveGateMeta(const ibBackendTypeSourceFactory* owner,
	const ibSourceDescription& srcDesc)
{
	if (owner == nullptr || !srcDesc.IsOk()) return nullptr;
	// FAMILY-BLIND gate: the head attribute's LIVE source value hands out its metaobject directly —
	// a metaobject source (catalog / document list) yields its composite, a queryable dynamic list
	// yields null (no metaobject → no tabular section to validate). Replaces the old
	// clsid → GetTypeCtor → ConvertToMetaValue gate, which assumed every source IS a metaobject.
	ibBackendFormAttributeValue* holder = owner->FindSourceHolder(srcDesc.GetFirst());
	ibSourceDataObject* source = holder != nullptr ? holder->GetSourceValue() : nullptr;
	return source != nullptr ? source->GetSourceMetaObject() : nullptr;
}

// The dot-walk lives on the OWNER factory (ibBackendTypeSourceFactory::WalkSource) — it owns the
// source list + metadata. These accessors just hand it the source-id path.

wxString ibVariantDataSource::MakeString() const
{
	bool valid = false;
	wxString text;
	if (m_ownerProperty != nullptr)
		m_ownerProperty->WalkSource(m_sourceDesc.GetPath(), &valid, &text);
	return valid ? text : _("<not selected>");
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
	bool valid = false;
	if (m_ownerProperty != nullptr)
		m_ownerProperty->WalkSource(m_sourceDesc.GetPath(), &valid, nullptr);
	return !valid;
}

const ibBackendSourceColumn* ibVariantDataSource::GetSourceAttributeObject() const
{
	// Just hand the source-id path to the OWNER factory's dot — it resolves the leaf column
	// (null for a whole-attribute binding or a broken path).
	return m_ownerProperty != nullptr ? m_ownerProperty->WalkSource(m_sourceDesc.GetPath()) : nullptr;
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
	// The Type is editable ONLY while NO source is chosen — asked through the SAME source-backend dot
	// (WalkSource) the rest of this variant resolves through. Richer than a raw leaf peek: it also
	// reports a leaf that no longer resolves (a DELETED source) as empty, so the Type frees up again.
	// A resolvable source locks the Type selector (RefreshChildren sets ReadOnly = !IsPropAllowed).
	return IsEmptySource();
}