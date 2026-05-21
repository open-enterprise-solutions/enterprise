/////////////////////////////////////////////////////////////////////////////
// ibPluginWebPane — native plugin-driven assistant pane inside the host.
//
// PLATFORM HISTORY (2026-05-20 refactor):
//   - Original: wxWebView-based pane loading an HTML/JS/CSS bundle. Heavy
//     dependency on platform-specific browser runtime (WebKit on macOS,
//     WebView2 on Windows); message dispatch traversed JS bridge with
//     several object-lifetime hazards and noticeable UI latency.
//   - Now: native wxPanel composition built from wx::core widgets only —
//     wxHtmlWindow for the rendered transcript, wxTextCtrl for input.
//     Lightweight (Code::Blocks AI plugin pattern), zero JS, zero
//     external browser process. Markdown rendered server-side via the
//     vendored md4c CommonMark parser at src/3rdparty/md4c.
//
// Channel contract (unchanged from plugin POV):
//   - host → pane:  PushMessage(jsonInline) parses one of the standard
//                   envelopes — chat.delta / chat.end / error /
//                   agent.plan / agent.applied — and updates the
//                   transcript.
//   - pane → host:  user input or button events build a chat.send
//                   envelope and invoke the plugin's onMessage callback
//                   exactly the way wxWebView's postMessage did.
//
// The htmlBundlePath constructor arg stays in the signature for ABI v4
// compatibility but is now ignored — the pane does not load a static
// bundle anymore. Plugins can pass an empty string.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PLUGIN_WEB_PANE_H_
#define _IB_PLUGIN_WEB_PANE_H_

#include "frontend/mainFrame/mainFrame.h"  // FRONTEND_API

#include "backend/plugin/pluginApi.h"      // ibPluginWebMsgFn

#include <wx/panel.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <vector>
#include <set>

class wxHtmlWindow;
class wxTextCtrl;
class wxButton;
class wxChoice;
class wxStaticText;
class wxCommandEvent;
class wxThreadEvent;
class wxHtmlLinkEvent;
class wxKeyEvent;
class wxFocusEvent;
class wxListBox;
class wxMouseEvent;

class ibSlashCommandPopup;
class ibChatContextPopup;

class FRONTEND_API ibPluginWebPane : public wxPanel {
public:
	// paneId is the stable identifier the plugin registered. title is the
	// visible AUI caption. htmlBundlePath is RETAINED in the signature for
	// source-level compatibility with the previous wxWebView pane but is
	// IGNORED — the new native pane is self-contained. onMessage receives
	// user-driven envelopes (chat.send / agent.approve / etc); userData is
	// forwarded unchanged.
	ibPluginWebPane(wxWindow* parent,
	                const wxString& paneId,
	                const wxString& title,
	                const wxString& htmlBundlePath,
	                ibPluginWebMsgFn onMessage,
	                void* userData);

	// Destructor — flushes the debounced save timer so a pending write
	// hits disk before the pane is torn down. Pane shutdown happens via
	// wxWindow ownership (parent::Destroy() cascades), but in practice
	// pluginManager calls Destroy explicitly during unload; either path
	// runs this dtor on the main thread.
	~ibPluginWebPane() override;

	// Push a host-originated JSON envelope into the pane. Parsed locally
	// and applied to the transcript. Thread-safe: off-UI callers marshal
	// through a queued wxThreadEvent so wxHtmlWindow stays on the main
	// thread.
	void PushMessage(const wxString& jsonInline);

	const wxString& GetPaneId() const { return m_paneId; }
	const wxString& GetTitle()  const { return m_title;  }

	// One transcript entry. Markdown is the canonical body; HTML is the
	// rendered cache used during incremental re-renders. role drives the
	// visual styling (Вы / Ассистент / Система / Ошибка).
	//
	// Public so the file-scope RoleLabel helper in the implementation
	// can reference Entry::Role without a friend declaration. The class
	// invariant — only the pane mutates m_entries — is enforced by the
	// fact that the vector itself stays private. Declared BEFORE the
	// test-only hooks so AppendEntryForTests can take an Entry by value
	// without a forward declaration trick.
	struct Entry {
		enum class Role { User, Assistant, System, Error, Plan, TripleReview, Subtasks };

		// One subtask row inside an `agent.subtask` envelope. Status drives
		// the rendered icon in the tree view; "pending" / "running" / "done"
		// / "failed" are the canonical values, but unknown future tags fall
		// through to the neutral pending icon so a forward-rev envelope
		// never blanks the row.
		struct Subtask {
			wxString id;
			wxString title;
			wxString status;                  // "pending" / "running" / "done" / "failed"
			std::vector<wxString> dependsOn;  // ids of subtasks that must finish first
		};

		// Per-reviewer summary in a TripleReview entry. One row per LLM
		// model that participated; counts are the model's own
		// [P0]/[P1]/[P2]/[P3] findings and latencyMs is the wall-clock
		// cost of that model's call.
		struct ReviewerSummary {
			wxString model;
			wxString content;
			int      p0        = 0;
			int      p1        = 0;
			int      p2        = 0;
			int      p3        = 0;
			long     latencyMs = 0;
		};

		// Single finding inside a TripleReview entry. Severity is the raw
		// "P0"/"P1"/"P2"/"P3" tag emitted by aiBridge; we keep it as a
		// string so future severity additions (e.g. "INFO") flow through
		// without a schema change here.
		struct Finding {
			wxString severity;
			wxString reviewer;
			int      line = 0;
			wxString message;
			wxString fix;
		};

		Role     role     = Role::User;
		wxString markdown;
		wxString requestId;          // for streaming reconciliation
		wxString planId;             // agent.plan / agent.applied only
		wxString conversationId;     // agent.plan / agent.applied only
		wxString rationale;          // agent.plan only
		wxString mutations;          // agent.plan only
		bool     applied  = false;   // agent.applied set this true
		// COMMIT-MSG: marks an assistant entry whose body is a proposed
		// git commit message (i.e. it was generated for an editor.skill
		// op="commit" turn). Renders an extra "Использовать как коммит"
		// link on each code block in the entry; oes-commit:<e>:<b> link
		// scheme runs `git commit -m <body>` after user confirmation.
		bool     commitSuggestion = false;
		// Fenced code blocks extracted from `markdown` during render.
		// First = language tag, second = raw code body. Populated by
		// RenderTranscript so OnLinkClicked can resolve oes-copy /
		// oes-apply URLs back to the original text without re-parsing.
		std::vector<std::pair<wxString, wxString>> codeBlocks;

		// agent.tripleReview only. Verdict is "PASS"/"WARN"/"BLOCK" (string
		// so unknown future values render without code change). Counts are
		// the aggregated tally across all reviewers.
		wxString verdict;
		wxString verdictReason;
		int      countP0 = 0;
		int      countP1 = 0;
		int      countP2 = 0;
		int      countP3 = 0;
		std::vector<ReviewerSummary> reviewers;
		std::vector<Finding>         findings;

		// agent.subtask only. Workmate-style task decomposition: an agent
		// plan that's too large for one context window emits a list of
		// child tasks here. parentPlanId links back to the originating
		// agent.plan entry so the panel can render a parent/children tree.
		// Subsequent agent.subtaskStatus envelopes (matched by parentPlanId)
		// mutate `subtasks[i].status` in place and trigger a re-render.
		wxString             parentPlanId;
		std::vector<Subtask> subtasks;
	};

	// Test-only hooks. SetConfigHashForTests installs a stable bucket
	// (production reads from activeMetaData) and ReloadHistoryFromDisk
	// re-runs the load path against the now-current bucket so a fresh
	// pane can verify "saved-then-restored" without process restart.
	void SetConfigHashForTests(const wxString& configHash);
	void ReloadHistoryFromDisk();
	void SetPersistEnabledForTests(bool enabled);
	size_t GetEntryCountForTests() const { return m_entries.size(); }
	void AppendEntryForTests(Entry entry);

private:

	wxHtmlWindow*    m_history    = nullptr;
	wxTextCtrl*      m_input      = nullptr;
	wxButton*        m_sendBtn    = nullptr;
	wxButton*        m_newChatBtn = nullptr;
	wxButton*        m_pinContextBtn = nullptr;
	wxButton*        m_clearPinnedContextBtn = nullptr;
	wxChoice*        m_modelProfile = nullptr;
	wxChoice*        m_permissionMode = nullptr;
	wxStaticText*    m_syntaxModeLabel = nullptr;
	wxStaticText*    m_pinnedContextLabel = nullptr;
	wxStaticText*    m_statusBar  = nullptr;

	wxString         m_paneId;
	wxString         m_title;
	ibPluginWebMsgFn m_onMessage  = nullptr;
	void*            m_userData   = nullptr;

	// Per-configuration storage bucket for the persisted transcript.
	// Computed once at construction from activeMetaData's config name so
	// switching configurations starts a fresh chat — same model 1С
	// Workmate uses ("each конфигурация has its own chat history").
	wxString         m_configHash;

	// Debounced save timer. Fires 500ms after the most recent append so
	// a busy chat.delta stream doesn't hit the disk on every chunk.
	wxTimer          m_saveTimer;

	// Disable persistence — set by tests that don't want stray files
	// under ~/Library/Preferences. Default false (production = persist).
	bool             m_persistEnabled = true;

private:
	std::vector<Entry> m_entries;

	// Indicates we are currently accumulating chat.delta chunks for an
	// in-flight assistant turn. Set on first chat.delta, cleared on
	// chat.end / error. m_pendingRequestId remembers which requestId is
	// active so out-of-order envelopes can be sanity-checked.
	bool             m_pending           = false;
	wxString         m_pendingRequestId;

	// COMMIT-MSG: requestIds whose assistant reply should be tagged as
	// a commit-message suggestion. Populated when DispatchEnvelope sees
	// an editor.skill op="commit" envelope (or when DispatchUserPrompt
	// rewrites a /commit slash to chat.send). Consulted by
	// AppendStreamingDelta when it creates the streaming entry, so the
	// "Использовать как коммит" link only renders on the right reply
	// and not on every code block in the transcript.
	std::set<wxString> m_commitRequestIds;

	// COMMIT-MSG: one-shot sentinel — when an editor.skill op="commit"
	// envelope routes through DispatchUserPrompt (which mints a fresh
	// rid for the outbound chat.send), this flag tells the next
	// DispatchUserPrompt call to register the new rid in
	// m_commitRequestIds. Cleared inside DispatchUserPrompt.
	bool             m_nextPromptIsCommit = false;

	enum class PermissionMode {
		ReadOnly = 0,
		ConfirmAll,
		ConfirmWrites,
		Auto
	};
	PermissionMode  m_permissionModeValue = PermissionMode::ConfirmWrites;
	bool            m_planPolicyElevated  = false;
	std::vector<wxString> m_pinnedContextTokens;

	wxString CurrentModelProfileId() const;
	wxString CurrentModelProfileLabel() const;
	void OnModelProfileChanged(wxCommandEvent& event);
	void OnPermissionModeChanged(wxCommandEvent& event);
	void ApplyPermissionModePolicy();
	void ElevatePlanPolicyForApply();
	void RestorePlanPolicyAfterApply();
	bool ConfirmAgentAction(const wxString& actionLabel) const;
	bool IsReadOnlyMode() const { return m_permissionModeValue == PermissionMode::ReadOnly; }

	void OnSendClicked(wxCommandEvent& event);
	void OnInputEnter(wxCommandEvent& event);
	void OnInputText(wxCommandEvent& event);
	void OnInputKeyDown(wxKeyEvent& event);
	void OnInputKillFocus(wxFocusEvent& event);
	void OnLinkClicked(wxHtmlLinkEvent& event);
	void OnPushFromOtherThread(wxThreadEvent& event);
	void OnPinContextClicked(wxCommandEvent& event);
	void OnClearPinnedContextClicked(wxCommandEvent& event);
	void RefreshPinnedContextLabel();
	wxString BuildPinnedContextProbe(const wxString& prompt) const;
	bool HasRecentMetadataCreationContext() const;

	// Slash-command autocomplete popup. Owned by the pane; created lazily
	// the first time the user types '/'. Anchored below m_input and
	// dismissed when focus moves out or the prefix stops matching.
	ibSlashCommandPopup* m_slashPopup = nullptr;

	// "@" context-token autocomplete popup. Lazily created on first '@'
	// keystroke; lists matching metadata objects and editor specials
	// (@selection / @file / @open). Same dismiss policy as the slash
	// popup so keyboard / focus handling is uniform.
	ibChatContextPopup*  m_atPopup    = nullptr;

	// New-chat button handler — confirms with the user, clears m_entries,
	// deletes the persisted file, and re-renders the empty transcript.
	void OnNewChatClicked(wxCommandEvent& event);

	// Debounced-save timer fire. Persists m_entries to disk and resets
	// the timer. Defensive against re-entry from cascaded events.
	void OnSaveTimer(wxTimerEvent& event);

	// Schedule a save. Resets the debounce timer so a flurry of appends
	// collapses into one disk write 500ms after the last entry.
	void ScheduleSave();

	// Force a save now (test hook + path used at destruction).
	void SaveNow();

	// Update / show / hide the slash popup based on m_input's current
	// contents. Called from OnInputText.
	void RefreshSlashPopup();

	// Update / show / hide the @-token popup. Driven by OnInputText.
	void RefreshAtPopup();

	// Insert "@<fullName> " into m_input at the cursor, replacing the
	// trailing "@<partial>" the user was typing. Mirrors the slash flow.
	void AcceptAtSuggestion(const wxString& fullName);

	// Replace the leading "/<partial>" prefix in m_input with the picked
	// command plus a trailing space, and dismiss the popup. Wired into
	// ibSlashCommandPopup via a callback.
	void AcceptSlashCommand(const wxString& command);

	// If `text` starts with a known slash command, emit an editor.skill
	// envelope and return true. Otherwise return false so the caller falls
	// back to the standard chat.send path.
	bool TryDispatchSlashCommand(const wxString& text);

	// oes-copy: / oes-apply: link handlers — extract the code block
	// referenced by the URL's <entryIdx>:<blockIdx> spec and either
	// shove it onto the clipboard or splice it into the focused
	// wxStyledTextCtrl.
	void HandleCopyLink(const wxString& spec);
	void HandleApplyLink(const wxString& spec);

	// COMMIT-MSG: oes-commit:<entryIdx>:<blockIdx> link handler.
	// Confirms with the user, copies the message to the clipboard,
	// and (when confirmed) runs `git commit -m "<message>"` via
	// wxExecute. Reports outcomes back into the transcript as
	// System / Error entries.
	void HandleCommitLink(const wxString& spec);
	void HandleResumeLink(const wxString& spec);

	// Parse the inbound envelope and apply it. Pure UI-thread work; the
	// off-thread variant queues a wxThreadEvent that ends up here.
	void DispatchEnvelope(const wxString& jsonInline);

	// Append a new transcript entry and re-render.
	void AppendEntry(Entry entry);

	// Update the assistant streaming entry by appending delta text.
	// Creates the entry on first call.
	void AppendStreamingDelta(const wxString& requestId, const wxString& delta);

	// Close the current streaming entry (chat.end). meta is rendered into
	// the status bar (model, token count).
	void CompleteStreaming(const wxString& requestId, const wxString& meta);

	// Build the full HTML transcript and SetPage it on the history widget.
	void RenderTranscript();

	// Helper: send a chat.send envelope built from the input box contents.
	void DispatchUserPrompt(const wxString& prompt);
	void DispatchAgentPrompt(const wxString& prompt,
	                         const wxString& displayPrompt = wxString());

	// Toggle the Send button label between "Отправить" and "Стоп" based
	// on m_pending. Routed through one helper so every entry/exit point
	// of the streaming state machine stays in sync — no scattered
	// SetLabel calls to forget.
	void UpdateSendButtonLabel();

	// Build an agent.cancel envelope for the in-flight request and ship
	// it through m_onMessage so the worker side trips its cancel token
	// and stops emitting further chat.delta chunks.
	void DispatchCancelRequest();
};

#endif // _IB_PLUGIN_WEB_PANE_H_
