/////////////////////////////////////////////////////////////////////////////
// ibExternalMutationNotifier — see header.
//
// Architecture note. The backend module ibConfigLock owns the on-disk
// JSON parsing; this file only consumes it. That split mirrors the rest
// of the OES codebase — backend has zero wx GUI deps, designer/frontend
// drives the user-visible side. The seq dedupe means we never repeat a
// toast for the same mutation across ticks; a fresh mutation always wins
// because oes-mcp's WriteMutationMarker is monotonic across writes.
/////////////////////////////////////////////////////////////////////////////

#include "externalMutationNotifier.h"

#include "backend/utils/configLock.hpp"

#include <wx/frame.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/statusbr.h>

namespace {

constexpr int kTimerId = 0xCFFE;  // arbitrary — distinct from designer's other ids

}  // namespace

ibExternalMutationNotifier::ibExternalMutationNotifier(wxFrame* frame)
	: m_frame(frame)
	, m_timer(this, kTimerId)
{
	Bind(wxEVT_TIMER, &ibExternalMutationNotifier::OnTick, this, kTimerId);
}

ibExternalMutationNotifier::~ibExternalMutationNotifier()
{
	Stop();
}

void ibExternalMutationNotifier::Start(const wxString& configDir)
{
	Stop();
	if (configDir.IsEmpty()) return;
	m_configDir = configDir;

	// Baseline: read the current seq so we don't surface a stale marker
	// left over from a prior Designer / MCP session.
	ibConfigLock::MutationMarker m;
	if (ibConfigLock::ReadMutationMarker(m_configDir, m)) {
		m_lastSeenSeq = m.seq;
	} else {
		m_lastSeenSeq = 0;
	}

	if (!m_timer.Start(m_pollIntervalMs, /*oneShot=*/false)) {
		wxLogWarning(wxT("ExternalMutationNotifier: timer failed to start; "
		                 "external MCP mutations will not be surfaced"));
	}
}

void ibExternalMutationNotifier::Stop()
{
	if (m_timer.IsRunning()) m_timer.Stop();
	m_configDir.Clear();
	// Keep m_lastSeenSeq across Stop/Start so a brief close+reopen of the
	// same config doesn't re-surface the most recent event. Reset happens
	// only when the directory actually changes (in Start).
}

void ibExternalMutationNotifier::OnTick(wxTimerEvent& /*event*/)
{
	if (m_configDir.IsEmpty() || m_frame == nullptr) return;

	ibConfigLock::MutationMarker m;
	if (!ibConfigLock::ReadMutationMarker(m_configDir, m)) {
		// No marker yet (oes-mcp hasn't run, or file is missing). Not an
		// error — just no event to surface.
		return;
	}

	if (m.seq <= m_lastSeenSeq) return;  // already surfaced (or same baseline).
	m_lastSeenSeq = m.seq;

	// Status-bar toast. wxString::Format keeps the message localisable via
	// gettext (_()), while still inlining the tool name + affected path.
	const wxString tool     = wxString::FromUTF8(m.tool.c_str());
	const wxString fullName = wxString::FromUTF8(m.fullName.c_str());
	wxString msg;
	if (!fullName.IsEmpty()) {
		msg = wxString::Format(
			_("[external] Configuration changed by %s: %s -> %s. "
			  "Reload tree to see changes."),
			wxString::FromUTF8(m.pluginId.c_str()),
			tool, fullName);
	} else {
		msg = wxString::Format(
			_("[external] Configuration changed by %s: %s. "
			  "Reload tree to see changes."),
			wxString::FromUTF8(m.pluginId.c_str()),
			tool);
	}

	wxStatusBar* sb = m_frame->GetStatusBar();
	if (sb != nullptr) {
		// Field 0 — primary status text. Other Designer status fields
		// (debugger state, cursor position) live in higher indexes.
		sb->SetStatusText(msg, 0);
	}
	// Also log to the wx log target so a user who missed the brief
	// status-bar update can find it in the output window.
	wxLogMessage(wxT("%s"), msg);
}
