#ifndef __DEBUGGING_H__
#define __DEBUGGING_H__

// DEBUGGING IN THE CURRENT SESSION — the check box of the application's Options (ibPanelDebugSettings), as the server
// holds it: whether the process listens for the designer, and whether the person may switch it — the box's value and
// whether it is enabled — and the switch itself, as the box's handler starts and shuts the debug server. The runtime's
// own: it is the runtime that is debugged. The Options item of the application's menu asks this schema.

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
