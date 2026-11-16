#ifndef __CORE_NET_SERVED_ADDRESS_H__
#define __CORE_NET_SERVED_ADDRESS_H__

// AN ADDRESS A BASE ANSWERS ON A WEB SERVER — one entry, the rule its path keeps, and how the base's list of them is
// kept as a node.
//
// A web server publishes a base under a name (/<publication>/) and carries its clients there. What else the base
// answers under that name is the base's own list: an HTTP service of the configuration, an assistant's channel. The
// web server knows no kind: it mounts each entry's path and hands what comes there to the base (protocol `serve`),
// and the base hands it to the entry's kind. Who answers a kind, and the list itself, are the engine's
// (backend/served/servedKind.h, servedAddressRegistry.h); what an entry IS, here.

#include <cstdint>
#include <vector>

#include "core/core.h"
#include "core/fstring.h"
#include "core/guid.h"

class ibDataNode;

// ONE ENTRY. Its path is relative to a publication: es/desk is answered at /<publication>/es/desk on every web server
// that publishes the base.
struct CORE_API ibServedAddress {
	ibGuid   m_id;       // the entry's identity, minted when it is registered
	ibString m_kind;     // the kind's registered name: which code answers
	ibString m_path;     // segments without a slash at either end: es/desk
	ibGuid   m_object;   // the configuration's object it answers for, when the kind is declared there; empty — none
	ibGuid   m_owner;    // the session that registered it — the entry goes with it; empty — the administrator's, kept

	// The administrator's entries are kept in the base; a session's live as long as it does.
	bool IsKept() const { return !m_owner.isValid(); }

	// A PATH IS SEGMENTS — letters, digits and the unreserved . _ ~ -, none empty, no slash at either end; the first
	// is not `client`, the web server's own door of the publication. False with why in `why`. What a declared kind
	// is addressed by (an HTTP service's root) is held to it as well: the address it makes is the one registered.
	static bool IsPath(const ibString& path, ibString& why);
};

// THE BASE'S KEPT ENTRIES AS A NODE — {Revision, Addresses: [{Id, Kind, Path, Object}]}: what a shared row of the
// settings holds. The owner is never written: a session's entry is never kept.
class CORE_API ibServedAddressMemory {
public:
	static bool ReadNode(const ibDataNode& node, std::vector<ibServedAddress>& addresses, std::int32_t& revision);
	static bool WriteNode(ibDataNode& node, const std::vector<ibServedAddress>& addresses, std::int32_t revision);
};

#endif // !__CORE_NET_SERVED_ADDRESS_H__
