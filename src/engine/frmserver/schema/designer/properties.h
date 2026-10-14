#ifndef __SCHEMA_PROPERTIES_H__
#define __SCHEMA_PROPERTIES_H__

// THE DESIGNER'S INSPECTOR — a metaobject's properties, read and edited by its id in a tab's tree. The client holds the
// object it shows (frmclient ibValueMetaObject) and asks it of the server: the rows, in one batch, when a person selects
// it and after each edit; an edit is given back through the object's door (ibPropertyGate). The designer's own.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaProperties : public ibClientSchema {
public:

	// The right to edit the configuration — what the designer's frame lets a person in by.
	virtual bool AccessRight(const ibSession& session) const override;

	// `Args: {Id, Tab}` — the metaobject of that id in the tab's tree (Tab: an external file's; none: the
	// configuration's), its Properties as the inspector writes them (ibPropertyInspector): what its state hides not sent.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Save {Id, Tab, Name, Value} — that metaobject's property or event of that name given that value, as its node holds
	// it. Refused, nothing done: no such object, no such property, no value.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
