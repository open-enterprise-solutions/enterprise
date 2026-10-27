#ifndef __DEBUGGING_H__
#define __DEBUGGING_H__

// DEBUGGING IN THE CURRENT SESSION — the check box of the enterprise's Options (enterprise/win/dlg/enterpriseOption), as
// the server holds it: whether the process listens for the designer, and whether the person may switch it — the
// dialog's SetValue and Enable — and the switch itself, as the box's handler started and shut the debug server. The
// runtime's own: it is the runtime that is debugged.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaDebugging : public ibClientSchema {
public:

	// Debugging — on; Allowed — it may be switched now.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Start and Stop — only where it may be switched; anything else is refused.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const override;
};

#endif
