#ifndef __SCHEMA_WEB_ADDRESS_H__
#define __SCHEMA_WEB_ADDRESS_H__

// THE BASE'S WEB ADDRESSES — the designer's Administration → Web addresses (frmclient/win/dlgs/webAddress.h): what the
// base answers on every web server that publishes it, beside its clients (backend/served/servedAddressRegistry.h). The
// list, what each kind offers to register, and the two things a person does there — register an entry, take one off —
// both through the registry's one door, refused in its words.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaWebAddress : public ibClientSchema {
public:

	// The configuration's right to administer the data — the base's addresses are the administrator's.
	virtual bool AccessRight(const ibSession& session) const override;

	// Addresses {Id, Kind, Path, Object, Title, Kept}, Offers {Kind, Path, Object, Title} — every kind's (ibServedKind::
	// Offer) not registered yet — and Kinds {Name}.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Create {Kind, Path[, Object]} — registered, kept in the base; Remove {Id} — taken off. Refused, the registry's
	// words; done in the asking.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const override;
};

#endif
