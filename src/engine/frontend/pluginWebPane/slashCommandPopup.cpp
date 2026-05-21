/////////////////////////////////////////////////////////////////////////////
// ibSlashCommandPopup — implementation.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/pluginWebPane/slashCommandPopup.h"

#include "backend/plugin/pluginsConfig.h"

#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/intl.h>
#include <wx/settings.h>

namespace {

// Hard cap on visible rows — wxPopupTransientWindow keeps its own size,
// we size the inner listbox so this many rows fit before a scrollbar
// kicks in. Matches the spec ("Max 6 rows visible at once").
constexpr int kMaxVisibleRows = 6;

} // namespace

std::vector<ibSlashCommandPopup::Command>
ibSlashCommandPopup::AllCommands()
{
	// Lazy-init so wxGetTranslation has a live locale by the time this
	// table is first read (the pane is constructed inside the Designer
	// frame, well after wxLocale is wired up).
	std::vector<Command> commands = {
		{ wxT("/explain"),  wxT("explain"),  _("Объяснить выделенный код"), wxEmptyString, false },
		{ wxT("/review"),   wxT("review"),   _("Проверить код"), wxEmptyString, false },
		{ wxT("/fix"),      wxT("fix"),      _("Исправить ошибки"), wxEmptyString, false },
		{ wxT("/doc"),      wxT("doc"),      _("Сгенерировать документацию"), wxEmptyString, false },
		{ wxT("/send"),     wxT("send"),     _("Отправить выделенное"), wxEmptyString, false },
		{ wxT("/test"),     wxT("test"),     _("Написать тесты"), wxEmptyString, false },
		{ wxT("/refactor"), wxT("refactor"), _("Рефакторинг"), wxEmptyString, false },
		{ wxT("/commit"),   wxT("commit"),   _("Сообщение коммита"), wxEmptyString, false },
		// AGENT-MODE: drives the oes_agent MCP tool — body is the natural-
		// language request ("Создай справочник X с реквизитами…").
		{ wxT("/agent"),    wxT("agent"),    _("Создать объект через агента"), wxEmptyString, false },
	};
	const auto config = pluginsConfig::Load();
	for (const auto& row : config.slashCommands) {
		if (row.name.IsEmpty() || row.prompt.IsEmpty()) continue;
		Command c;
		c.name = row.name;
		c.op = wxT("custom");
		c.description = row.description.IsEmpty()
		                ? _("Пользовательская команда")
		                : row.description;
		c.prompt = row.prompt;
		c.custom = true;
		commands.push_back(std::move(c));
	}
	return commands;
}

ibSlashCommandPopup::ibSlashCommandPopup(wxWindow* parent,
                                         SelectCallback onSelect)
    : wxPopupTransientWindow(parent, wxBORDER_SIMPLE)
    , m_onSelect(std::move(onSelect))
{
	auto* sizer = new wxBoxSizer(wxVERTICAL);

	m_list = new wxListBox(this, wxID_ANY,
	                        wxDefaultPosition, wxDefaultSize,
	                        0, nullptr,
	                        wxLB_SINGLE | wxLB_NEEDED_SB);

	// Soft visual to distinguish from the input box behind it.
	m_list->SetBackgroundColour(
	    wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));

	sizer->Add(m_list, 1, wxEXPAND);
	SetSizer(sizer);

	m_list->Bind(wxEVT_LISTBOX_DCLICK, &ibSlashCommandPopup::OnListDClick, this);
}

void ibSlashCommandPopup::UpdatePrefix(const wxString& prefix, wxWindow* anchor)
{
	const auto& cmds = AllCommands();

	m_visibleIdx.clear();
	wxArrayString labels;
	const wxString needle = prefix.Lower();

	// Match against the command name only — descriptions are display-only.
	// An empty prefix (i.e. user just typed "/") shows everything.
	for (size_t i = 0; i < cmds.size(); ++i) {
		const wxString& name = cmds[i].name;
		if (needle.IsEmpty() || needle == wxT("/")
		    || name.Lower().StartsWith(needle)) {
			m_visibleIdx.push_back(static_cast<int>(i));
			labels.Add(name + wxT("  —  ") + cmds[i].description);
		}
	}

	if (m_visibleIdx.empty()) {
		if (IsShown()) Dismiss();
		return;
	}

	m_list->Set(labels);
	m_list->SetSelection(0);

	// Size the popup: width = anchor width (input box), height = up to
	// kMaxVisibleRows rows worth of listbox content. wxListBox doesn't
	// expose a row height directly; GetCharHeight() + a per-row pad
	// matches the actual rendered row height closely enough on all three
	// desktop platforms.
	int width = 240;
	wxPoint origin(0, 0);
	if (anchor != nullptr) {
		width  = anchor->GetSize().GetWidth();
		origin = anchor->ClientToScreen(
		    wxPoint(0, anchor->GetSize().GetHeight()));
	}
	const int rowH = m_list->GetCharHeight() + 6;
	const int rows = static_cast<int>(m_visibleIdx.size());
	const int visible = (rows < kMaxVisibleRows) ? rows : kMaxVisibleRows;
	const int height = rowH * visible + 4;

	SetSize(width, height);
	Position(origin, wxSize(0, 0));

	if (!IsShown()) {
		Popup();
	}
}

void ibSlashCommandPopup::MoveSelection(int delta)
{
	if (m_visibleIdx.empty()) return;
	const int n = static_cast<int>(m_visibleIdx.size());
	int sel = m_list->GetSelection();
	if (sel == wxNOT_FOUND) sel = 0;
	sel = (sel + delta) % n;
	if (sel < 0) sel += n;
	m_list->SetSelection(sel);
}

void ibSlashCommandPopup::AcceptSelection()
{
	if (m_visibleIdx.empty()) return;
	int sel = m_list->GetSelection();
	if (sel == wxNOT_FOUND) sel = 0;
	const int cmdIdx = m_visibleIdx[sel];
	const auto& cmds = AllCommands();
	if (cmdIdx < 0 || cmdIdx >= static_cast<int>(cmds.size())) return;
	const wxString name = cmds[cmdIdx].name;
	Dismiss();
	if (m_onSelect) m_onSelect(name);
}

void ibSlashCommandPopup::OnListDClick(wxCommandEvent& /*event*/)
{
	AcceptSelection();
}
