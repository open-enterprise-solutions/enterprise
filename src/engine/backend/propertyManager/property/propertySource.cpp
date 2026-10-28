#include "propertySource.h"
#include "backend/propertyManager/property/variant/variantSource.h"   // GetGuidByID / GetIdByGuid — the hop <-> guid resolve
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (Binary)
#include "core/description/sourceDescription.h"       // ibSourceDescription / ibSourceHop (the id path the variant holds)
#include "core/memory/memoryStream.h"           // ibReaderMemory / ibWriterMemory — the guid-keyed node blob
#include "backend/functionalOption/functionalOptionGate.h"   // ibFunctionalOptionGate::AnyUnavailable — IsAvailable


////////////////////////////////////////////////////////////////////////

ibVariant ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibValueTypes& type)
{
	return CreateVariantData(property, ibTypeDescription(ibValue::GetIDByVT(type)));
}

ibVariant ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibClassID& clsid)
{
	return CreateVariantData(property, ibTypeDescription(clsid));
}

ibVariant ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibTypeDescription& typeDesc)
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return ibVariant();
	return ibVariant(new ibVariantSource(propFactory, typeDesc));
}

ibVariant ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibMetaID& id)
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return ibVariant();
	return ibVariant(new ibVariantSource(propFactory, id));
}

ibVariant ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibGuid& id, bool fillTypeDesc)
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return ibVariant();
	return ibVariant(new ibVariantSource(propFactory, id, fillTypeDesc));
}

ibVariant ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibSourceDescription& desc)
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return ibVariant();
	return ibVariant(new ibVariantSource(propFactory, desc));
}

////////////////////////////////////////////////////////////////////////
ibMetaID ibPropertySource::GetValueAsSource() const { return get_cell_variant<ibVariantSource>()->GetSource(); }
ibGuid ibPropertySource::GetValueAsSourceGuid() const { return get_cell_variant<ibVariantSource>()->GetSourceGuid(); }
ibTypeDescription& ibPropertySource::GetValueAsTypeDesc(bool fillTypeDesc) const { return get_cell_variant<ibVariantSource>()->GetSourceTypeDesc(fillTypeDesc); }
void ibPropertySource::SetValue(const ibMetaID& val) { m_propValue = CreateVariantData(m_owner, val); }
void ibPropertySource::SetValue(const ibGuid& val, bool fillTypeDesc) { m_propValue = CreateVariantData(m_owner, val, fillTypeDesc); }
void ibPropertySource::SetValue(const ibTypeDescription& val) { m_propValue = CreateVariantData(m_owner, val); }
void ibPropertySource::SetValue(const ibSourceDescription& val) { m_propValue = CreateVariantData(m_owner, val); }
////////////////////////////////////////////////////////////////////////

ibSourceDescription& ibPropertySource::GetValueAsSourceDesc() const { return get_cell_variant<ibVariantSource>()->GetSourceDesc(); }
const std::vector<ibSourceHop>& ibPropertySource::GetValueAsPath() const { return get_cell_variant<ibVariantSource>()->GetSourceDesc().GetPath(); }
ibString ibPropertySource::GetValueAsString() const { return m_propValue.GetString(); }
////////////////////////////////////////////////////////////////////////

bool ibPropertySource::IsDotWalk() const
{
	// Cheap: the variant just measures the stored path length.
	return get_cell_variant<ibVariantSource>()->IsDotWalk();
}
////////////////////////////////////////////////////////////////////////

const ibBackendSourceColumn* ibPropertySource::GetSourceAttributeObject() const {
	return get_cell_variant<ibVariantSource>()->GetSourceAttributeObject();
}

std::vector<ibBackendFormAttributeValue*> ibPropertySource::GetSourceList() const {
	// Via the variant (which holds the owning control's type factory) — same path
	// as GetSourceAttributeObject; no dependency on the frontend control type.
	std::vector<ibBackendFormAttributeValue*> out;
	get_cell_variant<ibVariantSource>()->GetSourceList(out);
	return out;
}

////////////////////////////////////////////////////////////////////////

bool ibPropertySource::IsEmptyProperty() const {
	return get_cell_variant<ibVariantSource>()->IsEmptySource();
}

bool ibPropertySource::IsAvailable() const {
	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	if (owner == nullptr || !ibFunctionalOptionGate::AnyUnavailable(owner->GetMetaData()))
		return true;
	return get_cell_variant<ibVariantSource>()->IsSourceAvailable();
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
	pvarPropVal = ibString(m_propValue.GetString());
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
	const ibVariantSource* variant = get_cell_variant<ibVariantSource>();
	const std::vector<ibSourceHop>& path = GetValueAsPath();
	ibWriterMemory writer;
	writer.w_u32((unsigned int)path.size());
	size_t idx = 0;
	for (const ibSourceHop& hop : path) {
		const ibGuid guid = (idx > 0) ? variant->GetGuidByID((ibMetaID)hop.m_id) : wxNullGuid;   // head is form-local -> raw
		if (guid.isValid()) { writer.w_u8(kHopGuid); writer.w_stringZ(guid.str()); }
		else                { writer.w_u8(kHopRaw);  writer.w_u32((unsigned int)hop.m_id); }
		const ibGuid typeGuid = IsMetaValue(hop.m_type) ? variant->GetGuidByID((ibMetaID)clsid_metaID(hop.m_type)) : wxNullGuid;
		if (typeGuid.isValid()) {
			writer.w_u8(kTypeMeta);
			writer.w_u64((unsigned long long)clsid_any_of(hop.m_type));   // its kind and metaclass: the type less its metaID
			writer.w_stringZ(typeGuid.str());
		}
		else                    { writer.w_u8(kTypeRaw);  writer.w_u64((unsigned long long)hop.m_type); }
		idx++;
	}
	value = ibDataValue::Binary(writer.buffer());
	return true;
}

bool ibPropertySource::PasteNodeValue(const ibDataValue& value)
{
	const ibMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen() == 0)
		return true;
	const ibVariantSource* variant = get_cell_variant<ibVariantSource>();
	ibSourceDescription& desc = GetValueAsSourceDesc();
	desc.ClearSource();
	ibReaderMemory reader(data);
	if (reader.elapsed() < static_cast<int>(sizeof(std::uint32_t)))
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
			const ibClassID any = (ibClassID)reader.r_u64();   // the type less its metaID — the pasted object's goes in
			const ibGuid typeGuid(reader.r_stringZ());
			type = make_clsid_dynamic((ibClassID)variant->GetIdByGuid(typeGuid), clsid_kind(any), clsid_metaclass(any));
		}
		else {
			type = (ibClassID)reader.r_u64();
		}
		desc.AppendSource(id, type);
	}
	return true;
}

#include "backend/backend_type.h"   // ibBackendTypeSourceFactory — what the control may be bound to
#include "backend/srcObject.h"        // ibSourceObject — complete for the cast of GetSourceObject (a column's table)
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — a class's picture, by its id
#include "backend/metaData.h"          // GetAvailableCtor — a class, named as its configuration names it
#include "backend/objCtor.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ConvertToMetaIds — a field's reference targets
#include "backend/srcDataObject.h"     // ibSourceDataObject / ibSourceExplorer — an attribute's fields
#include "backend/system/value/valueTable.h"   // g_valueTableCLSID — a table's picture
#include "protocol/protocol.h"     // ibProtocol::ibSchema::ibProperties — the row's keys

#include <algorithm>

namespace {

// The desktop's source window (frontend advpropSource.cpp): a type as its classes' names.
ibString MakeTypeString(const ibMetaData* metaData, const ibTypeDescription& typeDesc)
{
	ibString strDescr;
	for (const ibClassID& clsid : typeDesc.GetClsidList()) {
		const ibCtorAbstractType* const typeCtor = metaData->GetAvailableCtor(clsid);
		if (typeCtor != nullptr)
			strDescr = strDescr.IsEmpty() ? typeCtor->GetClassName() : strDescr + ibT(", ") + typeCtor->GetClassName();
	}
	return strDescr.IsEmpty() ? ibString(ibT("<empty>")) : strDescr;
}

// …a table source, per the class factory.
bool IsTableType(const ibMetaData* metaData, const ibTypeDescription& typeDesc)
{
	if (typeDesc.GetClsidCount() != 1)
		return false;
	const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(typeDesc.GetFirstClsid());
	return ctor != nullptr && ctor->IsTableValue();
}

// …one of its rows, as the attribute tree's are written (docViewFormEditAttribute.cpp): the label, the path from the
// attribute (none: not one to bind to), a reference's targets — opened as the tree opens it.
ibDataNode& AppendSource(ibDataNode& parent, const ibString& label, const std::vector<ibSourceHop>& path,
	const std::vector<ibMetaID>& refTypes, bool tableSection)
{
	ibDataNode& row = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
	row.SetValue(ibProtocol::ibView::ibFormDesigner::Name, label);

	std::vector<ibDataValue> hops;
	for (const ibSourceHop& hop : path) {
		std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
		node->SetValue(ibProtocol::ibView::ibFormDesigner::Id, static_cast<std::int32_t>(hop.m_id));
		if (hop.m_type != g_valueUndefinedCLSID)
			node->SetValue(ibProtocol::ibView::ibFormDesigner::Type, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(hop.m_type)));
		hops.push_back(ibDataValue::Child(node));
	}
	row.AddField(ibProtocol::ibView::ibFormDesigner::Path, ibDataValue::Array(std::move(hops)));

	if (!refTypes.empty()) {
		std::vector<ibDataValue> ids;
		for (const ibMetaID& target : refTypes)
			ids.push_back(ibDataValue::Int(target));
		row.AddField(ibProtocol::ibView::ibFormDesigner::Targets, ibDataValue::Array(std::move(ids)));
		row.SetValue(ibProtocol::ibView::ibFormDesigner::HasComposition, true);
	}
	if (tableSection) {
		row.SetValue(ibProtocol::ibView::ibFormDesigner::TableSection, true);
		row.SetValue(ibProtocol::ibView::ibFormDesigner::Picture, ibBackendPicture::SendPicture(g_valueTableCLSID));   // the desktop's icon_table
	}
	return row;
}

// …a source's columns under a row, each with `prefix` before its id; a table section is not a column.
void AppendExplorerColumns(ibDataNode& parent, const ibSourceExplorer& explorer, const std::vector<ibSourceHop>& prefix,
	const ibMetaData* metaData)
{
	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const ibSourceExplorer* const col = explorer.GetHelper(i);
		if (col == nullptr || col->IsTableSection())
			continue;
		std::vector<ibSourceHop> childPath = prefix;
		childPath.push_back({ col->GetSourceId() });
		AppendSource(parent, col->GetSourceName(), childPath,
			ibValueReferenceDataObject::ConvertToMetaIds(col->GetClsidList(), metaData), false);
	}
}

}

bool ibPropertySource::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	const ibBackendTypeSourceFactory* const factory = get_cell_variant<ibVariantSource>()->GetSourceFactory();
	const ibPropertyObject* const owner = m_owner;   // CONST: the non-const GetMetaData wxFAILs
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;

	if (reader == ibReader::Cell) {
		if (!ibProperty::GetProtocolValue(row, reader))
			return false;

		// …AND BESIDE THE PATH, IN THE SAME VALUE, THE TYPE IT HOLDS (SourceType) — the desktop's Type row beneath the
		// cell (ibPGDataSourceProperty: shown bound, read only): the type as the binding resolves it from its leaf, its
		// words, and whether it may be composite (Multiple — not in a table's shape). Only the protocol carries it:
		// stored, the type is the leaf's (WriteNodeValue — the path alone). Unbound — none.
		const auto held = row.FindProperty(ibProtocol::ibSchema::ibProperties::Value);
		if (held && held->Kind() == ibDataKind::Child && !IsEmptyProperty()) {
			const ibSelectorDataType shape = factory != nullptr ? factory->GetFilterDataType() : ibSelectorDataType::ibSelectorDataType_reference;
			ibDataValue typeDesc;
			ibBackendTypeDescriptionMemory::WriteNode(typeDesc, GetValueAsTypeDesc(), metaData);
			// …written where the value lies in the row: the one under the name, filled on.
			ibDataNode& sourceType = row.Child(ibProtocol::ibSchema::ibProperties::Value).Child(ibProtocol::ibSchema::ibProperties::SourceType);
			sourceType.SetProperty(ibProtocol::ibSchema::ibProperties::Value, typeDesc);
			sourceType.SetValue(ibProtocol::ibSchema::ibProperties::Presentation,
				ibVariant(get_cell_variant<ibVariantSource>()->CloneSourceAttribute()).GetString());
			sourceType.SetValue(ibProtocol::ibSchema::ibProperties::Multiple, shape != ibSelectorDataType::ibSelectorDataType_table);

			// …and the classes the desktop's Type row offered (GetTypesByFilter by the control's shape, with what it
			// holds), in that order (Classes — Type, Name, Picture), as the configuration names them: the client has none
			// to name a type by, and the Type row and its window read them so.
			std::vector<ibClassID> offered;
			ibBackendTypeConfigFactory::GetTypesByFilter(shape, metaData, offered);
			for (const ibClassID& clsid : GetValueAsTypeDesc().GetClsidList()) {
				if (std::find(offered.begin(), offered.end(), clsid) == offered.end())
					offered.push_back(clsid);
			}
			ibDataNode& named = row.Child(ibProtocol::ibSchema::ibProperties::Classes);
			for (const ibClassID& clsid : offered) {
				const ibCtorAbstractType* const ctor = metaData != nullptr
					? metaData->GetAvailableCtor(clsid) : ibValue::GetAvailableCtor(clsid);
				if (ctor == nullptr)
					continue;
				ibDataNode& entry = named.AddChild(0, static_cast<ibMetaID>(named.Children().size() + 1));
				entry.SetValue(ibProtocol::ibSchema::ibProperties::Type, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid)));
				entry.SetValue(ibProtocol::ibSchema::ibProperties::Name, ctor->GetClassName());
				entry.SetValue(ibProtocol::ibSchema::ibProperties::Picture, ibBackendPicture::SendPicture(clsid));
			}
		}

		// What the desktop's source window filtered its tree by (the owner's GetFilterSourceDataType): a table, a column
		// of the table its table box is bound to, or an attribute.
		if (factory != nullptr)
			row.SetValue(ibProtocol::ibSchema::ibProperties::SourceDataType, static_cast<std::int32_t>(factory->GetFilterSourceDataType()));
		return true;
	}

	// ITS EDITOR — ITS WINDOW'S TREE (Sources), the desktop's window's (ProcessAttribute, ProcessTableColumn), written as
	// the attribute tree's rows: the roots and the rows under them; a reference's, asked as the tree opens it
	// (Composition). Asked as the window opens, not with the row: every bindable control's row carried the whole of it.
	if (factory == nullptr || metaData == nullptr)
		return false;
	ibDataNode& sources = row.Child(ibProtocol::ibSchema::ibProperties::Sources);
	const ibSourceDataType filter = factory->GetFilterSourceDataType();
	const std::vector<ibBackendFormAttributeValue*> holders = GetSourceList();
	if (filter != ibSourceDataType::ibSourceDataType_tableColumn) {
		// Each form attribute is the gate: path[0] = its id, then the binding walks its type. A table box — its table
		// sections; a field — its fields, a reference's opened as the tree opens it.
		const bool tableBox = filter == ibSourceDataType::ibSourceDataType_table;
		for (ibBackendFormAttributeValue* holder : holders) {
			if (holder == nullptr)
				continue;
			const std::vector<ibSourceHop> basePrefix{ { holder->GetId() } };
			const ibString rootLabel = holder->GetName() + ibT(" (") + MakeTypeString(metaData, holder->GetTypeDesc()) + ibT(")");
			ibDataNode& root = AppendSource(sources, rootLabel, basePrefix, {}, IsTableType(metaData, holder->GetTypeDesc()));
			ibSourceDataObject* const attrSource = holder->GetSourceValue();
			const ibSourceExplorer* const explorer = attrSource != nullptr ? attrSource->GetSourceExplorer() : nullptr;
			if (explorer == nullptr)
				continue;   // a primitive — a leaf
			for (unsigned int i = 0; i < explorer->GetHelperCount(); ++i) {
				const ibSourceExplorer* const col = explorer->GetHelper(i);
				if (col == nullptr || tableBox != col->IsTableSection())
					continue;   // a table box — only table sections; a field — only fields
				std::vector<ibSourceHop> childPath = basePrefix;
				childPath.push_back({ col->GetSourceId() });
				// The row is the field's identity — its NAME, as the attribute tree's (AppendComposition), not its synonym.
				if (tableBox) {
					AppendSource(root, col->GetSourceName(), childPath, {}, true);
				}
				else {
					const ibString label = col->GetSourceName() + ibT(" (") + MakeTypeString(metaData, col->GetTypeDesc()) + ibT(")");
					AppendSource(root, label, childPath, ibValueReferenceDataObject::ConvertToMetaIds(col->GetClsidList(), metaData), false);
				}
			}
		}
	}
	else {
		// A column: the table its box is bound to (its columns, row-relative) and each object above it (its fields,
		// constant per row); the table and the object themselves are not columns — no path.
		const ibBackendTypeSourceFactory* const parent =
			dynamic_cast<const ibBackendTypeSourceFactory*>(factory->GetSourceObject());
		const std::vector<ibSourceHop> parentPath = parent != nullptr ? parent->GetSourceDesc().GetPath() : std::vector<ibSourceHop>{};
		const ibSourceId boundHead = parent != nullptr ? parent->GetSourceDesc().GetFirst() : wxNOT_FOUND;

		// The bound table may be a nested table section — not a form attribute: walked down from its head.
		if (parentPath.size() >= 2) {
			ibBackendFormAttributeValue* const headHolder = factory->FindSourceHolder(boundHead);
			ibSourceDataObject* const headSrc = headHolder != nullptr ? headHolder->GetSourceValue() : nullptr;
			const ibSourceExplorer* boundNode = headSrc != nullptr ? headSrc->GetSourceExplorer() : nullptr;
			for (size_t i = 1; i < parentPath.size() && boundNode != nullptr; ++i)
				boundNode = boundNode->FindById(parentPath[i].m_id);
			if (boundNode != nullptr)
				AppendExplorerColumns(AppendSource(sources, boundNode->GetSourceName(), {}, {}, true), *boundNode, parentPath, metaData);
		}

		std::vector<ibSourceId> seenRoots;   // GetSourceList is queried under two filters — a root once
		for (ibBackendFormAttributeValue* holder : holders) {
			if (holder == nullptr || std::find(seenRoots.begin(), seenRoots.end(), holder->GetId()) != seenRoots.end())
				continue;
			seenRoots.push_back(holder->GetId());
			ibSourceDataObject* const source = holder->GetSourceValue();
			if (source == nullptr) {
				// A primitive attribute: its whole value, constant per row — a leaf.
				AppendSource(sources, holder->GetName(), { { holder->GetId() } }, {}, false);
				continue;
			}
			const ibSourceExplorer* const explorer = source->GetSourceExplorer();
			if (explorer == nullptr)
				continue;
			if (IsTableType(metaData, holder->GetTypeDesc())) {
				// The bound table; any other — not this column's.
				if (boundHead != wxNOT_FOUND && holder->GetId() != boundHead)
					continue;
				AppendExplorerColumns(AppendSource(sources, holder->GetName(), {}, {}, true), *explorer, parentPath, metaData);
			}
			else {
				AppendExplorerColumns(AppendSource(sources, holder->GetName(), {}, {}, false), *explorer,
					std::vector<ibSourceHop>{ { holder->GetId() } }, metaData);
			}
		}
	}
	return true;
}
