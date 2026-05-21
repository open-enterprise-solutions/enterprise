/////////////////////////////////////////////////////////////////////////////
// ibAiTodoStore — persistent TODO list for the Designer AI Assistant panel.
//
// Workmate parity: the assistant accumulates a list of pending and completed
// tasks across conversations. The list survives Designer restarts so the
// user can pick up where they left off — the same model Workmate uses for
// its "TODO лист / Подзадача / Команда системы / Проекты" sidebar.
//
// Storage layout: <wxStandardPaths::GetUserConfigDir>/OES/ai-todo/<hash>.json
//   - macOS:   ~/Library/Preferences/OES/ai-todo/<configHash>.json
//   - Linux:   ~/.config/OES/ai-todo/<configHash>.json
//   - Windows: %APPDATA%/OES/ai-todo/<configHash>.json
//
// configHash matches ibChatHistory::ComputeConfigHash so a TODO row created
// for "DemoConfig" lives next to that configuration's chat transcript — the
// two stay paired across configuration switches.
//
// JSON shape (version 1):
//   {
//     "version": 1,
//     "savedAt": "2026-05-21T09:00:00Z",
//     "items":   [
//       {
//         "id":       "todo-<unix-ms>-<counter>",
//         "title":    "Создать справочник Контрагенты",
//         "status":   "pending|done|inProgress",
//         "planId":   "plan-001",         // empty if not tied to a chat plan
//         "createdAt":"2026-05-21T09:00:00Z",
//         "completedAt":"2026-05-21T09:15:00Z"  // empty if status != done
//       }
//     ]
//   }
//
// Hard cap: kMaxItems (500). Save() drops oldest items (completed first)
// beyond the cap so a long-running session can't grow the file without
// bound. We prefer dropping completed items because they're the lowest-
// signal rows — a long pending list is a real reminder the user wants
// to see.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_AI_TODO_STORE_H_
#define _IB_AI_TODO_STORE_H_

#include <wx/string.h>

#include <vector>

namespace ibAiTodoStore {

constexpr size_t kMaxItems = 500;

struct Item {
	wxString id;
	wxString title;
	wxString status;        // "pending" / "inProgress" / "done"
	wxString planId;        // empty when not tied to a chat plan
	wxString createdAt;     // ISO-8601, UTC
	wxString completedAt;   // ISO-8601, UTC; empty when not done
};

// Use the same configHash bucket as ibChatHistory so TODO + chat travel
// together. Falls back to "default" when no configuration is loaded
// (designer cold-start, tests).
wxString ComputeConfigHash();

// Absolute path to the JSON file for the given bucket. Creates the parent
// directory chain on demand (idempotent). Returns wxEmptyString when the
// platform user-config dir is unavailable.
wxString PathForBucket(const wxString& configHash);

// Persist the list synchronously. Truncates to kMaxItems (oldest completed
// dropped first, then oldest pending) before writing. Returns true on
// success. Atomic-rename via ".tmp" tail — same pattern ibChatHistory uses.
bool Save(const wxString& configHash, const std::vector<Item>& items);

// Load list from disk. Returns true on success. Missing file is NOT an
// error — returns false with `items` cleared. Corrupt JSON returns false
// and logs via wxLogWarning.
bool Load(const wxString& configHash, std::vector<Item>& items);

// Mint a new id of the form "todo-<unix-ms>-<counter>". Counter is a
// static thread-local so two same-millisecond calls don't collide.
wxString NewId();

// ISO-8601 UTC timestamp for createdAt / completedAt.
wxString NowIso();

} // namespace ibAiTodoStore

#endif // _IB_AI_TODO_STORE_H_
