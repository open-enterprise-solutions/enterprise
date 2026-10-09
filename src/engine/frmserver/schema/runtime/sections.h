#ifndef __SECTIONS_H__
#define __SECTIONS_H__

// SECTIONS — the enterprise's section panel (enterprise/mainFrame/mainFrameEnterpriseInterface.cpp, ibSubSystemWindow)
// as the server holds it: the configuration's sections this person may use, each with its page — the blocks of
// commands in the panel's two columns, in its order — and what is picked there opened by the schema itself, as the
// panel's link opened it (ibBackendCommandItem::Execute). The runtime's own: it opens what the runtime runs.

#include "frmserver/schema/clientSchema.h"

// The commands of Sections — a number on the wire (schema's Command), a type here.
enum class ibSchemaSectionsCommand : s32 {
	Open = 1,   // {Item, Type} — an item a page offers, as its link opens it (Type: the item's own, ibInterfaceCommandType)
};

class FRMSERVER_API ibSchemaSections : public ibClientSchema {
public:

	// THE SECTIONS — each by its id (NodeId) with its Title and Picture, and its page: Blocks in the panel's order, each
	// with its Column (1 — the navigation's, on the left; 2 — the actions', on the right), its Title (none for the
	// section's own items at the top), a declared group's Picture and Tooltip, and its items — Item, Title, Icon and
	// Type: what the link opens it as (Create in the Create group, the default anywhere else).
	virtual void Build(const ibSession& session, ibDataNode& result) const override;

	// Open — only an item a page offers this person, as that page offers it; anything else is refused.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
