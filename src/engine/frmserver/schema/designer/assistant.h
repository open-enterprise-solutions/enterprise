#ifndef __SCHEMA_ASSISTANT_H__
#define __SCHEMA_ASSISTANT_H__

// THE ASSISTANT WINDOW — the designer's Administration → Assistant window (the desktop's OnMcpAssistant), as the server
// holds it: a tab its command opens (ibAssistantTranscriptDocument), the exchange drawn by the tab's own view. The
// designer's own: the runtime has no assistant.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaAssistant : public ibClientSchema {
public:

	// ⭐ THE WINDOW IS ONLY A WINDOW ONTO A RUNNING SERVER — the base's assistant access listening, what the desktop's
	// menu item stood on (OnUpdateMcpAssistant). With nothing listening what a person typed would sit in a queue no
	// client can collect.
	virtual bool AccessRight(const ibSession& session) const override;

	// Nothing — the window is a tab, opened by Open.
	virtual void Build(const ibSession& WXUNUSED(session), const ibDataNode& WXUNUSED(args),
		ibDataNode& WXUNUSED(result)) const override {}

	// Open — the window's tab; ONE window, and the command means "show me it": an open one is raised.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
