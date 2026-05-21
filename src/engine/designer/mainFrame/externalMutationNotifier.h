/////////////////////////////////////////////////////////////////////////////
// ibExternalMutationNotifier — Designer-side consumer of the change marker
// oes-mcp drops into <config>/sys/.oes-mcp-mutation on every successful
// mutation. Polls via wxTimer every ~2.5s while a configuration is open,
// reads the JSON marker, dedupes by monotonic seq, and surfaces a non-
// blocking status-bar message + a one-shot offer to reload the metadata
// tree when something changes.
//
// Lives in Designer (not frontend) because the spec explicitly forbids
// adding wx GUI deps to backend; backend ships only the file primitives
// (ibConfigLock::ReadMutationMarker). Designer wires the timer to its
// own main frame lifetime.
//
// Behaviour
// ---------
//   * On config open  -> Start(configDir)
//     - reads current seq, remembers it as the baseline (so we don't
//       surface a stale event left over from before this Designer
//       session)
//     - starts wxTimer at 2500ms
//   * Each tick       -> ReadMutationMarker()
//     - if seq > m_lastSeenSeq -> surface a toast, store seq.
//   * On config close -> Stop()
//     - cancels the timer; safe to re-call.
//
// Toast UI
// --------
// Minimal: SetStatusText on the designer's status bar, prefixed with a
// "[external]" tag so the user can tell it's not an internal Designer
// action. We deliberately do NOT pop a modal — that would interrupt
// the user's editing flow. The user can then press F5 (or the
// equivalent "Reload from disk" path) to pick up the changes. A
// dedicated Reload button can be added in a follow-up commit when we
// have a stable "branch reload" API on ibMetaDataConfigurationBase.
//
// Threading
// ---------
// wxTimer fires on the main thread (the wxApp's event loop). All
// pollers / handlers therefore execute on the main thread; no extra
// synchronization required.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_EXTERNAL_MUTATION_NOTIFIER_H_
#define _IB_EXTERNAL_MUTATION_NOTIFIER_H_

#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <cstdint>

class wxFrame;

class ibExternalMutationNotifier : public wxEvtHandler {
public:
	// Construct in the parent frame's ctor (or first activation). The
	// notifier holds a non-owning back-pointer to the frame so it can
	// route status-bar messages.
	explicit ibExternalMutationNotifier(wxFrame* frame);
	virtual ~ibExternalMutationNotifier();

	// Begin watching the given configuration directory. Calling Start
	// again with a different directory transparently stops the previous
	// watch first. Empty path == Stop().
	void Start(const wxString& configDir);

	// Stop polling. Idempotent. Called from the frame's dtor and on
	// config close.
	void Stop();

	// True iff the timer is currently armed.
	bool IsActive() const { return m_timer.IsRunning(); }

	// Override polling interval (default 2500 ms). Mostly for tests.
	void SetPollIntervalMs(int ms) { m_pollIntervalMs = ms; }

private:
	void OnTick(wxTimerEvent& event);

	wxFrame*       m_frame = nullptr;
	wxString       m_configDir;
	wxTimer        m_timer;
	std::int64_t   m_lastSeenSeq = 0;
	int            m_pollIntervalMs = 2500;
};

#endif // _IB_EXTERNAL_MUTATION_NOTIFIER_H_
