#ifndef __SERVED_ADDRESS_REGISTRY_H__
#define __SERVED_ADDRESS_REGISTRY_H__

// THE BASE'S LIST OF ADDRESSES — what it answers on every web server that publishes it (servedAddress.h). Held by
// ibApplicationInstance as the job manager is, reached through ibApplicationInstance::GetServedAddressRegistry.
//
// ⭐ ONE DOOR. Register / Unregister do their job or throw ibBackendCoreException naming the entry — its kind and its
// path. The administration's form, the MCP tools and a session asking for an address of its own all come through it,
// and none of them decides anything on its own.
//
// ⭐ TWO KINDS OF ENTRY, ONE LIST.
//   - The administrator's (no owner) are KEPT: a shared row of sys_settings with no user, as the base's locale is —
//     ibSettingsKey(ibSettingsCategory::Web, kAddressesObject). Read when the base opens.
//   - A session's (m_owner) are never written: the entry is the session's, answered in its process, and the registry
//     withdraws it when ibSessionRegistry::OnDisconnect says the session is gone.
//
// ⭐ SEVERAL PROCESSES, ONE BASE. Application servers that serve one base each hold this list. A write takes the lock
// manager's key ServedAddresses (sys_lock) and, under it, reads the row again, changes it, raises its Revision and
// writes it back: two servers registering at once keep each other's entry. Every sweep of the session registry
// (ibSessionRegistry::OnSweep, 3 s) reads the row by its key; a Revision that is not the one held is taken, and the
// listeners told. A web server listening to another server learns of the entry within a sweep.

#include "backend/appDataCtorToken.h"
#include "backend/served/servedAddress.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

class ibApplicationInstance;

class BACKEND_API ibServedAddressRegistry {
public:

	// What a listener is told: the whole list, each time it changes.
	using ibListener = std::function<void(const std::vector<ibServedAddress>& addresses)>;

	explicit ibServedAddressRegistry(ib::AppDataCtorToken owner);
	~ibServedAddressRegistry();

	ibServedAddressRegistry(const ibServedAddressRegistry&) = delete;
	ibServedAddressRegistry& operator=(const ibServedAddressRegistry&) = delete;

	// THE DOOR. An entry registered — its id minted when it has none, and returned; kept in the base when it has no
	// owner. Refused, by name: a path that is not one (segments of letters, digits and . _ ~ -, the first not
	// `client`), a path another entry has, a kind nobody registered, an entry its kind does not take (ibServedKind::Check).
	ibGuid Register(const ibServedAddress& address);
	// …and taken off. Refused: an id that names nothing.
	void Unregister(const ibGuid& id);

	// What the base serves now, in the order of path.
	std::vector<ibServedAddress> GetServedAddressArray() const;

	// THE REQUEST TO AN ENTRY, handed to its kind — 404 for an id that names nothing, 503 for a kind no longer there.
	// On a thread that works for this base.
	ibHttpResponse Serve(const ibGuid& id, const ibHttpRequest& request, const ibString& prefix) const;

	// TOLD OF EVERY CHANGE — the whole list; the id is what Forget takes. A listener is called outside the registry's
	// lock, on the thread that changed the list: the one that registered, the registry's sweep, a session's teardown.
	std::uint64_t Listen(ibListener listener);
	void Forget(std::uint64_t listening);

	// ---- The base's moments (ibApplicationInstance) ----

	// The kept entries read — as the base opens. A row that cannot be read is said in the journal and served as none.
	void Load();
	// The row read again, taken when its Revision moved — every sweep of the session registry.
	void Refresh();
	// A session gone: its entries withdrawn.
	void OnSessionEnd(const ibGuid& session);

private:

	// The kept entries as the row holds them, with its Revision; false — no row.
	bool ReadKept(std::vector<ibServedAddress>& kept, std::int64_t& revision) const;
	void WriteKept(const std::vector<ibServedAddress>& kept, std::int64_t revision) const;

	// The kept entries taken from the row, the session's kept as they were; the listeners told when it changed.
	void TakeKept(std::vector<ibServedAddress> kept, std::int64_t revision);
	// The listeners told the list as it is now.
	void Tell();

	ibApplicationInstance* const m_applicationInstance;

	mutable std::mutex                       m_mutex;
	std::vector<ibServedAddress>             m_kept;      // the base's, as last read or written
	std::vector<ibServedAddress>             m_owned;     // the sessions' of this process
	std::int64_t                             m_revision = 0;
	std::map<std::uint64_t, ibListener>      m_listeners;
	std::uint64_t                            m_nextListener = 1;
};

#endif // !__SERVED_ADDRESS_REGISTRY_H__
