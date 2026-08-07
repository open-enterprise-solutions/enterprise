////////////////////////////////////////////////////////////////////////////
//	Description : CASE WHEN, edited as the ordered list it is (queryCaseDialog.h)
////////////////////////////////////////////////////////////////////////////

#include "queryCaseDialog.h"
#include "queryExpressionDialog.h"
#include "queryGridModel.h"

#include "backend/query/queryParser.h"
#include "backend/query/queryRender.h"
#include "backend/query/queryKeywords.h"
#include "backend/backend_exception.h"

#include "artProvider/artProvider.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/artprov.h>

namespace {

// The grid's two columns. Column 0 is reserved by the fork's model, as everywhere else in this
// window — see queryGridModel.h.
enum { kCaseColWhen = 1, kCaseColThen };

wxString Kw(ibQueryKeyword kw) { return ibQueryKeywordText(kw); }

} // namespace

ibDialogQueryCase::ibDialogQueryCase(wxWindow* parent,
                                     const std::vector<ibQueryConstructorField>& fields,
                                     const ibQueryAstExprPtr& existing,
                                     const ibMetaData* metaData, bool readOnly)
	: wxDialog(parent, wxID_ANY, Kw(ibQueryKeyword::Case), wxDefaultPosition, wxSize(760, 420),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_fields(fields)
	, m_metaData(metaData)
	, m_readOnly(readOnly)
{
	// The same picture the constructor and the expression editor wear: this writes the same language.
	{
		const wxBitmap picture = wxArtProvider::GetBitmap(wxART_QUERY_CONSTRUCTOR, wxART_FRONTEND,
			FromDIP(wxSize(16, 16)));
		if (picture.IsOk()) {
			wxIcon icon;
			icon.CopyFromBitmap(picture);
			SetIcon(icon);
		}
	}

	// READ THE EXISTING ONE. Anything that is not a CASE opens the window empty rather than being
	// refused: "turn this into a CASE" is a reasonable thing to mean, and the expression handed in
	// is left untouched until OK.
	if (existing && existing->m_kind == ibQueryAstExprKind::Case) {
		for (const auto& branch : existing->m_cases) {
			Branch row;
			if (branch.first)  row.m_when = ibRenderQueryExpr(*branch.first);
			if (branch.second) row.m_then = ibRenderQueryExpr(*branch.second);
			m_branches.push_back(std::move(row));
		}
		if (existing->m_else)
			m_otherwise = ibRenderQueryExpr(*existing->m_else);
	}

	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

	outer->Add(new wxStaticText(this, wxID_ANY,
		// ⚠ ASCII ONLY in this literal — the file is UTF-8 with no BOM and MSVC reads it in the
		// system codepage. The keywords themselves come from the ACTIVE table, so a localized
		// language shows its own words here.
		_("The branches are tried IN ORDER: the first condition that holds decides the result. "
		  "Use the arrows to change which is tried first.")),
		0, wxALL, FromDIP(6));

	// ---- the verbs, on a toolbar-shaped row of buttons -------------------------------------
	wxBoxSizer* commands = new wxBoxSizer(wxHORIZONTAL);
	auto command = [this, commands](const wxString& label, void (ibDialogQueryCase::*handler)(wxCommandEvent&)) {
		wxButton* button = new wxButton(this, wxID_ANY, label);
		button->Bind(wxEVT_BUTTON, handler, this);
		button->Enable(!m_readOnly);
		commands->Add(button, 0, wxRIGHT, FromDIP(4));
		return button;
	};
	command(_("Add"),    &ibDialogQueryCase::OnAdd);
	command(_("Delete"), &ibDialogQueryCase::OnRemove);
	{
		wxButton* up = new wxButton(this, wxID_ANY, _("Move up"));
		up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveBranch(-1); });
		up->Enable(!m_readOnly);
		commands->Add(up, 0, wxRIGHT, FromDIP(4));

		wxButton* down = new wxButton(this, wxID_ANY, _("Move down"));
		down->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveBranch(+1); });
		down->Enable(!m_readOnly);
		commands->Add(down, 0, wxRIGHT, FromDIP(4));
	}
	outer->Add(commands, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	// ---- the branches ----------------------------------------------------------------------
	m_model = new ibQueryGridModel();
	m_model->SetReader([this](unsigned int row, unsigned int col) -> wxString {
		if (row >= m_branches.size())
			return wxEmptyString;
		return col == kCaseColWhen ? m_branches[row].m_when : m_branches[row].m_then;
	});
	// ⚠ TYPED INTO WHERE IT STANDS, and read by the ENGINE on the way out. The cell keeps TEXT while
	// it is being edited — a half-written condition is a normal state of a cell and must not be a
	// parse error the moment a key is pressed. OK is where the parser has its say.
	m_model->SetWriter([this](unsigned int row, unsigned int col, const wxString& text) -> bool {
		if (m_readOnly || row >= m_branches.size())
			return false;
		if (col == kCaseColWhen) m_branches[row].m_when = text;
		else                     m_branches[row].m_then = text;
		return true;
	});

	m_grid = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_model->SetOnChanged([this] { ShowBranches(); });
	m_grid->AssociateModel(m_model);
	m_grid->AppendColumn(new ibDataViewColumn(Kw(ibQueryKeyword::When),
		new ibDataViewTextRenderer(ibDataViewTextRenderer::GetDefaultType(),
			m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_EDITABLE),
		kCaseColWhen, FromDIP(360), wxAlignment::wxALIGN_LEFT));
	m_grid->AppendColumn(new ibDataViewColumn(Kw(ibQueryKeyword::Then),
		new ibDataViewTextRenderer(ibDataViewTextRenderer::GetDefaultType(),
			m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_EDITABLE),
		kCaseColThen, FromDIP(320), wxAlignment::wxALIGN_LEFT));

	// A DOUBLE-CLICK OPENS THE FULL EDITOR over the cell — the same rule the constructor's grids
	// follow: the cell is for a short edit, the editor is where an expression is written.
	m_grid->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& event) {
		if (m_readOnly)
			return;
		const long row = SelectedRow();
		if (row < 0)
			return;
		const bool isWhen = event.GetDataViewColumn() == nullptr
			|| event.GetDataViewColumn()->GetModelColumn() == kCaseColWhen;
		wxString& cell = isWhen ? m_branches[row].m_when : m_branches[row].m_then;
		if (EditCell(cell, isWhen ? Kw(ibQueryKeyword::When) : Kw(ibQueryKeyword::Then)))
			ShowBranches();
	});
	outer->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));

	// ---- the ELSE ---------------------------------------------------------------------------
	wxBoxSizer* elseRow = new wxBoxSizer(wxHORIZONTAL);
	elseRow->Add(new wxStaticText(this, wxID_ANY, Kw(ibQueryKeyword::Else)),
		0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	m_elseBox = new wxTextCtrl(this, wxID_ANY, m_otherwise);
	m_elseBox->Enable(!m_readOnly);
	elseRow->Add(m_elseBox, 1, wxALIGN_CENTER_VERTICAL);
	{
		wxButton* pick = new wxButton(this, wxID_ANY, wxT("..."), wxDefaultPosition, FromDIP(wxSize(32, 24)));
		pick->Enable(!m_readOnly);
		pick->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			wxString text = m_elseBox->GetValue();
			if (EditCell(text, Kw(ibQueryKeyword::Else)))
				m_elseBox->SetValue(text);
		});
		elseRow->Add(pick, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
	}
	// LEFT EMPTY MEANS NO `ELSE`, which is a different query from `ELSE NULL` — the language says so
	// and this window must not decide otherwise on the author's behalf.
	outer->Add(elseRow, 0, wxEXPAND | wxALL, FromDIP(6));
	outer->Add(new wxStaticText(this, wxID_ANY,
		_("Leave it empty for no ELSE branch: that is not the same as ELSE NULL.")),
		0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	wxStdDialogButtonSizer* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	outer->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	SetSizer(outer);
	Bind(wxEVT_BUTTON, &ibDialogQueryCase::OnOk, this, wxID_OK);

	ShowBranches();
}

long ibDialogQueryCase::SelectedRow() const
{
	if (m_grid == nullptr || m_model == nullptr)
		return -1;
	const ibDataViewItem item = m_grid->GetSelection();
	return item.IsOk() ? static_cast<long>(m_model->GetRow(item)) : -1;
}

void ibDialogQueryCase::ShowBranches()
{
	if (m_model != nullptr)
		m_model->SetRowCount(static_cast<unsigned int>(m_branches.size()));
}

bool ibDialogQueryCase::EditCell(wxString& text, const wxString& title)
{
	// THE SAME EDITOR, one level down. It carries the fields, the language palette and the engine's
	// own parser — there is one of it in this product, and a CASE branch is an expression like any
	// other.
	ibDialogQueryExpression editor(this, title, m_fields, nullptr, m_metaData, m_readOnly);
	editor.SetText(text);
	if (editor.ShowModal() != wxID_OK)
		return false;
	text = editor.GetText();
	return true;
}

void ibDialogQueryCase::OnAdd(wxCommandEvent&)
{
	if (m_readOnly)
		return;
	m_branches.push_back(Branch());
	ShowBranches();
	if (!m_branches.empty())
		m_grid->Select(m_model->GetItem(static_cast<unsigned int>(m_branches.size() - 1)));
}

void ibDialogQueryCase::OnRemove(wxCommandEvent&)
{
	const long row = SelectedRow();
	if (m_readOnly || row < 0 || static_cast<size_t>(row) >= m_branches.size())
		return;
	m_branches.erase(m_branches.begin() + row);
	ShowBranches();
}

void ibDialogQueryCase::MoveBranch(int delta)
{
	const long row = SelectedRow();
	if (m_readOnly || row < 0 || static_cast<size_t>(row) >= m_branches.size())
		return;
	const long target = row + delta;
	if (target < 0 || static_cast<size_t>(target) >= m_branches.size())
		return;
	std::swap(m_branches[row], m_branches[target]);
	ShowBranches();
	m_grid->Select(m_model->GetItem(static_cast<unsigned int>(target)));
}

ibQueryAstExprPtr ibDialogQueryCase::GetExpression() const
{
	if (m_branches.empty())
		return nullptr;   // a CASE with no WHEN is not a CASE

	auto node = ibQueryAstExpr::Make(ibQueryAstExprKind::Case);
	ibQueryParser parser;
	for (const Branch& branch : m_branches) {
		if (branch.m_when.IsEmpty() || branch.m_then.IsEmpty())
			continue;
		node->m_cases.emplace_back(parser.ParseExpression(branch.m_when),
		                           parser.ParseExpression(branch.m_then));
	}
	if (node->m_cases.empty())
		return nullptr;
	if (!m_otherwise.IsEmpty())
		node->m_else = parser.ParseExpression(m_otherwise);
	return node;
}

void ibDialogQueryCase::OnOk(wxCommandEvent& event)
{
	if (m_readOnly) {
		event.Skip();
		return;
	}

	m_otherwise = m_elseBox != nullptr ? m_elseBox->GetValue() : wxString();

	// ⚠ A BRANCH IS BOTH HALVES. `WHEN` with no `THEN` is not a shorter branch, it is an unfinished
	// one — and letting it through would render text the parser then refuses, with the complaint
	// arriving somewhere else entirely.
	for (size_t i = 0; i < m_branches.size(); ++i) {
		if (m_branches[i].m_when.IsEmpty() || m_branches[i].m_then.IsEmpty()) {
			wxMessageBox(wxString::Format(
				_("Branch %u is unfinished: it needs both a condition and a result."),
				static_cast<unsigned int>(i + 1)),
				GetTitle(), wxOK | wxICON_WARNING, this);
			return;
		}
	}

	// THE ENGINE READS EVERY CELL, and its words are what the author is shown. There is no second,
	// softer opinion here about what a valid expression is.
	try {
		if (GetExpression() == nullptr) {
			wxMessageBox(_("Add at least one branch: a choice with no condition is not a choice."),
				GetTitle(), wxOK | wxICON_WARNING, this);
			return;
		}
	}
	catch (const ibBackendException& error) {
		wxMessageBox(error.GetErrorDescription(), GetTitle(), wxOK | wxICON_ERROR, this);
		return;
	}

	event.Skip();
}

bool ibShowQueryCaseBuilder(wxWindow* parent, const std::vector<ibQueryConstructorField>& fields,
                            ibQueryAstExprPtr& expression, const ibMetaData* metaData, bool readOnly)
{
	ibDialogQueryCase dialog(parent, fields, expression, metaData, readOnly);
	if (dialog.ShowModal() != wxID_OK)
		return false;

	const ibQueryAstExprPtr built = dialog.GetExpression();
	if (!built)
		return false;
	expression = built;
	return true;
}
