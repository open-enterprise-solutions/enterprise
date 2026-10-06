#ifndef __ALL_FUNCTIONS_H__
#define __ALL_FUNCTIONS_H__

// ALL FUNCTIONS — the enterprise's dialog of that name (enterprise/win/dlg/functionAll), as the server holds it: the
// tree — the scheme a client shows however it likes — and what is picked there opened by the schema itself, as the
// dialog's double click opened it (ibBackendCommandItem::Execute). The runtime's own: it opens what the runtime runs.

#include "sfrontend/schema/clientSchema.h"

// The commands of All functions — a number on the wire (schema's Command), a type here.
enum class ibSchemaAllFunctionsCommand : s32 {
	Open = 1,   // {Item[, Type]} — the item's object, as the navigation opens it (Type: ibInterfaceCommandType)
};

class SFRONTEND_API ibSchemaAllFunctions : public ibClientSchema {
public:

	// The configuration's right to the mode of all functions — what the desktop's Operations menu stood on.
	virtual bool AccessRight(const ibSession& session) const override;

	// THE TREE — the objects grouped by kind, in the dialog's order, each group with what this person may open:
	// available (no functional option has switched it off — the gate the sections ask) and shown to them
	// (AccessRight_Show — the right the form door asks before it opens anything). Each item by its Item id.
	virtual void Build(const ibSession& session, ibDataNode& tree) const override;

	// Open — only an item the tree offers this person; anything else is refused, whatever its id names.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibClientRefusal& refusal, wxString& error) const override;
};

#endif
