#include "clientSchema.h"

#include "backend/backend_exception.h"   // ibBackendException::DrainLastErrors — the reasons the base gave

std::function<void()> ibClientSchema::Command(const ibSession& WXUNUSED(session),
	s32 WXUNUSED(command), const ibDataNode& WXUNUSED(args), ibProtocolRefusal& refusal, wxString& error) const
{
	// A schema that only shows something takes no commands.
	refusal = ibProtocolRefusal::NotFound;
	error = wxT("this schema takes no commands");
	return nullptr;
}

wxString ibClientSchema::BackendErrorChain(const wxString& detail)
{
	wxString body = detail;
	const auto chain = ibBackendException::DrainLastErrors();
	wxString previous;
	size_t repeats = 0;
	const auto flush = [&body, &previous, &repeats]() {
		if (previous.IsEmpty())
			return;
		if (!body.IsEmpty())
			body += wxT("\n");
		body += previous;
		if (repeats > 1)
			body += wxString::Format(wxT("  (x%u)"), (unsigned int)repeats);
		previous.clear();
		repeats = 0;
	};
	const size_t kMaxShown = 20;
	size_t shown = 0, dropped = 0;
	for (size_t i = 0; i < chain.size(); ++i) {
		if (chain[i] == detail)
			continue;
		if (chain[i] == previous) { ++repeats; continue; }
		if (shown >= kMaxShown) { ++dropped; continue; }
		flush();
		previous = chain[i];
		repeats = 1;
		++shown;
	}
	flush();
	if (dropped > 0)
		body += wxString::Format(wxT("\n... and %u more"), (unsigned int)dropped);
	return body;
}
