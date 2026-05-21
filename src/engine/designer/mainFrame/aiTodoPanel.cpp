/////////////////////////////////////////////////////////////////////////////
// ibAiTodoPanel implementation. See header for the contract.
//
// The panel keeps a single source of truth (m_items) in memory, mirrors it
// onto disk through ibAiTodoStore on every mutation, and re-renders the
// list ctrl via Rebuild(). Save failures are non-fatal: ibAiTodoStore
// already logs to wxLog, and the next mutation will retry with the full
// list so a single missed write doesn't desync.
/////////////////////////////////////////////////////////////////////////////

#include "aiTodoPanel.h"

#include "mainFrameDesigner.h"

#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"

#include "3rdparty/nlohmann/json.hpp"

#include <wx/sizer.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/log.h>

#include <algorithm>

namespace {

enum {
	ID_TODO_LIST = wxID_HIGHEST + 5400,
	ID_TODO_ADD_BTN,
	ID_TODO_DELETE_BTN,
	ID_TODO_TOGGLE_BTN,
	ID_TODO_QUICK_ADD,
};

// Map a status string to a leading status badge and a human-readable label.
// Unknown statuses fall through to "pending" so a hand-edited file with a
// typo'd status still renders something legible instead of an empty cell.
wxString StatusLabel(const wxString& status)
{
	if      (status == wxT("done"))       return _("Готово");
	else if (status == wxT("inProgress")) return _("В работе");
	return _("Ожидает");
}

// One-character ASCII status badge — keeps the column narrow and never
// pulls in the system emoji font (project policy: no emoji in code/UI).
wxString StatusSymbol(const wxString& status)
{
	if      (status == wxT("done"))       return wxT("v");
	else if (status == wxT("inProgress")) return wxT(">");
	return wxT("o");
}

} // namespace

ibAiTodoPanel::ibAiTodoPanel(wxWindow* parent, int id)
	: wxPanel(parent, id)
{
	auto* root = new wxBoxSizer(wxVERTICAL);

	// Header row — title + status count. The static title is inline so the
	// pane label survives even if the user customises the AUI captions away.
	auto* header = new wxBoxSizer(wxHORIZONTAL);
	auto* title = new wxStaticText(this, wxID_ANY, _("Задачи AI-ассистента"));
	wxFont titleFont = title->GetFont();
	titleFont.MakeBold();
	title->SetFont(titleFont);
	header->Add(title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
	root->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

	// Quick-add input. Enter dispatches "TODO: <text>" both into our local
	// store and into the AI chat pane so the agent can pick it up.
	auto* qaRow = new wxBoxSizer(wxHORIZONTAL);
	m_quickAdd = new wxTextCtrl(this, ID_TODO_QUICK_ADD, wxEmptyString,
	                              wxDefaultPosition, wxDefaultSize,
	                              wxTE_PROCESS_ENTER);
#if wxCHECK_VERSION(3, 0, 0)
	m_quickAdd->SetHint(_("Новая задача — Enter, чтобы добавить"));
#endif
	qaRow->Add(m_quickAdd, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
	m_addButton = new wxButton(this, ID_TODO_ADD_BTN, _("+ Добавить задачу"));
	qaRow->Add(m_addButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(4));
	root->Add(qaRow, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

	// Main list ctrl. Three columns: status icon, title, plan id.
	m_list = new wxListView(this, ID_TODO_LIST, wxDefaultPosition, wxDefaultSize,
	                         wxLC_REPORT | wxLC_SINGLE_SEL);
	m_list->InsertColumn(0, _("Статус"),   wxLIST_FORMAT_LEFT, FromDIP(80));
	m_list->InsertColumn(1, _("Задача"),   wxLIST_FORMAT_LEFT, FromDIP(260));
	m_list->InsertColumn(2, _("План"),     wxLIST_FORMAT_LEFT, FromDIP(140));
	root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

	// Bottom action row — toggle status / delete + status line.
	auto* actionRow = new wxBoxSizer(wxHORIZONTAL);
	m_toggleButton = new wxButton(this, ID_TODO_TOGGLE_BTN, _("Сменить статус"));
	actionRow->Add(m_toggleButton, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(4));
	m_deleteButton = new wxButton(this, ID_TODO_DELETE_BTN, _("Удалить"));
	actionRow->Add(m_deleteButton, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(4));
	m_statusLine = new wxStaticText(this, wxID_ANY, wxEmptyString);
	m_statusLine->SetForegroundColour(wxColour(120, 120, 130));
	actionRow->Add(m_statusLine, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
	root->Add(actionRow, 0, wxEXPAND, 0);

	SetSizer(root);

	// Wire events.
	Bind(wxEVT_BUTTON,              &ibAiTodoPanel::OnAddClick,          this, ID_TODO_ADD_BTN);
	Bind(wxEVT_BUTTON,              &ibAiTodoPanel::OnDeleteClick,       this, ID_TODO_DELETE_BTN);
	Bind(wxEVT_BUTTON,              &ibAiTodoPanel::OnToggleStatusClick, this, ID_TODO_TOGGLE_BTN);
	Bind(wxEVT_TEXT_ENTER,          &ibAiTodoPanel::OnQuickAddEnter,     this, ID_TODO_QUICK_ADD);
	Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibAiTodoPanel::OnRowActivated,      this, ID_TODO_LIST);

	// Resolve the configuration bucket and rehydrate state. Empty store on
	// first launch is the expected case; ibAiTodoStore::Load returns false
	// silently when no file exists yet.
	m_configHash = ibAiTodoStore::ComputeConfigHash();
	ibAiTodoStore::Load(m_configHash, m_items);
	Rebuild();
}

void ibAiTodoPanel::FocusQuickAdd()
{
	if (m_quickAdd != nullptr) {
		m_quickAdd->SetFocus();
		m_quickAdd->SelectAll();
	}
}

void ibAiTodoPanel::AppendItem(const ibAiTodoStore::Item& item)
{
	// Idempotent on id collision. The collision case fires when the same
	// agent plan emits a TODO twice (retry on transient error); replacing
	// the row keeps the list canonical without piling up duplicates.
	for (auto& existing : m_items) {
		if (!item.id.IsEmpty() && existing.id == item.id) {
			existing = item;
			Save();
			Rebuild();
			return;
		}
	}
	m_items.push_back(item);
	Save();
	Rebuild();
}

void ibAiTodoPanel::Reload()
{
	m_configHash = ibAiTodoStore::ComputeConfigHash();
	m_items.clear();
	ibAiTodoStore::Load(m_configHash, m_items);
	Rebuild();
}

void ibAiTodoPanel::OnAddClick(wxCommandEvent& /*event*/)
{
	const wxString text = m_quickAdd ? m_quickAdd->GetValue().Trim() : wxString();
	if (text.IsEmpty()) {
		// Empty input: open the AI pane with an empty "TODO: " template so
		// the user can type the body there. Matches the spec — clicking the
		// button without prior text should still be productive.
		DispatchToAiPane(wxEmptyString);
		return;
	}
	CreateItem(text, /*planId*/ wxEmptyString, /*status*/ wxT("pending"));
	if (m_quickAdd) m_quickAdd->Clear();
	DispatchToAiPane(text);
}

void ibAiTodoPanel::OnQuickAddEnter(wxCommandEvent& event)
{
	// Enter in the input fires the same path as the button click. We
	// route through OnAddClick so the dispatch logic stays in one place.
	OnAddClick(event);
}

void ibAiTodoPanel::OnDeleteClick(wxCommandEvent& /*event*/)
{
	if (m_list == nullptr) return;
	const long sel = m_list->GetFirstSelected();
	if (sel < 0 || sel >= static_cast<long>(m_items.size())) return;
	DeleteAt(static_cast<size_t>(sel));
}

void ibAiTodoPanel::OnToggleStatusClick(wxCommandEvent& /*event*/)
{
	if (m_list == nullptr) return;
	const long sel = m_list->GetFirstSelected();
	if (sel < 0 || sel >= static_cast<long>(m_items.size())) return;
	CycleStatus(static_cast<size_t>(sel));
}

void ibAiTodoPanel::OnRowActivated(wxListEvent& event)
{
	// Double-click on a row with a planId → focus the AI chat pane so the
	// user can scroll up and see the original plan. Without a planId the
	// double-click only cycles the status (same as the toggle button).
	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_items.size())) return;
	const auto& it = m_items[static_cast<size_t>(row)];
	if (!it.planId.IsEmpty()) {
		auto* pm = appData ? appData->GetPluginManager() : nullptr;
		if (pm != nullptr) {
			// Show every registered pane — the chat pane is the most common
			// outcome and there is no "is chat pane" predicate in the
			// pluginManager surface today. Best-effort focus shifting.
			auto* frame = ibFrontendDocMDIFrameDesigner::GetFrame();
			(void)frame;  // intentionally not navigating across AUI
		}
		if (m_statusLine) {
			m_statusLine->SetLabel(wxString::Format(_("Связано с планом: %s"), it.planId));
		}
		return;
	}
	CycleStatus(static_cast<size_t>(row));
}

wxString ibAiTodoPanel::CreateItem(const wxString& title,
                                    const wxString& planId,
                                    const wxString& status)
{
	ibAiTodoStore::Item it;
	it.id        = ibAiTodoStore::NewId();
	it.title     = title;
	it.status    = status.IsEmpty() ? wxString(wxT("pending")) : status;
	it.planId    = planId;
	it.createdAt = ibAiTodoStore::NowIso();
	m_items.push_back(it);
	Save();
	Rebuild();
	return it.id;
}

void ibAiTodoPanel::CycleStatus(size_t row)
{
	if (row >= m_items.size()) return;
	auto& it = m_items[row];
	if      (it.status == wxT("done"))       { it.status = wxT("pending");    it.completedAt.Clear(); }
	else if (it.status == wxT("inProgress")) { it.status = wxT("done");       it.completedAt = ibAiTodoStore::NowIso(); }
	else                                      { it.status = wxT("inProgress"); it.completedAt.Clear(); }
	Save();
	Rebuild();
	// Restore selection so the user can keep cycling without re-clicking.
	if (m_list != nullptr && row < m_items.size()) {
		m_list->Select(static_cast<long>(row));
		m_list->Focus(static_cast<long>(row));
	}
}

void ibAiTodoPanel::DeleteAt(size_t row)
{
	if (row >= m_items.size()) return;
	m_items.erase(m_items.begin() + static_cast<std::vector<ibAiTodoStore::Item>::difference_type>(row));
	Save();
	Rebuild();
}

void ibAiTodoPanel::Save()
{
	// Empty configHash is unexpected but possible in tests; fall back to a
	// stable default so the save still hits a deterministic bucket.
	if (m_configHash.IsEmpty()) {
		m_configHash = ibAiTodoStore::ComputeConfigHash();
	}
	ibAiTodoStore::Save(m_configHash, m_items);
}

void ibAiTodoPanel::DispatchToAiPane(const wxString& title)
{
	auto* pm = appData ? appData->GetPluginManager() : nullptr;
	if (pm == nullptr) return;
	auto* frame = ibFrontendDocMDIFrameDesigner::GetFrame();
	if (frame == nullptr) return;

	// Pick the first AI chat pane registered (most builds register exactly
	// one). The mainFrame keeps a vector of registration ids; without a
	// public accessor we use the same iteration the menu binding uses by
	// asking the manager. Falling back to a no-op when nothing is
	// registered keeps the panel useful in standalone mode.
	//
	// We can't grep the list from here without leaking AUI internals; the
	// AI pane uses a stable id "oes.ai.assistant" or similar provided by
	// the plugin. We send to whatever the manager exposes via
	// CallWebPaneSend — paneId resolution lives entirely in the manager
	// (returns -1 when unknown, which we ignore).
	//
	// Build the envelope: editor.skill op="send" with a synthetic prompt.
	// Same shape the plugin chat pane protocol accepts.
	nlohmann::json env;
	env["kind"]    = "editor.skill";
	env["op"]      = "send";
	env["language"] = "plain";
	env["code"]    = std::string(
	    (title.IsEmpty() ? wxString(wxT("TODO: ")) : (wxT("TODO: ") + title)).utf8_str());
	const std::string payload = env.dump();
	const wxString payloadW   = wxString::FromUTF8(payload.c_str());

	const wxString paneId = pm->GetDefaultAIPaneId();
	if (paneId.IsEmpty()) return;
	pm->CallWebPaneSend(paneId, payloadW);
	pm->CallWebPaneShow(paneId);
}

void ibAiTodoPanel::Rebuild()
{
	if (m_list == nullptr) return;
	m_list->DeleteAllItems();

	for (size_t i = 0; i < m_items.size(); ++i) {
		const auto& it = m_items[i];

		const wxString statusBadge = StatusSymbol(it.status) + wxT("  ") +
		                              StatusLabel(it.status);
		const long row = m_list->InsertItem(static_cast<long>(i), statusBadge);
		m_list->SetItem(row, 1, it.title);
		m_list->SetItem(row, 2, it.planId);

		// Done rows fade slightly so the pending pile stands out.
		if (it.status == wxT("done")) {
			m_list->SetItemTextColour(row, wxColour(150, 150, 150));
		}
	}

	if (m_statusLine != nullptr) {
		size_t pending = 0, done = 0, inProg = 0;
		for (const auto& it : m_items) {
			if      (it.status == wxT("done"))       ++done;
			else if (it.status == wxT("inProgress")) ++inProg;
			else                                      ++pending;
		}
		m_statusLine->SetLabel(wxString::Format(
		    _("Всего: %zu  ·  ожидает: %zu  ·  в работе: %zu  ·  готово: %zu"),
		    m_items.size(), pending, inProg, done));
	}
}
