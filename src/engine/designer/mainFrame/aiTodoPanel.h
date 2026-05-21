/////////////////////////////////////////////////////////////////////////////
// ibAiTodoPanel — dockable AI Assistant TODO list (Workmate parity).
//
// Workmate quote: "Управление: TODO лист, Подзадача, Команда системы,
// Проекты". This panel surfaces the TODO half of that quartet directly
// inside Designer so the user can see and manage the accumulated agent
// task list across conversations.
//
// Behaviour:
//   - Rows are loaded from ibAiTodoStore at construction; saved back on
//     every mutation (add / toggle status / delete).
//   - Each row carries a status checkbox, the title, and the planId
//     (clickable to focus the chat pane and scroll to that plan entry).
//   - "+ Добавить задачу" button opens the chat pane with the input
//     pre-filled "TODO: ".
//
// Russian UI throughout — Workmate parity ships in Russian.
//
// The pane lives on the right side of the AUI manager next to the
// metadata/help/AI chat panes; it is created lazily on first toggle.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_AI_TODO_PANEL_H_
#define _IB_AI_TODO_PANEL_H_

#include "aiTodoStore.h"

#include <wx/panel.h>
#include <wx/string.h>

#include <vector>

class wxListView;
class wxListEvent;
class wxButton;
class wxStaticText;
class wxTextCtrl;
class wxCommandEvent;

class ibAiTodoPanel : public wxPanel {
public:
	ibAiTodoPanel(wxWindow* parent, int id = wxID_ANY);
	~ibAiTodoPanel() override = default;

	// Move keyboard focus into the quick-add input. Called by the
	// Designer when the pane is brought up via menu / shortcut.
	void FocusQuickAdd();

	// Append one task and re-save. Idempotent on id collision (replaces
	// the existing row). Used by external code that wants to inject a
	// task without going through the chat pane — e.g. a future agent that
	// observes a plan and forwards summarised rows here.
	void AppendItem(const ibAiTodoStore::Item& item);

	// Re-read from disk and refresh the list. Useful when the
	// configuration hash changes (configuration reload, tests).
	void Reload();

private:
	void OnAddClick(wxCommandEvent& event);
	void OnDeleteClick(wxCommandEvent& event);
	void OnToggleStatusClick(wxCommandEvent& event);
	void OnRowActivated(wxListEvent& event);
	void OnQuickAddEnter(wxCommandEvent& event);

	// Rebuild the wxListView from m_items. Cheap (single-digit ms even at
	// 500 items) so we don't bother diffing — call this after any mutation.
	void Rebuild();

	// Append a new TODO row with given title, plan id, and initial status.
	// Returns the id of the freshly-created item.
	wxString CreateItem(const wxString& title,
	                    const wxString& planId,
	                    const wxString& status);

	// Cycle a row's status pending → inProgress → done → pending. Sets
	// completedAt when transitioning into "done"; clears it on the way
	// back. Triggers Save().
	void CycleStatus(size_t row);

	// Delete the row at `index` from m_items + persist. Out-of-range
	// indices are a no-op so a stale event from a removed row can't crash.
	void DeleteAt(size_t row);

	// Persist m_items to disk. Errors are logged via wxLogWarning by the
	// store — UI does not need to surface them; a single failed Save is
	// recoverable on the next mutation.
	void Save();

	// Push a chat-pane envelope ("editor.skill" with op="send") containing
	// "TODO: <text>" so the assistant can pick up the new task. Best-effort
	// — silently no-ops if the AI pane isn't registered yet.
	void DispatchToAiPane(const wxString& title);

	wxString m_configHash;
	std::vector<ibAiTodoStore::Item> m_items;

	wxListView*   m_list           = nullptr;
	wxButton*     m_addButton      = nullptr;
	wxButton*     m_deleteButton   = nullptr;
	wxButton*     m_toggleButton   = nullptr;
	wxStaticText* m_statusLine     = nullptr;
	wxTextCtrl*   m_quickAdd       = nullptr;
};

#endif // _IB_AI_TODO_PANEL_H_
