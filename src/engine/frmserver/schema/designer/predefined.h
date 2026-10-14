#ifndef __SCHEMA_PREDEFINED_H__
#define __SCHEMA_PREDEFINED_H__

// THE PREDEFINED VALUES EDITOR — the desktop's dialog over an object of the designer's tree (designer/win/dlg/
// predefinedEditor), as the server holds it: what the object declares, and its three edits, each the engine's own
// (ibValueMetaObjectRecordDataHierarchyMutableRef). An item is written as the engine stores it (WriteData), and a kind
// of object whose items say more (a chart's) adds its own beside them; the client tells the kinds apart by Type. The
// designer's own.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaPredefined : public ibClientSchema {
public:

	// The right to edit the configuration — the tree's (ibSchemaMetaData).
	virtual bool AccessRight(const ibSession& session) const override;

	// Of the object Id names (in Tab's tree): Type, its class, and Items — what it declares. Nothing, when it is no such
	// object or one that declares none.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Create {Id, Name, Code, Description, IsFolder, Parent} — one more item; Set {Id, Guid, …} — the item of that Guid
	// given these, or one more of that Guid (the engine's SetPredefinedValue); Remove {Id, Guid}. Refused, nothing done:
	// no such object or item, a name it has already, a parent that holds nothing.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
