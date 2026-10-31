#include "sessionHolder.h"
#include "session.h"
#include "core/diagnostics/journal.h"

ibSessionWatch::ibSessionWatch(const ibSessionHolder& holder)
{
	if (ibSession* s = holder.Get())
		m_session = s->shared_from_this();
}

ibSessionWatch::ibSessionWatch(ibSession* session)
{
	if (session != nullptr)
		m_session = session->shared_from_this();
}

void ibSessionHolder::Reset()
{
	if (m_session == nullptr)
		return;

	// Take the reference out of the member FIRST. Close() runs listeners
	// and (on the GUI path) marshals work onto other threads; any of them
	// may reach back to the owner and reset it again. A holder whose
	// member is already empty makes that re-entry a no-op instead of a
	// second teardown.
	const std::shared_ptr<ibSession> session = std::move(m_session);
	m_session.reset();

	// Straight to the teardown — NOT through Close(). Close means "ask
	// the owner to go", and the owner is already gone: it is releasing us
	// right now. Asking it again would be a wasted round trip at best and
	// a re-entry into a dying window at worst.
	//
	// The teardown submits Remove@Urgent, and the registry keeps its own
	// strong reference to the session until that request is processed —
	// so the session outlives our reference drop by exactly as long as
	// the teardown needs.
	//
	// ⚠ THIS IS A DESTRUCTOR'S ROAD, and a throw past here is std::terminate. With the process at its memory limit a
	// teardown does throw. What it left undone stays for the stale sweep, as a killed session's does.
	try {
		session->Teardown();
	}
	catch (...) {
		try {
			ibTechJournal::Print(ibJournalMark::Warning, ibT("session"), ibT("a session's teardown ended with an exception"));
		}
		catch (...) {
		}
	}
}
