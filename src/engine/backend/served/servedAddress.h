#ifndef __SERVED_ADDRESS_H__
#define __SERVED_ADDRESS_H__

// THE ADDRESSES A BASE ANSWERS ON A WEB SERVER — one entry, and how one kind of entry answers.
//
// A web server publishes a base under a name (/<publication>/, ibWebPublication) and carries its clients there. What
// else the base answers under that name is the base's own list (ibServedAddressRegistry): an HTTP service of the
// configuration, an assistant's channel. The web server knows no kind: it mounts each entry's path and hands what comes
// there to the base (protocol `serve`), and the base hands it to the entry's kind.
//
//   ibServedAddress   — one entry: which kind answers, where, for which object, whose
//   ibServedKind      — how a kind answers, registered by its name (SERVED_KIND_REGISTER)

#include "backend/backend.h"
#include "core/guid.h"
#include "core/net/httpServer.h"   // ibHttpRequest, ibHttpResponse — what came, and what goes back

#include <memory>
#include <vector>

// ONE ENTRY. Its path is relative to a publication: es/desk is answered at /<publication>/es/desk/ on every web server
// that publishes the base.
struct BACKEND_API ibServedAddress {
	ibGuid   m_id;       // the entry's identity, minted when it is registered
	ibString m_kind;     // the kind's registered name (ibServedKind): which code answers
	ibString m_path;     // segments without a slash at either end: es/desk
	ibGuid   m_object;   // the configuration's object it answers for, when the kind is declared there; empty — none
	ibGuid   m_owner;    // the session that registered it — the entry goes with it; empty — the administrator's, kept

	// The administrator's entries are kept in the base; a session's live as long as it does.
	bool IsKept() const { return !m_owner.isValid(); }
};

// HOW ONE KIND OF ENTRY ANSWERS. One instance per kind for the whole process, registered by its name; it holds
// nothing of a base. Every call is made on a thread that works for the entry's base (ibApplicationInstance::Get).
class BACKEND_API ibServedKind {
public:
	virtual ~ibServedKind() = default;

	// THE REQUEST, ANSWERED — `prefix`: the address up to the entry's path and the path itself (/trade/es/desk), what
	// the request's own path continues. What goes wrong is a status, as a web server says it; it never throws.
	virtual ibHttpResponse Answer(const ibServedAddress& address, const ibHttpRequest& request, const ibString& prefix) const = 0;

	// An entry this kind cannot answer, refused before it is registered: throws ibBackendCoreException naming why (an
	// object that is not in the configuration). The default takes every entry.
	virtual void Check(const ibServedAddress& address) const;

	// THE ENTRIES THIS KIND OFFERS TO REGISTER — what the configuration declares for it (an HTTP service: its RootURL
	// under es/), each with its path and its object and no id. The administration's form lists them. The default:
	// none, the kind is registered by a session.
	virtual std::vector<ibServedAddress> Offer() const;

	// THE KINDS — by their registered name; null for a name nobody registered.
	static const ibServedKind* Find(const ibString& name);
	// …the names, in their order of name.
	static std::vector<ibString> GetKindNames();

	// The registration itself — SERVED_KIND_REGISTER's. A second kind of one name is refused: the first stays.
	static bool Register(const ibString& name, std::unique_ptr<ibServedKind> kind);
};

// A KIND REGISTERED by its class and its name, in a translation unit of the library that answers it.
#define SERVED_KIND_REGISTER(cls, name) \
	static const bool s_servedKind_##cls = ibServedKind::Register(ibT(name), std::make_unique<cls>())

#endif // !__SERVED_ADDRESS_H__
