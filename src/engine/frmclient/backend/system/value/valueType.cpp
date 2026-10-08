#include "valueType.h"

#include <algorithm>

#include "core/serialize/dataBuilder.h"   // the empty value of a server's type, written as the engine writes one

namespace {

// The empty value of one class — a primitive's own, a server's type as the engine writes an empty one ({t}).
ibValue EmptyOf(const ibClassID& clsid)
{
	switch (ibValue::GetVTByID(clsid)) {
	case ibValueTypes::TYPE_EMPTY:   return ibValue();
	case ibValueTypes::TYPE_BOOLEAN: return ibValue(false);
	case ibValueTypes::TYPE_NUMBER:  return ibValue(ibNumber());
	case ibValueTypes::TYPE_DATE:    return ibValue(ibDateTime());
	case ibValueTypes::TYPE_STRING:  return ibValue(wxString());
	default:
		break;
	}
	ibDataNode written;
	written.SetValue(kValueFieldClsid, wxString::Format(wxT("%llu"), (unsigned long long)clsid));
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
			return ibValue(wxString(varValue.GetString()).Left(data.GetLength()));
		default:
			return varValue;
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
				return ibValue(wxString(varValue.GetString()));
			return ibValue(wxString(varValue.GetString()).Left(data.GetLength()));
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
