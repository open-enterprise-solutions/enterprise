#include "clientSchema.h"

#include "backend/backend_exception.h"   // ibBackendException::DrainLastErrors — the reasons the base gave

std::function<void()> ibClientSchema::Command(const ibSession& /*session*/,
	std::int32_t /*command*/, const ibDataNode& /*args*/, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	// A schema that only shows something takes no commands.
	refusal = ibProtocol::ibRefusal::NotFound;
	error = ibT("this schema takes no commands");
	return nullptr;
}

ibString ibClientSchema::BackendErrorChain(const ibString& detail)
{
	ibString body = detail;
	const auto chain = ibBackendException::DrainLastErrors();
	ibString previous;
	size_t repeats = 0;
	const auto flush = [&body, &previous, &repeats]() {
		if (previous.IsEmpty())
			return;
		if (!body.IsEmpty())
			body += ibT("\n");
		body += previous;
		if (repeats > 1)
			body += ibString::Format(ibT("  (x%u)"), (unsigned int)repeats);
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
		body += ibString::Format(ibT("\n... and %u more"), (unsigned int)dropped);
	return body;
}
