////////////////////////////////////////////////////////////////////////////
//	Description : the base's list of addresses on a web server, and the kinds that answer them
////////////////////////////////////////////////////////////////////////////

#include "servedAddressRegistry.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/lock/lockHolder.h"
#include "backend/lock/lockManager.h"
#include "backend/settings/settingsStorage.h"

#include "core/serialize/dataBuilder.h"

#include <algorithm>

//*******************************************************************************************
//*                                        The kinds                                        *
//*******************************************************************************************

namespace {

// By name, for the whole process — a function's own, so a kind registered by a static of another translation unit
// finds it made.
std::map<ibString, std::unique_ptr<ibServedKind>>& Kinds()
{
	static std::map<ibString, std::unique_ptr<ibServedKind>> kinds;
	return kinds;
}

std::mutex& KindsMutex()
{
	static std::mutex mutex;
	return mutex;
}

} // namespace

void ibServedKind::Check(const ibServedAddress&) const
{
}

std::vector<ibServedAddress> ibServedKind::Offer() const
{
	return {};
}

const ibServedKind* ibServedKind::Find(const ibString& name)
{
	std::lock_guard<std::mutex> lock(KindsMutex());
	const auto found = Kinds().find(name);
	return found != Kinds().end() ? found->second.get() : nullptr;
}

std::vector<ibString> ibServedKind::GetKindNames()
{
	std::lock_guard<std::mutex> lock(KindsMutex());
	std::vector<ibString> names;
	for (const auto& kind : Kinds())
		names.push_back(kind.first);
	return names;
}

bool ibServedKind::Register(const ibString& name, std::unique_ptr<ibServedKind> kind)
{
	std::lock_guard<std::mutex> lock(KindsMutex());
	return Kinds().emplace(name, std::move(kind)).second;
}

//*******************************************************************************************
//*                                      The registry                                       *
//*******************************************************************************************

namespace {

// THE ROW — a shared row of sys_settings, no user, one well-known object: the base's addresses.
const ibGuid kAddressesObject(ibT("73657276-6564-6164-6472-657373657300"));

ibSettingsKey AddressesKey()
{
	return ibSettingsKey(ibSettingsCategory::Web, kAddressesObject);
}

// The lock manager's key a write is made under, in every process of the base.
const ibString kAddressesLock = ibT("ServedAddresses");

// THE NODE'S WORDS.
const ibString kRevision  = ibT("Revision");
const ibString kAddresses = ibT("Addresses");
const ibString kId        = ibT("Id");
const ibString kKind      = ibT("Kind");
const ibString kPath      = ibT("Path");
const ibString kObject    = ibT("Object");

// A PATH IS SEGMENTS — letters, digits and the unreserved . _ ~ -, none empty, no slash at either end; the first is not
// `client`, the web server's own door of the publication. False with why in `why`.
bool IsPath(const ibString& path, ibString& why)
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

// Two paths that are one address: the same segments, whatever case — a web server is asked in any.
bool SamePath(const ibString& a, const ibString& b)
{
	return a.CmpNoCase(b) == 0;
}

void SortByPath(std::vector<ibServedAddress>& addresses)
{
	std::sort(addresses.begin(), addresses.end(),
		[](const ibServedAddress& a, const ibServedAddress& b) { return a.m_path.CmpNoCase(b.m_path) < 0; });
}

} // namespace

ibServedAddressRegistry::ibServedAddressRegistry(ib::AppDataCtorToken owner) :
	m_applicationInstance(owner.GetApplicationInstance())
{
}

ibServedAddressRegistry::~ibServedAddressRegistry() = default;

bool ibServedAddressRegistry::ReadKept(std::vector<ibServedAddress>& kept, std::int64_t& revision) const
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(m_applicationInstance);
	ibDataNode row;
	if (storage == nullptr || !storage->Restore(AddressesKey(), row))
		return false;
	revision = row.GetValue<std::int32_t>(kRevision);
	kept.clear();
	if (const auto list = row.FindField(kAddresses)) {
		for (const ibDataValue& item : list->AsArray()) {
			const std::optional<ibDataNode> node = item.AsChild();
			if (!node)
				continue;
			ibServedAddress address;
			address.m_id = node->GetValue<ibGuid>(kId);
			address.m_kind = node->GetValue<ibString>(kKind);
			address.m_path = node->GetValue<ibString>(kPath);
			address.m_object = node->GetValue<ibGuid>(kObject);
			kept.push_back(std::move(address));
		}
	}
	return true;
}

void ibServedAddressRegistry::WriteKept(const std::vector<ibServedAddress>& kept, std::int64_t revision) const
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage(m_applicationInstance);
	if (storage == nullptr)
		ibBackendCoreException::Error(_("the base has no settings storage"));
	ibDataNode row;
	row.SetValue(kRevision, static_cast<std::int32_t>(revision));
	std::vector<ibDataValue> items;
	for (const ibServedAddress& address : kept) {
		auto item = std::make_shared<ibDataNode>();
		item->SetValue(kId, address.m_id);
		item->SetValue(kKind, address.m_kind);
		item->SetValue(kPath, address.m_path);
		if (address.m_object.isValid())
			item->SetValue(kObject, address.m_object);
		items.push_back(ibDataValue::Child(item));
	}
	row.AddField(kAddresses, ibDataValue::Array(std::move(items)));
	if (!storage->Save(AddressesKey(), row))
		ibBackendCoreException::Error(_("the base's web addresses were not written"));
}

ibGuid ibServedAddressRegistry::Register(const ibServedAddress& given)
{
	ibServedAddress address = given;
	if (!address.m_id.isValid())
		address.m_id = ibGuid::newGuid();

	ibString why;
	if (!IsPath(address.m_path, why))
		ibBackendCoreException::Error(_("The web address %s '%s' is not registered: %s."), address.m_kind, address.m_path, why);
	const ibServedKind* const kind = ibServedKind::Find(address.m_kind);
	if (kind == nullptr)
		ibBackendCoreException::Error(_("The web address %s '%s' is not registered: no kind of that name answers."),
			address.m_kind, address.m_path);
	kind->Check(address);

	// The path free, among the entries as they stand — the kept ones read again under the lock, below.
	const auto taken = [&address](const std::vector<ibServedAddress>& entries) {
		for (const ibServedAddress& entry : entries) {
			if (entry.m_id == address.m_id)
				ibBackendCoreException::Error(_("The web address %s '%s' is not registered: its id is registered already."),
					address.m_kind, address.m_path);
			if (SamePath(entry.m_path, address.m_path))
				ibBackendCoreException::Error(_("The web address %s '%s' is not registered: %s answers there already."),
					address.m_kind, address.m_path, entry.m_kind);
		}
	};

	if (!address.IsKept()) {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			taken(m_kept);
			taken(m_owned);
			m_owned.push_back(address);
		}
		Tell();
		return address.m_id;
	}

	// ⭐ A KEPT ENTRY — written under the lock every process of the base takes, the row read again first.
	ibLockManager* const locks = ibApplicationInstance::GetLockManager(m_applicationInstance);
	if (locks == nullptr)
		ibBackendCoreException::Error(_("The web address %s '%s' is not registered: the base is not open."), address.m_kind, address.m_path);
	ibSingleLockHolder holder(ibT("Web addresses"));
	ibLockHandle claim = locks->Acquire({ ibLockItem::ForNamespace(kAddressesLock) }, {}, &holder);

	std::vector<ibServedAddress> kept;
	std::int64_t revision = 0;
	ReadKept(kept, revision);
	taken(kept);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		taken(m_owned);
	}
	kept.push_back(address);
	SortByPath(kept);
	WriteKept(kept, revision + 1);
	TakeKept(std::move(kept), revision + 1);
	return address.m_id;
}

void ibServedAddressRegistry::Unregister(const ibGuid& id)
{
	bool owned = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto found = std::find_if(m_owned.begin(), m_owned.end(), [&id](const ibServedAddress& entry) { return entry.m_id == id; });
		if (found != m_owned.end()) {
			m_owned.erase(found);
			owned = true;
		}
	}
	if (owned) {
		Tell();
		return;
	}

	ibLockManager* const locks = ibApplicationInstance::GetLockManager(m_applicationInstance);
	if (locks == nullptr)
		ibBackendCoreException::Error(_("The web address %s is not unregistered: the base is not open."), id.str());
	ibSingleLockHolder holder(ibT("Web addresses"));
	ibLockHandle claim = locks->Acquire({ ibLockItem::ForNamespace(kAddressesLock) }, {}, &holder);

	std::vector<ibServedAddress> kept;
	std::int64_t revision = 0;
	ReadKept(kept, revision);
	const auto found = std::find_if(kept.begin(), kept.end(), [&id](const ibServedAddress& entry) { return entry.m_id == id; });
	if (found == kept.end())
		ibBackendCoreException::Error(_("The web address %s is not unregistered: no entry has that id."), id.str());
	kept.erase(found);
	WriteKept(kept, revision + 1);
	TakeKept(std::move(kept), revision + 1);
}

std::vector<ibServedAddress> ibServedAddressRegistry::GetServedAddressArray() const
{
	std::vector<ibServedAddress> addresses;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		addresses = m_kept;
		addresses.insert(addresses.end(), m_owned.begin(), m_owned.end());
	}
	SortByPath(addresses);
	return addresses;
}

ibHttpResponse ibServedAddressRegistry::Serve(const ibGuid& id, const ibHttpRequest& request, const ibString& prefix) const
{
	ibServedAddress address;
	bool found = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const std::vector<ibServedAddress>* entries : { &m_kept, &m_owned }) {
			for (const ibServedAddress& entry : *entries) {
				if (entry.m_id == id) {
					address = entry;
					found = true;
				}
			}
		}
	}
	ibHttpResponse response;
	if (!found) {
		response.m_status = 404;
		return response;
	}
	const ibServedKind* const kind = ibServedKind::Find(address.m_kind);
	if (kind == nullptr) {
		response.m_status = 503;
		return response;
	}
	return kind->Answer(address, request, prefix);
}

std::uint64_t ibServedAddressRegistry::Listen(ibListener listener)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const std::uint64_t listening = m_nextListener++;
	m_listeners.emplace(listening, std::move(listener));
	return listening;
}

void ibServedAddressRegistry::Forget(std::uint64_t listening)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_listeners.erase(listening);
}

void ibServedAddressRegistry::Load()
{
	std::vector<ibServedAddress> kept;
	std::int64_t revision = 0;
	try {
		if (!ReadKept(kept, revision))
			return;
	}
	catch (const ibCoreException& err) {
		ibJournalError(ibT("web"), ibT("the base's web addresses could not be read: %s"), err.GetErrorDescription());
		return;
	}
	TakeKept(std::move(kept), revision);
}

void ibServedAddressRegistry::Refresh()
{
	std::vector<ibServedAddress> kept;
	std::int64_t revision = 0;
	if (!ReadKept(kept, revision))
		return;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (revision == m_revision)
			return;
	}
	TakeKept(std::move(kept), revision);
}

void ibServedAddressRegistry::OnSessionEnd(const ibGuid& session)
{
	bool withdrawn = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		withdrawn = std::erase_if(m_owned, [&session](const ibServedAddress& entry) { return entry.m_owner == session; }) > 0;
	}
	if (withdrawn)
		Tell();
}

void ibServedAddressRegistry::TakeKept(std::vector<ibServedAddress> kept, std::int64_t revision)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_kept = std::move(kept);
		m_revision = revision;
	}
	Tell();
}

void ibServedAddressRegistry::Tell()
{
	std::vector<ibListener> listeners;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& listener : m_listeners)
			listeners.push_back(listener.second);
	}
	if (listeners.empty())
		return;
	const std::vector<ibServedAddress> addresses = GetServedAddressArray();
	for (const ibListener& listener : listeners) {
		try {
			listener(addresses);
		}
		catch (const std::exception& err) {
			ibJournalWarning(ibT("web"), ibT("a listener of the web addresses ended with an exception: %s"), ibString::FromUTF8(err.what()));
		}
	}
}
