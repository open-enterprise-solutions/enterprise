////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value guid
////////////////////////////////////////////////////////////////////////////

#include "valueGuid.h"

namespace {

// 32 hex digits, hyphens ignored — the same alphabet the parser accepts.
// The nil GUID is well formed and still fails isValid(), which means "not the
// empty identity", so a script cannot ask isValid() whether the text parsed.
bool IsWellFormedGuid(const wxString& text)
{
	unsigned hex = 0;
	for (const wxUniChar ch : text) {
		if (ch == wxT('-'))
			continue;
		const unsigned c = ch.GetValue();
		const bool digit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!digit)
			return false;
		++hex;
	}
	return hex == 32;
}

} // namespace

ibValueGuid::ibValueGuid() : ibValue(ibValueTypes::TYPE_VALUE, true), m_guid() {}

ibValueGuid::ibValueGuid(const ibGuid& guid) : ibValue(ibValueTypes::TYPE_VALUE, true), m_guid(guid) {}

bool ibValueGuid::Init()
{
	m_guid = wxNewUniqueGuid;
	return true;
}

bool ibValueGuid::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		const wxString text = paParams[0]->GetString();
		// The nil GUID parses to all zeros, and isValid() answers "this is not
		// the empty identity". New Guid("00000000-0000-0000-0000-000000000000")
		// is that identity. Text that is not a GUID is still refused.
		if (!IsWellFormedGuid(text))
			return false;
		m_guid = ibGuid(text);
		return true;
	}
	return false;
}

ibGuid GuidOf(const ibValue& value)
{
	if (const ibValueGuid* const guidValue = dynamic_cast<const ibValueGuid*>(value.GetRef()))
		return (ibGuid)*guidValue;

	// The text road — for a value that really does carry a guid's spelling.
	const wxString text = value.GetString();
	const ibGuid parsed(text);

	// ⚠ AN UNREADABLE IDENTITY SAYS SO. Empty is a legitimate answer (an unset key, an empty
	// reference), but TEXT THAT IS NOT A GUID is not: it means something upstream handed identity
	// over in a form it does not hold, and the invalid guid it becomes travels on to be looked up,
	// found nowhere, and reported as a missing object far from here — which is exactly the distance
	// this assert removes.
	wxASSERT_MSG(parsed.isValid() || text.IsEmpty(), wxT("GuidOf: the value carries no guid"));

	return parsed;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueGuid, "Guid", value_to_clsid("VL_GUID"));
