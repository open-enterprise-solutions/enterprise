#include "propertySource.h"
#include "backend/propertyManager/property/variant/variantSource.h"   // GetGuidByID / GetIdByGuid — the hop <-> guid resolve
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary)
#include "backend/sourceDescription.h"       // ibSourceDescription / ibSourceHop (the id path the variant holds)
#include "backend/fileSystem/fs.h"           // ibReaderMemory / ibWriterMemory — the guid-keyed node blob


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibValueTypes& type) const
{
	return CreateVariantData(property, ibTypeDescription(ibValue::GetIDByVT(type)));
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibClassID& clsid) const
{
	return CreateVariantData(property, ibTypeDescription(clsid));
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibTypeDescription& typeDesc) const
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataSource(propFactory, typeDesc);
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibMetaID& id) const
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataSource(propFactory, id);
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibGuid& id, bool fillTypeDesc) const
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataSource(propFactory, id, fillTypeDesc);
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibSourceDescription& desc) const
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataSource(propFactory, desc);
}

////////////////////////////////////////////////////////////////////////
ibMetaID ibPropertySource::GetValueAsSource() const { return get_cell_variant<ibVariantDataSource>()->GetSource(); }
ibGuid ibPropertySource::GetValueAsSourceGuid() const { return get_cell_variant<ibVariantDataSource>()->GetSourceGuid(); }
ibTypeDescription& ibPropertySource::GetValueAsTypeDesc(bool fillTypeDesc) const { return get_cell_variant<ibVariantDataSource>()->GetSourceTypeDesc(fillTypeDesc); }
void ibPropertySource::SetValue(const ibMetaID& val) { m_propValue = CreateVariantData(m_owner, val); }
void ibPropertySource::SetValue(const ibGuid& val, bool fillTypeDesc) { m_propValue = CreateVariantData(m_owner, val, fillTypeDesc); }
void ibPropertySource::SetValue(const ibTypeDescription& val) { m_propValue = CreateVariantData(m_owner, val); }
void ibPropertySource::SetValue(const ibSourceDescription& val) { m_propValue = CreateVariantData(m_owner, val); }
////////////////////////////////////////////////////////////////////////

ibSourceDescription& ibPropertySource::GetValueAsSourceDesc() const { return get_cell_variant<ibVariantDataSource>()->GetSourceDesc(); }
const std::vector<ibSourceHop>& ibPropertySource::GetValueAsPath() const { return get_cell_variant<ibVariantDataSource>()->GetSourceDesc().GetPath(); }
wxString ibPropertySource::GetValueAsString() const { wxString s; get_cell_variant<ibVariantDataSource>()->Write(s); return s; }
////////////////////////////////////////////////////////////////////////

bool ibPropertySource::IsDotWalk() const
{
	// Cheap: the variant just measures the stored path length.
	return get_cell_variant<ibVariantDataSource>()->IsDotWalk();
}
////////////////////////////////////////////////////////////////////////

const ibBackendSourceColumn* ibPropertySource::GetSourceAttributeObject() const {
	return get_cell_variant<ibVariantDataSource>()->GetSourceAttributeObject();
}

std::vector<ibBackendFormAttributeValue*> ibPropertySource::GetSourceList() const {
	// Via the variant (which holds the owning control's type factory) — same path
	// as GetSourceAttributeObject; no dependency on the frontend control type.
	std::vector<ibBackendFormAttributeValue*> out;
	get_cell_variant<ibVariantDataSource>()->GetSourceList(out);
	return out;
}

////////////////////////////////////////////////////////////////////////

bool ibPropertySource::IsEmptyProperty() const {
	return get_cell_variant<ibVariantDataSource>()->IsEmptySource();
}

////////////////////////////////////////////////////////////////////////

//base property for "source"
bool ibPropertySource::SetDataValue(const ibValue& varPropVal)
{
	//varPropVal.GetString();
	return false;
}

bool ibPropertySource::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = m_propValue.GetString();
	return true;
}

// READ / WRITE — the DUMB serializer: the id path verbatim, no metadata, no guids. This is the normal on-disk format.
bool ibPropertySource::ReadNodeValue(const ibDataValue& value)
{
	return ibSourceDescriptionMemory::ReadNode(value, GetValueAsSourceDesc());
}

bool ibPropertySource::WriteNodeValue(ibDataValue& value) const
{
	return ibSourceDescriptionMemory::WriteNode(value, GetValueAsSourceDesc());
}

// COPY / PASTE — their OWN binary of the source description: each metaobject hop rides its GUID instead of a raw id.
// GetGuidByID == the metaobject's GetCommonGuid, which auto-picks the copy-guid while the object is marked for copy.
// PASTE resolves each guid back through GetIdByGuid → THIS config's live id: on a paste the mark landed on the NEW
// object (paste-guid == copy-guid, ibControlPasteGuard), so it lands on IT, not the surviving original. HEAD (idx 0)
// is a form-local attribute id → always raw. This format is TRANSIENT — it lives only in the clipboard / paste
// transaction; the first normal WriteNodeValue re-emits the plain raw path.
static const unsigned char kHopRaw  = 0, kHopGuid  = 1;
static const unsigned char kTypeRaw = 0, kTypeMeta = 1;

bool ibPropertySource::CopyNodeValue(ibDataValue& value) const
{
	const ibVariantDataSource* variant = get_cell_variant<ibVariantDataSource>();
	const std::vector<ibSourceHop>& path = GetValueAsPath();
	ibWriterMemory writer;
	writer.w_u32((unsigned int)path.size());
	size_t idx = 0;
	for (const ibSourceHop& hop : path) {
		const ibGuid guid = (idx > 0) ? variant->GetGuidByID((ibMetaID)hop.m_id) : wxNullGuid;   // head is form-local -> raw
		if (guid.isValid()) { writer.w_u8(kHopGuid); writer.w_stringZ(guid.str()); }
		else                { writer.w_u8(kHopRaw);  writer.w_u32((unsigned int)hop.m_id); }
		const ibGuid typeGuid = IsMetaValue(hop.m_type) ? variant->GetGuidByID((ibMetaID)(hop.m_type & kIbClsidBodyMask)) : wxNullGuid;
		if (typeGuid.isValid()) { writer.w_u8(kTypeMeta); writer.w_u8((unsigned char)clsid_kind(hop.m_type)); writer.w_stringZ(typeGuid.str()); }
		else                    { writer.w_u8(kTypeRaw);  writer.w_u64((unsigned wxLongLong_t)hop.m_type); }
		idx++;
	}
	value = ibDataValue::Binary(writer.buffer());
	return true;
}

bool ibPropertySource::PasteNodeValue(const ibDataValue& value)
{
	const wxMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen() == 0)
		return true;
	const ibVariantDataSource* variant = get_cell_variant<ibVariantDataSource>();
	ibSourceDescription& desc = GetValueAsSourceDesc();
	desc.ClearSource();
	ibReaderMemory reader(data);
	if (reader.elapsed() < static_cast<int>(sizeof(u32)))
		return true;
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		// A HOP HERE IS VARIABLE-LENGTH (a tag, then either a guid string or a raw id, then the same
		// again for the type), so the only honest guard is "is there anything left at all" before
		// each field-group. A blob shorter than its own count stops with the hops it really had —
		// the same rule as sourceDescription.cpp, where reading past the end was found first.
		if (reader.eof())
			break;
		ibSourceId id = wxNOT_FOUND;
		if (reader.r_u8() == kHopGuid) {
			const ibGuid guid(reader.r_stringZ());
			id = (ibSourceId)variant->GetIdByGuid(guid);   // guid -> live id (the pasted object's)
		}
		else {
			id = (ibSourceId)reader.r_u32();
		}
		ibClassID type = g_valueUndefinedCLSID;
		if (reader.eof()) {                 // id read, type missing — take the hop as typeless
			desc.AppendSource(id, type);
			break;
		}
		if (reader.r_u8() == kTypeMeta) {
			const ibClassKind kind = (ibClassKind)reader.r_u8();
			const ibGuid typeGuid(reader.r_stringZ());
			type = make_clsid_dynamic((ibClassID)variant->GetIdByGuid(typeGuid), kind);
		}
		else {
			type = (ibClassID)reader.r_u64();
		}
		desc.AppendSource(id, type);
	}
	return true;
}
