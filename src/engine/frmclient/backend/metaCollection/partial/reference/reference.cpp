#include "reference.h"

#include <algorithm>

#include "frmclient/backend/compiler/value.h"          // the empty reference, made of the node it came as
#include "frmclient/backend/serialize/dataProtocol.h"  // …read off the wire as it lies
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

std::map<ibClassID, ibDataNode>& ibValueReferenceDataObject::EmptyRefs()
{
	static std::map<ibClassID, ibDataNode> s_empty;
	return s_empty;
}

ibValue ibValueReferenceDataObject::EmptyRef(const ibClassID& clsid)
{
	const auto found = EmptyRefs().find(clsid);
	return found != EmptyRefs().end() ? ibValue::FromNode(found->second) : ibValue();
}

void ibValueReferenceDataObject::ReadReferences(const ibProtocolNode& references)
{
	for (const ibProtocolNode& reference : references.Children()) {
		unsigned long long clsid = 0;
		if (!reference.GetString(ibProtocol::ibRequest::ibListSettings::Type).ToULongLong(&clsid) || clsid == 0)
			continue;
		std::vector<ibMetaID> ids;
		for (const ibProtocolNode& target : reference.GetList(ibProtocol::ibRequest::ibListSettings::Targets))
			ids.push_back(static_cast<ibMetaID>(target.AsInt()));
		Targets()[static_cast<ibClassID>(clsid)] = std::move(ids);
		const ibProtocolNode empty = reference.FindChild(ibProtocol::ibRequest::ibListSettings::Value);
		if (empty.IsNode())
			ibReadProtocolNode(empty, EmptyRefs()[static_cast<ibClassID>(clsid)]);
	}
}
