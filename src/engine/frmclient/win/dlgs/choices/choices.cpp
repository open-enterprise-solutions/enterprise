#include "choices.h"

#include "core/formatString.h"   // ibFormatString::FromTypeDesc — a value shown as the field's type writes it
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibBackendProperty — the row the window is over
#include "frmclient/backend/backend_localization.h"             // ibTranslateString — a presentation, every language
#include "frmclient/win/dlgs/queryConstructor/queryGridModel.h"   // the one grid model for a plain list
#include "frmclient/win/dlgs/rowValueCell.h"                      // the Value cell of a typed value: a report parameter's
#include "deskgui/univ/dataview/dataviewEditOnActivate.h"         // …and the one way a cell opens on a double-click
#include "deskgui/univ/dataview/dvrenderers.h"                    // the Value cell of a finite type: picked
#include "protocol/protocolDesigner.h"                            // ibProtocol::ibSchema::ibProperties — the row's keys

#include <algorithm>
#include <wx/sizer.h>
#include <wx/artprov.h>
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/bmpButton.h"
#include "deskgui/univ/ctrl/staticLine.h"
#include "deskgui/univ/messageDialog.h"   // the refusal OK gives when a row has no value

namespace {

// The classes a value is typed in — a string, a number, a date; the others are picked.
const ibClassID kTypedClasses[] = { g_valueNumberCLSID, g_valueDateCLSID, g_valueStringCLSID };

}

ibDialogChoices::ibDialogChoices(wxWindow* parent, ibBackendProperty* property, const ibChoicesDescription& choices)
	: ibDialog(parent, wxID_ANY, _("Choices"), wxDefaultPosition, wxSize(560, 360), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_choices(choices)
{
	// WHAT THE ROW BROUGHT — the field's type, and the values of its finite classes (ibPropertyChoices::GetProtocolValue);
	// the application the type is read against is the field's.
	ibDataNode row;
	if (property != nullptr) {
		property->GetProtocolValue(row);
		const ibPropertyObject* const owner = property->GetPropertyObject();   // CONST: the non-const GetMetaData wxFAILs
		m_metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	}
	if (const auto type = row.FindProperty(ibProtocol::ibSchema::ibProperties::Type))
		ibTypeDescriptionMemory::ReadNode(*type, m_type);
	if (const auto values = row.FindChild(ibProtocol::ibSchema::ibProperties::Values)) {
		for (const ibDataNode& item : values->Children()) {
			const auto value = item.FindChild(ibProtocol::ibSchema::ibProperties::Value);
			if (!value)
				continue;
			m_offered.push_back({ *value, item.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Presentation) });
			m_offeredWords.Add(m_offered.back().m_label);
		}
	}

	// The type's own cell where the type has a class a value is typed in, or names none (its "…" asks the type first, as
	// a composite one's does); a type of finite classes alone is picked from what they offer.
	bool typed = !m_type.IsOk();
	for (const ibClassID& clsid : kTypedClasses)
		typed = typed || std::find(m_type.GetClsidList().begin(), m_type.GetClsidList().end(), clsid) != m_type.GetClsidList().end();

	wxBoxSizer* const outer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* const rowsAndButtons = new wxBoxSizer(wxHORIZONTAL);

	m_rows = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	m_rowsModel = new ibQueryGridModel();
	m_rowsModel->SetReader([this](unsigned int row, unsigned int col) { return RowCellText(row, col); });
	m_rowsModel->SetWriter([this](unsigned int row, unsigned int col, const wxString& text) {
		return SetRowCellText(row, col, text);
	});
	m_rows->AssociateModel(m_rowsModel);
	// ⭐ A TYPED VALUE IS EDITED IN THE TYPE'S OWN CELL — a report parameter's Value (ibRowValueCellRenderer): a string
	// within its length, a number in the mask of its digits and its scale (Number(15,2) takes no third digit after the
	// point), a date in its fractions' with the calendar on "…", each shaped by the type as it is typed (Max, 2026-11-24:
	// "not a string — the type, a number with its limit after the point"). The cell works in values, a row in nodes.
	ibDataViewRenderer* valueCell = nullptr;
	if (typed) {
		ibRowValueCellRenderer* const cell = new ibRowValueCellRenderer(this, ibRowValueCellRenderer::FieldChooser(),
			[this](const ibDataViewItem& item) -> ibValue {
				const unsigned int line = m_rowsModel->GetRow(item);
				return line < m_choices.m_rows.size() && !(m_choices.m_rows[line].m_value == ibDataNode())
					? ibValue::FromNode(m_choices.m_rows[line].m_value) : ibValue();
			},
			[this](const ibDataViewItem& item, const ibValue& value) {
				const unsigned int line = m_rowsModel->GetRow(item);
				ibDataNode node;
				if (line < m_choices.m_rows.size() && value.Serialize(node))
					m_choices.m_rows[line].m_value = node;
			});
		cell->SetTypeSource([this](const ibDataViewItem&) { return m_type; }, [this]() { return m_metaData; });
		valueCell = cell;
	}
	else {
		valueCell = new ibDataViewChoiceRenderer(m_offeredWords, wxDATAVIEW_CELL_EDITABLE);
	}
	m_rows->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Value"), valueCell, kGridCol1, FromDIP(200),
		wxAlignment::wxALIGN_LEFT));
	m_rows->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Presentation"),
		new ibDataViewTextRenderer(ibDataViewTextRenderer::GetDefaultType(), wxDATAVIEW_CELL_EDITABLE),
		kGridCol2, FromDIP(220), wxAlignment::wxALIGN_LEFT));
	rowsAndButtons->Add(m_rows, wxSizerFlags(1).Expand());

	wxBoxSizer* const buttons = new wxBoxSizer(wxVERTICAL);
	ibButton* const add = new ibButton(this, wxID_ADD, _("Add"));
	ibButton* const remove = new ibButton(this, wxID_REMOVE, _("Remove"));
	m_fill = new ibButton(this, wxID_ANY, _("Fill"));
	m_fill->SetToolTip(_("Add every value of the field's type the switch does not offer yet."));
	m_up = new ibBitmapButton(this, wxID_UP, wxArtProvider::GetBitmapBundle(wxART_GO_UP, wxART_BUTTON));
	m_down = new ibBitmapButton(this, wxID_DOWN, wxArtProvider::GetBitmapBundle(wxART_GO_DOWN, wxART_BUTTON));
	buttons->Add(add, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(4)));
	buttons->Add(remove, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(4)));
	buttons->Add(m_fill, wxSizerFlags().Expand().Border(wxBOTTOM, FromDIP(12)));
	buttons->Add(m_up, wxSizerFlags().Border(wxBOTTOM, FromDIP(4)));
	buttons->Add(m_down, wxSizerFlags());
	m_fill->Show(!m_offered.empty());
	rowsAndButtons->Add(buttons, wxSizerFlags().Border(wxLEFT, FromDIP(6)));

	outer->Add(rowsAndButtons, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(6)));
	outer->Add(new ibStaticLine(this), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, FromDIP(6)));
	outer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));
	SetSizer(outer);

	add->Bind(wxEVT_BUTTON, &ibDialogChoices::OnAdd, this);
	remove->Bind(wxEVT_BUTTON, &ibDialogChoices::OnRemove, this);
	m_fill->Bind(wxEVT_BUTTON, &ibDialogChoices::OnFill, this);
	m_up->Bind(wxEVT_BUTTON, &ibDialogChoices::OnMoveUp, this);
	m_down->Bind(wxEVT_BUTTON, &ibDialogChoices::OnMoveDown, this);
	m_rows->Bind(ibEVT_DATAVIEW_SELECTION_CHANGED, [this](ibDataViewEvent&) { ShowSelection(); });
	ibDataViewEditOnActivate(m_rows);

	FillRows();
	ShowSelection();
	CentreOnParent();
}

wxString ibDialogChoices::RowCellText(unsigned int row, unsigned int col) const
{
	if (row >= m_choices.m_rows.size())
		return wxString();
	const ibChoiceDescription& item = m_choices.m_rows[row];
	if (col == kGridCol1)
		return ValueText(item.m_value);
	if (col == kGridCol2)
		return ibTranslateString(item.m_presentation).GetString();
	return wxString();
}

bool ibDialogChoices::SetRowCellText(unsigned int row, unsigned int col, const wxString& text)
{
	if (row >= m_choices.m_rows.size())
		return false;
	ibChoiceDescription& item = m_choices.m_rows[row];

	if (col == kGridCol1)
		return ReadValue(text, item.m_value);

	if (col == kGridCol2) {
		// The session's language's text, laid into the others; emptied — that language's taken out, and none left is
		// the value's own presentation.
		ibTranslateString presentation(item.m_presentation);
		if (text.IsEmpty())
			presentation.RemoveTranslate(ibBackendLocalization::GetUserLanguage());
		else
			presentation.SetTranslate(ibString(text));
		item.m_presentation = presentation.GetRawText();
		return true;
	}
	return false;
}

wxString ibDialogChoices::ValueText(const ibDataNode& value) const
{
	for (const ibOffered& offered : m_offered) {
		if (offered.m_value == value)
			return offered.m_label;
	}
	if (value == ibDataNode())
		return wxString();

	// …as the field's type writes it — a number with its scale, `5.00` of Number(15,2); a date with its fractions.
	const ibValue held = ibValue::FromNode(value);
	ibFormatString format;
	return ibFormatString::FromTypeDesc(m_type, format) ? format.Apply(held).ToWxString() : held.GetString().ToWxString();
}

bool ibDialogChoices::ReadValue(const wxString& text, ibDataNode& value) const
{
	for (const ibOffered& offered : m_offered) {
		if (offered.m_label == text) {
			value = offered.m_value;
			return true;
		}
	}
	return false;
}

void ibDialogChoices::FillRows()
{
	m_rowsModel->SetRowCount(static_cast<unsigned int>(m_choices.m_rows.size()));
}

long ibDialogChoices::SelectedLine() const
{
	const ibDataViewItem item = m_rows->GetSelection();
	return item.IsOk() ? static_cast<long>(m_rowsModel->GetRow(item)) : wxNOT_FOUND;
}

void ibDialogChoices::SelectLine(long line)
{
	if (line < 0 || static_cast<size_t>(line) >= m_choices.m_rows.size())
		return;
	const ibDataViewItem item = m_rowsModel->GetItem(static_cast<unsigned int>(line));
	m_rows->Select(item);
	m_rows->EnsureVisible(item);
}

void ibDialogChoices::ShowSelection()
{
	const long line = SelectedLine();
	m_up->Enable(line > 0);
	m_down->Enable(line >= 0 && static_cast<size_t>(line) + 1 < m_choices.m_rows.size());
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

// A row with no value yet, its Value cell opened — the value is the question it is added to answer.
void ibDialogChoices::OnAdd(wxCommandEvent&)
{
	m_choices.m_rows.push_back(ibChoiceDescription());
	FillRows();
	const long line = static_cast<long>(m_choices.m_rows.size()) - 1;
	SelectLine(line);
	ShowSelection();
	if (ibDataViewColumn* const column = m_rows->GetColumn(0)) {
		const ibDataViewItem item = m_rowsModel->GetItem(static_cast<unsigned int>(line));
		m_rows->CallAfter([this, item, column] { m_rows->EditItem(item, column); });
	}
}

void ibDialogChoices::OnRemove(wxCommandEvent&)
{
	const long line = SelectedLine();
	if (line < 0 || static_cast<size_t>(line) >= m_choices.m_rows.size())
		return;
	m_choices.m_rows.erase(m_choices.m_rows.begin() + line);
	FillRows();
	SelectLine(line < static_cast<long>(m_choices.m_rows.size()) ? line : static_cast<long>(m_choices.m_rows.size()) - 1);
	ShowSelection();
}

// Up and down are one act with a sign: the row swaps with the one it passes, and the selection travels with it.
void ibDialogChoices::OnMoveUp(wxCommandEvent&)
{
	const long line = SelectedLine();
	if (line <= 0)
		return;
	std::swap(m_choices.m_rows[line], m_choices.m_rows[line - 1]);
	FillRows();
	SelectLine(line - 1);
	ShowSelection();
}

void ibDialogChoices::OnMoveDown(wxCommandEvent&)
{
	const long line = SelectedLine();
	if (line < 0 || static_cast<size_t>(line) + 1 >= m_choices.m_rows.size())
		return;
	std::swap(m_choices.m_rows[line], m_choices.m_rows[line + 1]);
	FillRows();
	SelectLine(line + 1);
	ShowSelection();
}

// Every value the type offers that no row writes yet, after the rows there are, in the server's order — with no text of
// its own: the switch shows the value's.
void ibDialogChoices::OnFill(wxCommandEvent&)
{
	for (const ibOffered& offered : m_offered) {
		const bool listed = std::any_of(m_choices.m_rows.begin(), m_choices.m_rows.end(),
			[&offered](const ibChoiceDescription& item) { return item.m_value == offered.m_value; });
		if (!listed)
			m_choices.m_rows.push_back(ibChoiceDescription{ offered.m_value, ibString() });
	}
	FillRows();
	ShowSelection();
}

// ⭐ A ROW WITHOUT A VALUE IS NOT AN ITEM, and OK says so rather than storing it: the switch would show a button that
// writes nothing. The row is selected and its Value cell opened, so the refusal ends where the answer goes.
bool ibDialogChoices::Validate()
{
	for (size_t idx = 0; idx < m_choices.m_rows.size(); idx++) {
		if (!(m_choices.m_rows[idx].m_value == ibDataNode()))
			continue;
		ibMessageBox(ibString::Format(_("Item %d has no value. Give it one in the Value column, or remove it."),
			static_cast<int>(idx + 1)), _("Choices"), wxOK | wxICON_EXCLAMATION, this);
		SelectLine(static_cast<long>(idx));
		ShowSelection();
		if (ibDataViewColumn* const column = m_rows->GetColumn(0))
			m_rows->EditItem(m_rowsModel->GetItem(static_cast<unsigned int>(idx)), column);
		return false;
	}
	return true;
}

// THE BUFFER IS WRITTEN BACK ONLY ON OK, and only changed — an OK that changed nothing does not mark the form modified.
bool ibDialogChoices::ShowChoicesDialog(ibBackendProperty* property, ibChoicesDescription& choices)
{
	if (property == nullptr)
		return false;

	ibDialogChoices dialog(nullptr, property, choices);
	if (dialog.ShowModal() != wxID_OK || dialog.GetChoices() == choices)
		return false;

	choices = dialog.GetChoices();
	return true;
}
