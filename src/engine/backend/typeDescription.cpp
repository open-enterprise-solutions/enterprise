#include "typeDescription.h"
#include "backend/fileSystem/fs.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode / ibDataValue (Child + Array)
#include "backend/metaData.h"                 // ibMetaData::GetTypeCtor — clsid <-> type name
#include "backend/objCtor.h"                  // ibCtorMetaValueType::GetClassName / GetClassType

////////////////////////////////////////////////////////////////////////

// node form (mirrors LoadData/SaveData over ibDataValue): Child{ types: [ { TypeId } … ],
// qualifier fields }. Each entry pairs a clsid with only the qualifier that applies
// (number -> precision/scale, date -> fraction, string -> length); a reference type
// has none. One source of truth — ibPropertyType and predefined attributes both call.
bool ibTypeDescriptionMemory::ReadNode(const ibDataValue& value, ibTypeDescription& typeDesc, const ibMetaData* metaData)
{
	const std::shared_ptr<ibDataNode>& root = value.AsChild(); // throws on wrong kind; null when absent
	if (!root)
		return false;

	typeDesc.ClearMetaType();

	// 🛑 EITHER AREA, BECAUSE THE TEXT CANNOT SAY WHICH ONE IT CAME FROM. A node built by the
	// storage layer puts this list in the PROPERTY area — SetProperty, a dozen lines below — while a
	// node PARSED FROM JSON puts every non-object value in the FIELD area, arrays included: the
	// reader decides by the value's kind because "which area a key came from is not in the text"
	// (jsonProvider.cpp, which says so itself).
	//
	// So the two halves of one format disagreed the moment a description made the round trip
	// through text. metadata_set_type's `description` argument exists for exactly that trip - read
	// a type, add a second one to it, send it back - and the list arrived invisible: the types were
	// silently dropped, and a composite type came back as whatever the attribute already held
	// (measured over this server, 2026-09-05, on the first attempt to build one).
	//
	// ⚠ NOT FIXED IN THE JSON READER, though that is where the guess lives: arrays land in fields
	// deliberately and every other tool reads them there (`cells`, `ids`). What is wrong is a READER
	// of a node that may have come from either side insisting on one - so the reader accepts both,
	// and the writer keeps saying it the one way storage expects.
	ibDataValue typesVal = root->GetProperty(wxT("Types"));

	if (typesVal.Kind() != ibDataKind::Array) {
		if (const ibDataValue* fromField = root->FindField(wxT("Types")))
			typesVal = *fromField;
	}

	if (typesVal.Kind() == ibDataKind::Array) {
		for (const ibDataValue& item : typesVal.AsArray()) {
			const std::shared_ptr<ibDataNode>& entry = item.AsChild();
			if (entry == nullptr)
				continue;

			// Prefer the portable NAME (resolved to THIS config's live clsid); the raw
			// TypeId is the same-config fallback when there is no metaData / no such type.
			// ⚠ A NAME IS ONLY EVER ASKED OF THE METADATA, and that is not an oversight: a REFERENCE
			// type belongs to the configuration that declared it, while the value types — String,
			// Number, Date, Boolean — are common to the whole process (Max, 2026-09-05: *"a
			// reference is a type of a particular metadata, and values are common"*). So the name
			// exists for carrying a reference BETWEEN configurations, and everything else is
			// identified by its id, which does not move.
			ibClassID clsid = 0;
			if (metaData != nullptr) {
				const wxString typeName = entry->GetValue<wxString>(wxT("TypeName"));
				if (!typeName.IsEmpty()) {
					if (ibCtorMetaValueType* ctor = metaData->GetTypeCtor(typeName))
						clsid = ctor->GetClassType();
				}
			}

			// EITHER SPELLING OF THE ID. It is written as text now, so that it survives a trip
			// through JSON (see WriteNode); a configuration written before that holds a number, and
			// both mean the same thing.
			if (clsid == 0) {
				if (const ibDataValue* tid = entry->FindField(wxT("TypeId"))) {

					if (tid->Kind() == ibDataKind::String) {
						unsigned long long parsed = 0;
						if (tid->AsString().ToULongLong(&parsed))
							clsid = (ibClassID)parsed;
					}
					else {
						clsid = (ibClassID)tid->AsInt();
					}
				}
			}
			if (clsid != 0)
				typeDesc.AppendMetaType(clsid);
		}
	}

	// restore the shared qualifier block (overwrites the per-type defaults set above)
	ibTypeDescription::ibTypeData data;
	data.SetNumber((unsigned char)root->GetValue<s32>(wxT("Precision")),
		(unsigned char)root->GetValue<s32>(wxT("Scale")),
		root->GetValue<bool>(wxT("NonNegative")));
	data.SetDate((ibDateFractions)root->GetValue<s32>(wxT("DateFraction")));
	data.SetString((unsigned short)root->GetValue<s32>(wxT("Length")),
		(ibAllowedLength)root->GetValue<s32>(wxT("AllowedLength")));
	typeDesc.SetTypeData(data);
	return true;
}

bool ibTypeDescriptionMemory::WriteNode(ibDataValue& value, const ibTypeDescription& typeDesc, const ibMetaData* metaData)
{
	auto root = std::make_shared<ibDataNode>();

	// the type list — the raw clsid plus, when metaData is given, the portable type NAME
	// (built-in "String"/"Number" or reference "CatalogRef.Catalog1") for copy-aware load.
	std::vector<ibDataValue> types;
	for (ibClassID clsid : typeDesc.GetClsidList()) {
		auto entry = std::make_shared<ibDataNode>();
		// 🛑 AS TEXT, BECAUSE A CLASS ID IS 64 BITS AND A JSON NUMBER IS A DOUBLE. String's id is
		// 125750747597698322 and comes back from JSON as 125750747597698320 — near enough to look
		// right, different enough to name nothing. A description that went out of this platform and
		// came back therefore lost every type the reader had to fall back on the id for: sent three
		// types, two arrived, and the primitive vanished without a word (measured 2026-09-05).
		//
		// ⚠ AND THE ID STAYS THE IDENTITY — resolving by NAME instead was tried and is wrong: a
		// reference type's name has the OBJECT'S name inside it (`CatalogRef.Goods`), so a rename
		// silently breaks every description that trusted it, while the id does not move (Max,
		// 2026-09-05: *"if you go looking by knowledge alone, your references will stop working"*).
		// The name beside it is for travel BETWEEN configurations and nothing else.
		//
		// Read back as either — see ReadNode. A configuration written before this holds a number.
		entry->AddField(wxT("TypeId"),
			ibDataValue::String(wxString::Format(wxT("%llu"), (unsigned long long)clsid)));

		if (metaData != nullptr) {
			if (ibCtorMetaValueType* ctor = metaData->GetTypeCtor(clsid))
				entry->AddField(wxT("TypeName"), ibDataValue::String(ctor->GetClassName()));
		}
		types.push_back(ibDataValue::Child(entry));
	}
	root->SetProperty(wxT("Types"), ibDataValue::Array(types));

	// the qualifier — ONE shared block (ibTypeData holds all three), no per-type branching
	const ibTypeDescription::ibTypeData& data = typeDesc.GetTypeData();
	root->SetValue(wxT("Precision"),    (s32)data.GetPrecision());
	root->SetValue(wxT("Scale"),        (s32)data.GetScale());
	root->SetValue(wxT("NonNegative"),  data.IsNonNegative());
	root->SetValue(wxT("DateFraction"), (s32)data.GetDateFraction());
	root->SetValue(wxT("Length"),       (s32)data.GetLength());
	root->SetValue(wxT("AllowedLength"),(s32)data.GetAllowedLength());

	value = ibDataValue::Child(root);
	return true;
}

////////////////////////////////////////////////////////////////////////

// node form: Array[ metaID… ]. One source of truth for the meta-description list.
bool ibMetaDescriptionMemory::ReadNode(const ibDataValue& value, ibMetaDescription& metaDesc)
{
	metaDesc.ClearMetaType();
	for (const ibDataValue& item : value.AsArray()) // empty when absent; throws on wrong kind
		metaDesc.AppendMetaType((ibMetaID)item.AsInt());
	return true;
}

bool ibMetaDescriptionMemory::WriteNode(ibDataValue& value, const ibMetaDescription& metaDesc)
{
	std::vector<ibDataValue> ids;
	for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++)
		ids.push_back(ibDataValue::Int(metaDesc.GetByIdx(idx)));
	value = ibDataValue::Array(ids);
	return true;
}

//**********************************************************************************************
//*   THE COLUMN FORM — a type description that lives in a ROW, written by the column codec     *
//**********************************************************************************************

namespace {

constexpr unsigned char kTypeDescBufferVersion = 1;

void PutU32(wxMemoryBuffer& out, std::uint32_t v)
{
	out.AppendByte(static_cast<char>( v        & 0xFF));
	out.AppendByte(static_cast<char>((v >>  8) & 0xFF));
	out.AppendByte(static_cast<char>((v >> 16) & 0xFF));
	out.AppendByte(static_cast<char>((v >> 24) & 0xFF));
}

void PutU64(wxMemoryBuffer& out, std::uint64_t v)
{
	PutU32(out, static_cast<std::uint32_t>(v & 0xFFFFFFFFu));
	PutU32(out, static_cast<std::uint32_t>(v >> 32));
}

struct ibTypeDescReader {
	const unsigned char* m_data = nullptr;
	size_t               m_size = 0;
	size_t               m_pos  = 0;

	bool Take(std::uint32_t& value) {
		if (m_pos + 4 > m_size) return false;
		value = static_cast<std::uint32_t>(m_data[m_pos])
		      | (static_cast<std::uint32_t>(m_data[m_pos + 1]) <<  8)
		      | (static_cast<std::uint32_t>(m_data[m_pos + 2]) << 16)
		      | (static_cast<std::uint32_t>(m_data[m_pos + 3]) << 24);
		m_pos += 4;
		return true;
	}

	bool Take(std::uint64_t& value) {
		std::uint32_t low = 0, high = 0;
		if (!Take(low) || !Take(high)) return false;
		value = (static_cast<std::uint64_t>(high) << 32) | low;
		return true;
	}
};

} // namespace

void ibTypeDescriptionMemory::WriteBuffer(wxMemoryBuffer& out, const ibTypeDescription& typeDesc)
{
	out.SetDataLen(0);
	out.AppendByte(static_cast<char>(kTypeDescBufferVersion));

	const std::vector<ibClassID>& list = typeDesc.GetClsidList();
	PutU32(out, static_cast<std::uint32_t>(list.size()));
	for (const ibClassID& clsid : list)
		PutU64(out, static_cast<std::uint64_t>(clsid));

	// The three qualifiers, in declaration order. Fixed width each, so a reader that knows this
	// version can skip forward without parsing what it does not need.
	PutU32(out, static_cast<std::uint32_t>(typeDesc.m_typeData.m_number.m_precision));
	PutU32(out, static_cast<std::uint32_t>(typeDesc.m_typeData.m_number.m_scale));
	PutU32(out, typeDesc.m_typeData.m_number.m_nonNegative ? 1u : 0u);
	PutU32(out, static_cast<std::uint32_t>(typeDesc.m_typeData.m_date.m_dateTime));
	PutU32(out, static_cast<std::uint32_t>(typeDesc.m_typeData.m_string.m_length));
	PutU32(out, static_cast<std::uint32_t>(typeDesc.m_typeData.m_string.m_allowedLength));
}

bool ibTypeDescriptionMemory::ReadBuffer(const void* data, size_t length, ibTypeDescription& typeDesc)
{
	// NO BYTES IS NO OPINION — refuse without touching the caller's value. An empty cell (a row
	// written before the requisite existed, a NULL column) must not silently replace a description
	// already in hand with one that admits nothing; what "empty" MEANS belongs to the caller.
	if (data == nullptr || length == 0)
		return false;

	ibTypeDescReader reader{ static_cast<const unsigned char*>(data), length, 0 };
	if (reader.m_size < 1)
		return false;

	const unsigned char version = reader.m_data[reader.m_pos++];
	if (version == 0 || version > kTypeDescBufferVersion)
		return false;   // written by something newer — refuse rather than misread

	std::uint32_t count = 0;
	if (!reader.Take(count))
		return false;

	std::vector<ibClassID> list;
	list.reserve(count);
	for (std::uint32_t idx = 0; idx < count; idx++) {
		std::uint64_t clsid = 0;
		if (!reader.Take(clsid))
			return false;
		list.push_back(static_cast<ibClassID>(clsid));
	}

	std::uint32_t precision = 0, scale = 0, nonNegative = 0, dateFractions = 0, strLength = 0, allowedLength = 0;
	if (!reader.Take(precision) || !reader.Take(scale) || !reader.Take(nonNegative) ||
		!reader.Take(dateFractions) || !reader.Take(strLength) || !reader.Take(allowedLength))
		return false;

	ibTypeDescription::ibTypeData typeData;
	typeData.m_number = ibQualifierNumber(static_cast<unsigned char>(precision),
		static_cast<char>(scale), nonNegative != 0);
	typeData.m_date = ibQualifierDate(static_cast<ibDateFractions>(dateFractions));
	typeData.m_string = ibQualifierString(static_cast<unsigned short>(strLength),
		static_cast<ibAllowedLength>(allowedLength));

	typeDesc.SetDefaultMetaType(list);
	typeDesc.SetTypeData(typeData);
	return true;
}
