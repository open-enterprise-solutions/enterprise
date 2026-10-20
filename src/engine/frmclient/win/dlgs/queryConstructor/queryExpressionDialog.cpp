////////////////////////////////////////////////////////////////////////////
//	Description : "Arbitrary expression" — one editor for every expression
//	              (queryExpressionDialog.h)
////////////////////////////////////////////////////////////////////////////

// THE DESKTOP'S FILE (frontend/win/dlgs/queryConstructor/queryExpressionDialog.cpp), so far its role editor
// (ibEditBalanceRole): a word and a number, picked. It knows no query and no configuration — the words are the
// query language's own (core/query/queryBalanceRole.h) — so it runs on the client as it is and asks the server nothing.
//
// ⚠ NOT HERE YET: ibDialogQueryExpression's body — it parses and renders through the engine (see the header).

#include "queryExpressionDialog.h"

#include "core/query/queryBalanceRole.h"   // ibBalanceRoleWord, ibSourcePeriodRank

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>     // the role constructor — the word picked
#include <wx/spinctrl.h>   // …and a period's number
#include <iterator>        // std::size — GCC does not bring it along with the rest

// ⭐ THE ROLE EDITOR — the word the query writes (`ROLE OPENING`), picked rather than typed, and for a PERIOD its
// number: which period decides "first" and "last" before the others. "As its source says" leaves the field what
// its source says it is; NONE takes that away. The words are the query's own, as everything in the constructor is
// (it speaks the language it writes). One window — see the declaration for who opens it.
bool ibEditBalanceRole(wxWindow* parent, bool& said, ibBalanceRole& role, int& periodRank, bool readOnly, bool withNumber)
{
	wxDialog dialog(parent, wxID_ANY, _("Role"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	static const ibBalanceRole s_roles[] = { ibBalanceRole::Moment, ibBalanceRole::Dimension,
		ibBalanceRole::Opening, ibBalanceRole::Closing, ibBalanceRole::None };
	wxArrayString words;
	words.Add(_("As its source says"));
	for (const ibBalanceRole known : s_roles)
		words.Add(ibBalanceRoleWord(known));
	wxChoice* choice = new wxChoice(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, words);
	int at = 0;
	if (said)
		for (size_t i = 0; i < std::size(s_roles); ++i)
			if (s_roles[i] == role)
				at = static_cast<int>(i) + 1;
	choice->SetSelection(at);
	choice->Enable(!readOnly);
	sizer->Add(new wxStaticText(&dialog, wxID_ANY, _("What the field is in a balance:")), 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(8));
	sizer->Add(choice, 0, wxEXPAND | wxALL, dialog.FromDIP(8));

	// A PERIOD'S NUMBER — 0 is "in the order it stands": the platform counts it after the numbered ones.
	wxSpinCtrl* rank = nullptr;
	if (withNumber) {
		wxBoxSizer* rankRow = new wxBoxSizer(wxHORIZONTAL);
		rankRow->Add(new wxStaticText(&dialog, wxID_ANY, _("Period number (0 = in order):")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dialog.FromDIP(6));
		rank = new wxSpinCtrl(&dialog, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
			wxSP_ARROW_KEYS, 0, ibSourcePeriodRank(0) - 1, periodRank);
		rankRow->Add(rank, 0);
		sizer->Add(rankRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(8));
		const auto syncRank = [choice, rank, readOnly]() { rank->Enable(!readOnly && choice->GetSelection() == 1); };   // PERIOD only
		choice->Bind(wxEVT_CHOICE, [syncRank](wxCommandEvent&) { syncRank(); });
		syncRank();
	}

	sizer->Add(dialog.CreateStdDialogButtonSizer(readOnly ? wxCLOSE : (wxOK | wxCANCEL)), 0, wxEXPAND | wxALL, dialog.FromDIP(8));
	dialog.SetSizerAndFit(sizer);
	if (readOnly) {
		dialog.Bind(wxEVT_BUTTON, [&dialog](wxCommandEvent&) { dialog.EndModal(wxID_CANCEL); }, wxID_CLOSE);
		dialog.ShowModal();
		return false;
	}
	if (dialog.ShowModal() != wxID_OK)
		return false;

	const int picked = choice->GetSelection();
	said       = picked > 0;
	role       = said ? s_roles[picked - 1] : ibBalanceRole::None;
	periodRank = said && role == ibBalanceRole::Moment && rank != nullptr ? rank->GetValue() : 0;
	return true;
}
