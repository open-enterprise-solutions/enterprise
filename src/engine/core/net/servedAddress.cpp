#include "core/net/servedAddress.h"

#include "core/localization/dictionary.h"   // _() — why a path is not one
#include "core/serialize/dataBuilder.h"     // ibDataNode — the kept list's node

namespace {

// THE NODE'S WORDS.
const ibString kRevision  = ibT("Revision");
const ibString kAddresses = ibT("Addresses");
const ibString kId        = ibT("Id");
const ibString kKind      = ibT("Kind");
const ibString kPath      = ibT("Path");
const ibString kObject    = ibT("Object");

} // namespace

bool ibServedAddress::IsPath(const ibString& path, ibString& why)
{
	if (path.IsEmpty()) {
		why = _("the path is empty");
		return false;
	}
	size_t from = 0;
	bool first = true;
	while (from <= path.length()) {
		size_t slash = path.find(ibT('/'), from);
		if (slash == ibString::npos)
			slash = path.length();
		const ibString segment = path.Mid(from, slash - from);
		if (segment.IsEmpty()) {
			why = _("the path has an empty segment");
			return false;
		}
		for (const ibChar c : segment) {
			const bool letter = (c >= ibT('a') && c <= ibT('z')) || (c >= ibT('A') && c <= ibT('Z')) || (c >= ibT('0') && c <= ibT('9'));
			if (!letter && c != ibT('.') && c != ibT('_') && c != ibT('~') && c != ibT('-')) {
				why = _("a segment holds a character other than a letter, a digit or . _ ~ -");
				return false;
			}
		}
		if (segment == ibT(".") || segment == ibT("..")) {
			why = _("a segment is . or ..");
			return false;
		}
		if (first && segment == ibT("client")) {
			why = _("the first segment is client, the publication's own door");
			return false;
		}
		first = false;
		from = slash + 1;
	}
	return true;
}

bool ibServedAddressMemory::ReadNode(const ibDataNode& node, std::vector<ibServedAddress>& addresses, std::int32_t& revision)
{
	revision = node.GetValue<std::int32_t>(kRevision);
	addresses.clear();
	if (const auto list = node.FindField(kAddresses)) {
		for (const ibDataValue& item : list->AsArray()) {
			const std::optional<ibDataNode> entry = item.AsChild();
			if (!entry)
				continue;
			ibServedAddress address;
			address.m_id = entry->GetValue<ibGuid>(kId);
			address.m_kind = entry->GetValue<ibString>(kKind);
			address.m_path = entry->GetValue<ibString>(kPath);
			address.m_object = entry->GetValue<ibGuid>(kObject);
			addresses.push_back(std::move(address));
		}
	}
	return true;
}

bool ibServedAddressMemory::WriteNode(ibDataNode& node, const std::vector<ibServedAddress>& addresses, std::int32_t revision)
{
	node.SetValue(kRevision, revision);
	std::vector<ibDataValue> items;
	for (const ibServedAddress& address : addresses) {
		auto item = std::make_shared<ibDataNode>();
		item->SetValue(kId, address.m_id);
		item->SetValue(kKind, address.m_kind);
		item->SetValue(kPath, address.m_path);
		if (address.m_object.isValid())
			item->SetValue(kObject, address.m_object);
		items.push_back(ibDataValue::Child(item));
	}
	node.AddField(kAddresses, ibDataValue::Array(std::move(items)));
	return true;
}
