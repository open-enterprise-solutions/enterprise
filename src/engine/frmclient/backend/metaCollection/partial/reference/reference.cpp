#include "reference.h"

#include <algorithm>

#include "protocol/protocol.h"

std::map<ibClassID, std::vector<ibMetaID>>& ibValueReferenceDataObject::Targets()
{
	static std::map<ibClassID, std::vector<ibMetaID>> s_targets;
	return s_targets;
}

std::vector<ibMetaID> ibValueReferenceDataObject::ConvertToMetaIds(const std::vector<ibClassID>& clsids,
	const ibMetaData* WXUNUSED(metaData))
{
	std::vector<ibMetaID> ids;
	for (const ibClassID& clsid : clsids) {
		const auto found = Targets().find(clsid);
		if (found == Targets().end())
			continue;
		for (const ibMetaID& id : found->second)
			if (std::find(ids.begin(), ids.end(), id) == ids.end())
				ids.push_back(id);
	}
	return ids;
}

void ibValueReferenceDataObject::ReadReferences(const ibProtocolNode& references)
{
	for (const ibProtocolNode& reference : references.Children()) {
		unsigned long long clsid = 0;
		if (!reference.GetString(ibProtocolName::Type).ToULongLong(&clsid) || clsid == 0)
			continue;
		std::vector<ibMetaID> ids;
		for (const ibProtocolNode& target : reference.GetList(ibProtocolName::Targets))
			ids.push_back(static_cast<ibMetaID>(target.AsInt()));
		Targets()[static_cast<ibClassID>(clsid)] = std::move(ids);
	}
}
