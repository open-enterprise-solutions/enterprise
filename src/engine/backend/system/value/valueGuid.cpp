////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value guid
////////////////////////////////////////////////////////////////////////////

#include "valueGuid.h"


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
		const ibGuid& newGuid = paParams[0]->GetString();
		if (newGuid.isValid())
			m_guid = newGuid;
		return newGuid.isValid();
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
