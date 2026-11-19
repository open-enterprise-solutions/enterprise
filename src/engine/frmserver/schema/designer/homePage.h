#ifndef __SCHEMA_HOME_PAGE_H__
#define __SCHEMA_HOME_PAGE_H__

// THE HOME PAGE WORKSPACE EDITOR — the desktop's dialog over the application's root (designer/win/dlg/homePageEditor),
// as the server holds it: the description the root keeps (ibValueMetaObjectApplication::GetHomePage), the forms it may
// show — what the dialog asked of the application itself, sent with it — and the one edit, the application modified by
// it. The designer's own.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaHomePage : public ibClientSchema {
public:

	// The right to edit the application — the tree's (ibSchemaMetaData).
	virtual bool AccessRight(const ibSession& session) const override;

	// Of the root Id names: HomePage — as the root writes it (ibHomePageDescription::WriteNode); ReadOnly, when the
	// application takes no edit; Forms — the forms the application allows (Id, Name, FullName, Picture, Common, and
	// a form's owner: Owner, OwnerName, OwnerPicture). Nothing, when it is no such root.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Save {Id, HomePage} — the root given that description. Refused, nothing done: no such root, no description.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const override;
};

#endif
