#include "valueType.h"

#include <algorithm>

#include "core/serialize/dataBuilder.h"   // the empty value of a server's type, written as the engine writes one
#include "frmclient/backend/metaCollection/partial/reference/reference.h"   // which metaobject a type points at
#include "frmclient/backend/system/value/composition/valueComposerField.h"   // a declared value, the reference it names
#include "protocol/protocol.h"                 // …read as it was written (Presentation)

namespace {

// The empty value of one class — a primitive's own, a reference's as the server wrote it and reads it (References: in the
// designer `CatalogRef.Goods.EmptyRef`), any other type of the server's as the engine writes an empty one ({t}).
ibValue EmptyOf(const ibClassID& clsid)
{
	switch (ibValue::GetVTByID(clsid)) {
	case ibValueTypes::TYPE_EMPTY:   return ibValue();
	case ibValueTypes::TYPE_BOOLEAN: return ibValue(false);
	case ibValueTypes::TYPE_NUMBER:  return ibValue(ibNumber());
	case ibValueTypes::TYPE_DATE:    return ibValue(ibDateTime());
	case ibValueTypes::TYPE_STRING:  return ibValue(ibString());
	default:
		break;
	}
	const ibValue empty = ibValueReferenceDataObject::EmptyRef(clsid);
	if (empty.GetClassType() == clsid)
		return empty;
	ibDataNode written;
	written.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"), (unsigned long long)clsid));
	return ibValue::FromNode(written);
}

} // namespace

ibValue ibValueTypeDescription::AdjustValue(const ibTypeDescription& typeDescription, const ibMetaData* WXUNUSED(metaData))
{
	if (!typeDescription.IsOk() || typeDescription.GetClsidCount() != 1)
		return ibValue();
	return EmptyOf(typeDescription.GetFirstClsid());
}

ibValue ibValueTypeDescription::AdjustValue(const ibTypeDescription& typeDescription, const ibValue& varValue,
	const ibMetaData* metaData)
{
	if (!typeDescription.IsOk())
		return varValue;

	const ibTypeDescription::ibTypeData& data = typeDescription.GetTypeData();

	// OF THE TYPE ALREADY — shaped by its qualifiers, the engine's: a number to its scale, a date to its day, a string to
	// its length (0 is "no limit", not "empty").
	const std::vector<ibClassID>& listed = typeDescription.GetClsidList();
	if (std::find(listed.begin(), listed.end(), varValue.GetClassType()) != listed.end()) {
		switch (ibValue::GetVTByID(varValue.GetClassType())) {
		case ibValueTypes::TYPE_NUMBER:
			if (data.GetPrecision() == 0)
				return varValue;
			return ibValue(varValue.GetNumber().Round(data.GetScale()));
		case ibValueTypes::TYPE_DATE:
			if (data.GetDateFraction() == ibDateFractions::ibDateFractions_Date)
				return ibValue(varValue.GetDate().GetDayStart());
			return varValue;
		case ibValueTypes::TYPE_STRING:
			if (data.GetLength() == 0)
				return varValue;
			return ibValue(ibString(varValue.GetString()).Left(data.GetLength()));
		default:
			return varValue;
		}
	}

	// ⭐ A DECLARED VALUE IS THE REFERENCE IT NAMES — the designer's choice in its window of declared values
	// (`CatalogRef.Goods.EmptyRef`, a predefined element), for the type that points at its metaobject and at nothing else
	// (the server's answer, References). Its metaobject and guid are the pair a reference packs itself with: the same node
	// under the type's header, read as it was written. Made the type's empty value below, the element chosen was lost and
	// the cell read as nothing — the client has no name of its own for a reference (2026-10-29).
	ibValueCompositionPredefined* declared = nullptr;
	if (varValue.ConvertToValue(declared)) {
		for (const ibClassID& type : listed) {
			if (ibValueReferenceDataObject::ConvertToMetaIds({ type }, metaData) != std::vector<ibMetaID>{ declared->GetMetaId() })
				continue;
			ibDataNode named;
			declared->Serialize(named);
			named.SetField(kValueFieldClsid, ibDataValue::String(ibString::Format(ibT("%llu"), (unsigned long long)type)));
			named.SetValue(ibProtocol::ibRequest::ibListSettings::Presentation, declared->GetString());
			return ibValue::FromNode(named);
		}
	}

	// ⭐ NOT NAMED IS NOT REFUSED — a family admits its members; the empty value is not put to the gates.
	const ibClassID clsid = varValue.GetClassType();
	if (clsid != g_valueUndefinedCLSID && AllowValue(typeDescription, clsid, metaData))
		return varValue;

	// …READ AS THE TYPE'S ONE CLASS — what a person typed, as a number, a date, a flag or a text.
	if (typeDescription.GetClsidCount() == 1) {
		switch (ibValue::GetVTByID(typeDescription.GetFirstClsid())) {
		case ibValueTypes::TYPE_NUMBER:
			if (data.GetPrecision() == 0)
				return ibValue(varValue.GetNumber());
			return ibValue(varValue.GetNumber().Round(data.GetScale()));
		case ibValueTypes::TYPE_DATE:
			if (data.GetDateFraction() == ibDateFractions::ibDateFractions_Date)
				return ibValue(varValue.GetDate().GetDayStart());
			return ibValue(varValue.GetDate());
		case ibValueTypes::TYPE_STRING:
			if (data.GetLength() == 0)
				return ibValue(ibString(varValue.GetString()));
			return ibValue(ibString(varValue.GetString()).Left(data.GetLength()));
		case ibValueTypes::TYPE_BOOLEAN:
			return ibValue(varValue.GetBoolean());
		default:
			return EmptyOf(typeDescription.GetFirstClsid());
		}
	}

	return ibValue();
}

bool ibValueTypeDescription::AllowValue(const ibTypeDescription& typeDescription, const ibClassID& clsid,
	const ibMetaData* WXUNUSED(metaData))
{
	const std::vector<ibClassID>& allowed = typeDescription.GetClsidList();
	return std::any_of(allowed.begin(), allowed.end(), [clsid](const ibClassID& type) { return clsid_admits(type, clsid); });
}
