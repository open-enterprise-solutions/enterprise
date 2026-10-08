#include "clientSchema.h"

std::function<void()> ibClientSchema::Command(const ibSession& WXUNUSED(session),
	s32 WXUNUSED(command), const ibDataNode& WXUNUSED(args), ibProtocolRefusal& refusal, wxString& error) const
{
	// A schema that only shows something takes no commands.
	refusal = ibProtocolRefusal::NotFound;
	error = wxT("this schema takes no commands");
	return nullptr;
}
