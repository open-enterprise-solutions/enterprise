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

	const ibDataValue typesVal = root->GetProperty(wxT("Types"));
	if (typesVal.Kind() == ibDataKind::Array) {
		for (const ibDataValue& item : typesVal.AsArray()) {
			const std::shared_ptr<ibDataNode>& entry = item.AsChild();
			if (entry == nullptr)
				continue;

			// Prefer the portable NAME (resolved to THIS config's live clsid); the raw
			// TypeId is the same-config fallback when there is no metaData / no such type.
			ibClassID clsid = 0;
			if (metaData != nullptr) {
				const wxString typeName = entry->GetValue<wxString>(wxT("TypeName"));
				if (!typeName.IsEmpty()) {
					if (ibCtorMetaValueType* ctor = metaData->GetTypeCtor(typeName))
						clsid = ctor->GetClassType();
				}
			}
			if (clsid == 0) {
				if (const ibDataValue* tid = entry->FindField(wxT("TypeId")))
					clsid = (ibClassID)tid->AsInt();
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
		entry->AddField(wxT("TypeId"), ibDataValue::Int((s64)clsid));
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
