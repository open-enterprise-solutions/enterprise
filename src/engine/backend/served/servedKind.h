#ifndef __SERVED_KIND_H__
#define __SERVED_KIND_H__

// HOW ONE KIND OF WEB ADDRESS ANSWERS — the engine's half of an entry (core/net/servedAddress.h): an HTTP service of
// the configuration, an assistant's channel. The web server knows no kind: it hands what comes to an entry's path to
// the base (protocol `serve`), the base's list (servedAddressRegistry.h) finds the entry, and its kind answers.

#include "backend/backend.h"
#include "core/net/httpServer.h"      // ibHttpRequest, ibHttpResponse — what came, and what goes back
#include "core/net/servedAddress.h"   // ibServedAddress — the entry

#include <memory>
#include <vector>

// One instance per kind for the whole process, registered by its name; it holds nothing of a base. Every call is made
// on a thread that works for the entry's base (ibApplicationInstance::Get).
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

#endif // !__SERVED_KIND_H__
