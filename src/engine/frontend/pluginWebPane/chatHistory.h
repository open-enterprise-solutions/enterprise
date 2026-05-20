/////////////////////////////////////////////////////////////////////////////
// ibChatHistory — per-configuration chat transcript persistence for
// ibPluginWebPane.
//
// Storage layout: <wxStandardPaths::GetUserConfigDir>/OES/chat/<hash>.json
//   - macOS:   ~/Library/Preferences/OES/chat/<hash>.json
//   - Linux:   ~/.config/OES/chat/<hash>.json  (XDG default)
//   - Windows: %APPDATA%/OES/chat/<hash>.json
//
// configHash key:
//   std::hash<std::string>(GetConfigName() utf8) hex-encoded.
//   Falls back to a static "default" bucket when activeMetaData is not
//   loaded yet (tests, designer cold-start before LoadConfigFromFile).
//
// JSON shape (version 1):
//   {
//     "version": 1,
//     "savedAt": "2026-05-20T12:34:56Z",
//     "entries": [
//       { "role": "user|assistant|system|error|plan|tripleReview",
//         "markdown": "...",
//         "requestId": "...",
//         "planId": "...",
//         "conversationId": "...",
//         "rationale": "...",
//         "mutations": "...",
//         "applied": false,
//         "verdict": "PASS|WARN|BLOCK",       // tripleReview only
//         "verdictReason": "...",              // tripleReview only
//         "countP0": 0, "countP1": 0,
//         "countP2": 0, "countP3": 0,
//         "reviewers": [ {"model":"...","content":"...","p0":0,"p1":0,
//                         "p2":0,"p3":0,"latencyMs":0} ],
//         "findings": [ {"severity":"P0","reviewer":"...","line":0,
//                         "message":"...","fix":"..."} ] }
//     ]
//   }
//
// codeBlocks are NOT persisted — RenderTranscript() re-extracts them from
// `markdown` on the next render pass, so they round-trip for free.
//
// Backward compatibility: when reading a file produced before the
// tripleReview fields were added, the missing fields parse as their
// defaults (empty strings / zero ints / empty arrays). No version bump.
//
// Hard cap: kMaxEntries (200). Save() drops oldest entries beyond the cap
// before writing to disk so a long session can't fill /Library/Preferences
// indefinitely.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_CHAT_HISTORY_H_
#define _IB_CHAT_HISTORY_H_

#include "frontend/pluginWebPane/pluginWebPane.h"

#include <wx/string.h>

#include <vector>

namespace ibChatHistory {

// Hard cap on stored entries — drop oldest first when exceeded.
constexpr size_t kMaxEntries = 200;

// Compute the per-configuration hash bucket. Reads
// activeMetaData->GetConfigName() if a configuration is loaded, otherwise
// returns a stable fallback bucket ("default"). Tests can override via
// the explicit-bucket overload.
wxString ComputeConfigHash();
wxString ComputeConfigHashFor(const wxString& configName);

// Absolute path to the JSON file for the given bucket. Creates the parent
// directory chain on demand (idempotent). Returns wxEmptyString if the
// platform user-config dir is itself unavailable (extreme — e.g. sandboxed
// CI with $HOME unset).
wxString PathForBucket(const wxString& configHash);

// Persist the transcript synchronously. Truncates to kMaxEntries (oldest
// dropped) before writing. Returns true on success.
//
// Atomicity: writes to "<path>.tmp" then renames over the destination so a
// crash mid-write cannot corrupt an existing valid transcript.
bool Save(const wxString& configHash,
          const std::vector<ibPluginWebPane::Entry>& entries);

// Load transcript from disk. Returns true if a file was found and parsed
// successfully; out-param `entries` is replaced. Missing file is NOT an
// error — returns false silently with `entries` cleared. Corrupt JSON
// returns false and logs via wxLogWarning.
bool Load(const wxString& configHash,
          std::vector<ibPluginWebPane::Entry>& entries);

// Delete the persisted file for the bucket. Returns true if the file was
// removed (or did not exist). Used by the "New chat" button.
bool Clear(const wxString& configHash);

} // namespace ibChatHistory

#endif // _IB_CHAT_HISTORY_H_
