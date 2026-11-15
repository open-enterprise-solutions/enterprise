#include "search.h"

#include <wx/sizer.h>

#include "protocol/protocol.h"
#include "frmclient/win/picture.h"                                          // ibProtocolPicture — the window's picture
#include "frmclient/backend/appData.h"                                      // activeMetaData — what a type is read by
#include "frmclient/backend/serialize/dataProtocol.h"                       // a value as the question carries it
#include "frmclient/backend/system/value/valueType.h"                       // ibValueTypeDescription::AdjustValue
#include "frmclient/backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject::ReadReferences
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/choice.h"
#include "deskgui/univ/ctrl/radioBox.h"
#include "deskgui/univ/ctrl/staticText.h"
#include "deskgui/univ/ctrl/textCtrl.h"

namespace ibSearch = ibProtocol::ibRequest::ibSearch;

namespace {

// How to look, in the window's order — By value first.
const ibSearch::ibHow s_hows[] = { ibSearch::ibHow::Value, ibSearch::ibHow::Begins, ibSearch::ibHow::Contains,
	ibSearch::ibHow::Exact };

// A type a text is looked for in — a text, one nobody declared, a reference (by the fields of it that hold text).
bool HoldsText(const ibTypeDescription& type)
{
	if (type.GetClsidCount() == 0 || type.ContainType(ibValueTypes::TYPE_STRING))
		return true;
	for (const ibClassID& clsid : type.GetClsidList()) {
		if (::IsReference(clsid))
			return true;
	}
	return false;
}

} // namespace

ibDialogSearch::ibDialogSearch(wxWindow* parent, const ibProtocolNode& request)
	: ibDialog(parent, wxID_ANY, _("Advanced search"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
	const wxBitmap picture = ibProtocolPicture(request, ibSearch::Picture);
	if (picture.IsOk()) {
		wxIcon icon;
		icon.CopyFromBitmap(picture);
		SetIcon(icon);
	}

	// Where — the columns, a settings window's fields, the one the cursor stands on chosen; which of their types are
	// references, a reference's empty value made from it (ibValueReferenceDataObject::EmptyRef).
	ibValueReferenceDataObject::ReadReferences(request.FindChild(ibSearch::References));
	m_fields = ibReadSettingsFields(request.FindChild(ibSearch::Fields));
	m_column = new ibChoice(this, wxID_ANY);
	const ibMetaID current = request.GetInt(ibSearch::Column);
	for (const ibSettingsPlainField& field : m_fields) {
		const int at = m_column->Append(field.m_presentation.IsEmpty() ? field.m_name : field.m_presentation);
		if (field.m_id == current)
			m_column->SetSelection(at);
	}
	if (m_column->GetSelection() == wxNOT_FOUND && !m_fields.empty())
		m_column->SetSelection(0);

	// What — what the current row holds in each column, by its place: the column picked takes its own (OnColumn), the
	// cursor's first. A server's type reads as its Presentation beside it (ibValue::FromNode).
	for (const ibProtocolNode& described : request.FindChild(ibSearch::Fields).Children()) {
		const ibProtocolNode written = described.FindChild(ibSearch::Value);
		ibDataNode node;
		if (written.IsNode())
			ibReadProtocolNode(written, node);
		m_rowValues.push_back(written.IsNode() ? ibValue::FromNode(node) : ibValue());
	}
	m_value = new ibTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(260, -1)));
	m_value->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibDialogSearch::OnSelect, this);
	m_value->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibDialogSearch::OnClear, this);

	wxFlexGridSizer* const fields = new wxFlexGridSizer(2, FromDIP(wxSize(8, 6)));
	fields->AddGrowableCol(1);
	fields->Add(new ibStaticText(this, wxID_ANY, _("Where to look:")), 0, wxALIGN_CENTER_VERTICAL);
	fields->Add(m_column, 1, wxEXPAND);
	fields->Add(new ibStaticText(this, wxID_ANY, _("What to look for:")), 0, wxALIGN_CENTER_VERTICAL);
	fields->Add(m_value, 1, wxEXPAND);

	// How — in the order of s_hows.
	const wxString hows[] = { _("By value"), _("From the start of the line"), _("Anywhere in the line"), _("Exact match") };
	m_how = new ibRadioBox(this, wxID_ANY, _("How to look"), wxDefaultPosition, wxDefaultSize, WXSIZEOF(hows), hows, 1,
		wxRA_SPECIFY_COLS);
	const auto how = static_cast<ibSearch::ibHow>(request.GetInt(ibSearch::How, static_cast<int>(ibSearch::ibHow::Contains)));
	for (unsigned int at = 0; at < WXSIZEOF(s_hows); ++at) {
		if (s_hows[at] == how)
			m_how->SetSelection(at);
	}
	m_column->Bind(wxEVT_CHOICE, &ibDialogSearch::OnColumn, this);
	m_how->Bind(wxEVT_RADIOBOX, &ibDialogSearch::OnHow, this);

	ibStdDialogButtonSizer* const buttons = new ibStdDialogButtonSizer();
	ibButton* const find = new ibButton(this, wxID_OK, _("Find"));
	find->SetDefault();
	buttons->AddButton(find);
	buttons->AddButton(new ibButton(this, wxID_CANCEL, _("Close")));
	buttons->Realize();

	wxBoxSizer* const main = new wxBoxSizer(wxVERTICAL);
	main->Add(fields, 0, wxEXPAND | wxALL, FromDIP(10));
	main->Add(m_how, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
	main->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(10));

	// The field as the way chosen draws it, and the ways the column is looked in.
	m_byValue = ByValue();
	ShowField();
	wxCommandEvent none;
	OnColumn(none);

	SetSizerAndFit(main);
	Centre(wxBOTH);
	m_value->SetFocus();
	m_value->SelectAll();
}

bool ibDialogSearch::SetControlValue(const ibValue& value)
{
	m_held = value;
	ShowField();
	return true;
}

bool ibDialogSearch::HasQuickChoice() const
{
	// As a settings window's value cell answers (ibFilterValueRenderer): a number and a date have one — the calculator,
	// the calendar — and a value that offers what it is picked among, a flag's or an enumeration's.
	const ibValueTypes vt = ibValue::GetVTByID(m_held.GetClassType());
	if (vt == ibValueTypes::TYPE_NUMBER || vt == ibValueTypes::TYPE_DATE)
		return true;
	std::vector<ibValue> offered;
	return m_held.FindValue(wxEmptyString, offered);
}

void ibDialogSearch::ChoiceProcessing(ibValue& selected)
{
	m_held = selected;
	ShowField();
}

const ibMetaData* ibDialogSearch::GetMetaData() const
{
	return activeMetaData;
}

void ibDialogSearch::OnColumn(wxCommandEvent& WXUNUSED(event))
{
	const int at = m_column->GetSelection();
	m_typeDesc = at != wxNOT_FOUND ? m_fields[at].m_type : ibTypeDescription();

	// What the current row holds in it — by its value, and as it reads for the other ways; none, nothing is looked for by
	// value until one is given.
	m_held = at != wxNOT_FOUND && static_cast<size_t>(at) < m_rowValues.size() ? m_rowValues[at] : ibValue();
	m_text = m_held.GetString().ToWxString();
	const bool text = HoldsText(m_typeDesc);
	for (unsigned int at = 1; at < WXSIZEOF(s_hows); ++at)
		m_how->Enable(at, text);
	if (!text && m_how->GetSelection() != 0) {
		m_how->SetSelection(0);
		m_byValue = true;
	}
	ShowField();
}

void ibDialogSearch::OnHow(wxCommandEvent& WXUNUSED(event))
{
	TakeField();
	m_byValue = ByValue();
	ShowField();
}

void ibDialogSearch::OnSelect(wxCommandEvent& WXUNUSED(event))
{
	TakeField();
	ibTypeControlFactory::ChooseValue(this, {}, m_value);
}

void ibDialogSearch::OnClear(wxCommandEvent& WXUNUSED(event))
{
	// The empty value of the column's type, as a settings window's «×» gives — an empty reference looks for the rows that
	// have none.
	m_held = ibValueTypeDescription::AdjustValue(GetTypeDesc(), ibValue(), GetMetaData());
	ShowField();
}

bool ibDialogSearch::ByValue() const
{
	const int at = m_how->GetSelection();
	return at != wxNOT_FOUND && s_hows[at] == ibSearch::ibHow::Value;
}

void ibDialogSearch::TakeField()
{
	const wxString typed = m_value->GetValue();
	if (!m_byValue) {
		m_text = typed;
		return;
	}
	// The field shows the value held — nothing typed over it, nothing to read.
	if (typed == m_held.GetString().ToWxString())
		return;
	if (typed.IsEmpty()) {
		m_held = ibValue();
		return;
	}
	// A number, a date, a text typed — read as the column's type; a reference is chosen, not typed.
	const ibValueTypes vt = ibValue::GetVTByID(GetTypeDesc().GetFirstClsid());
	if (vt == ibValueTypes::TYPE_NUMBER || vt == ibValueTypes::TYPE_DATE || vt == ibValueTypes::TYPE_STRING
		|| vt == ibValueTypes::TYPE_BOOLEAN)
		m_held = ibValueTypeDescription::AdjustValue(GetTypeDesc(), ibValue(typed), GetMetaData());
}

void ibDialogSearch::ShowField()
{
	m_value->ShowSelectButton(m_byValue);
	m_value->ShowClearButton(m_byValue);
	m_value->ChangeValue(m_byValue ? m_held.GetString().ToWxString() : m_text);
}

void ibDialogSearch::Show(wxWindow* parent, const ibProtocolNode& request, ibProtocolNode& response)
{
	ibDialogSearch dialog(parent, request);
	if (dialog.ShowModal() != wxID_OK || dialog.m_column->GetSelection() == wxNOT_FOUND)
		return;
	dialog.TakeField();
	response.SetValue(ibSearch::Column, static_cast<long long>(dialog.m_fields[dialog.m_column->GetSelection()].m_id));
	const ibSearch::ibHow how = s_hows[dialog.m_how->GetSelection()];
	response.SetValue(ibSearch::How, static_cast<long long>(how));
	if (how == ibSearch::ibHow::Value) {
		ibDataNode node;
		dialog.m_held.Serialize(node);
		ibWriteProtocolNode(node, response.Child(ibSearch::Value));
	}
	else
		response.SetValue(ibSearch::Value, ibString(dialog.m_text));
}
