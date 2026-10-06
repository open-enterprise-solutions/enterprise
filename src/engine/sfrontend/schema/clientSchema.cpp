#include "clientSchema.h"

std::function<void()> ibClientSchema::Command(const ibSession& WXUNUSED(session),
	s32 WXUNUSED(command), const ibDataNode& WXUNUSED(args), ibClientRefusal& refusal, wxString& error) const
{
	// A schema that only shows something takes no commands.
	refusal = ibClientRefusal::NotFound;
	error = wxT("this schema takes no commands");
	return nullptr;
}
