#ifndef __SCHEMA_PREDEFINED_H__
#define __SCHEMA_PREDEFINED_H__

// THE PREDEFINED VALUES EDITOR — the desktop's dialog over an object of the designer's tree (designer/win/dlg/
// predefinedEditor), as the server holds it: the one schema of what the object declares (ibPredefinedDescription — the
// items as the configuration keeps them and what an item is entered with), and its three edits, each the engine's own
// (ibValueMetaObjectRecordDataHierarchyMutableRef). The designer's own.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaPredefined : public ibClientSchema {
public:

	// The right to edit the configuration — the tree's (ibSchemaMetaData).
	virtual bool AccessRight(const ibSession& session) const override;

	// Of the object Id names (in Tab's tree): Type, its class; Declares — the one schema, items included; and Classes —
	// the types a type is picked from, named. Nothing, when it is no such object or one that declares none.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Create {Id, an item in its node form} — one more item; Set {Id, Guid, …} — the item of that Guid given these, or
	// one more of that Guid (the engine's SetPredefinedValue); Remove {Id, Guid}. Refused, nothing done: no such object
	// or item, a name it has already, what the engine's door refuses.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const override;
};

#endif
