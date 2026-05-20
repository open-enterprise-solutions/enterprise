/////////////////////////////////////////////////////////////////////////////
// ibChatContextPopup — implementation. Mirrors ibSlashCommandPopup with a
// different suggestion source (ibChatContext::CollectSuggestions) and a
// different row format ("<Kind> <FullName>" vs "<cmd> — <desc>").
/////////////////////////////////////////////////////////////////////////////

#include "frontend/pluginWebPane/chatContextPopup.h"

#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/intl.h>
#include <wx/settings.h>

namespace {

constexpr int kMaxVisibleRows = 8;

} // namespace

ibChatContextPopup::ibChatContextPopup(wxWindow* parent, SelectCallback onSelect)
    : wxPopupTransientWindow(parent, wxBORDER_SIMPLE)
    , m_onSelect(std::move(onSelect))
{
	auto* sizer = new wxBoxSizer(wxVERTICAL);
	m_list = new wxListBox(this, wxID_ANY,
	                        wxDefaultPosition, wxDefaultSize,
	                        0, nullptr,
	                        wxLB_SINGLE | wxLB_NEEDED_SB);
	m_list->SetBackgroundColour(
	    wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
	sizer->Add(m_list, 1, wxEXPAND);
	SetSizer(sizer);
	m_list->Bind(wxEVT_LISTBOX_DCLICK, &ibChatContextPopup::OnListDClick, this);
}

void ibChatContextPopup::UpdatePrefix(const wxString& prefix, wxWindow* anchor)
{
	m_suggestions = ibChatContext::CollectSuggestions(prefix, /*cap=*/50);

	if (m_suggestions.empty()) {
		if (IsShown()) Dismiss();
		return;
	}

	wxArrayString labels;
	for (const auto& s : m_suggestions) {
		// Two-column layout via spaces — wxListBox has no native column
		// support, but the kind label is short (≤ ~20 chars) so a fixed
		// gap reads cleanly enough.
		labels.Add(s.kindLabel + wxT("  ") + s.fullName);
	}
	m_list->Set(labels);
	m_list->SetSelection(0);

	int width = 320;
	wxPoint origin(0, 0);
	if (anchor != nullptr) {
		width  = anchor->GetSize().GetWidth();
		origin = anchor->ClientToScreen(
		    wxPoint(0, anchor->GetSize().GetHeight()));
	}
	const int rowH = m_list->GetCharHeight() + 6;
	const int rows = static_cast<int>(m_suggestions.size());
	const int visible = (rows < kMaxVisibleRows) ? rows : kMaxVisibleRows;
	const int height = rowH * visible + 4;

	SetSize(width, height);
	Position(origin, wxSize(0, 0));
	if (!IsShown()) Popup();
}

void ibChatContextPopup::MoveSelection(int delta)
{
	if (m_suggestions.empty()) return;
	const int n = static_cast<int>(m_suggestions.size());
	int sel = m_list->GetSelection();
	if (sel == wxNOT_FOUND) sel = 0;
	sel = (sel + delta) % n;
	if (sel < 0) sel += n;
	m_list->SetSelection(sel);
}

void ibChatContextPopup::AcceptSelection()
{
	if (m_suggestions.empty()) return;
	int sel = m_list->GetSelection();
	if (sel == wxNOT_FOUND) sel = 0;
	if (sel < 0 || sel >= static_cast<int>(m_suggestions.size())) return;
	const wxString full = m_suggestions[sel].fullName;
	Dismiss();
	if (m_onSelect) m_onSelect(full);
}

void ibChatContextPopup::OnListDClick(wxCommandEvent& /*event*/)
{
	AcceptSelection();
}
