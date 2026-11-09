#include "webAddress.h"

#include <wx/button.h>
#include <wx/choicdlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibWebAddress

namespace {

namespace ibWeb = ibProtocol::ibSchema::ibWebAddress;

// The hidden column that holds an entry's id.
constexpr int kIdColumn = 4;

// One of the schema's commands. False — refused, and the person told why (the frame's Call says it).
bool WebAddressCommand(ibProtocol::ibSchema::ibCommand command, const ibProtocolNode& args)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;
	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::WebAddress));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(command));
	params.SetValue(ibProtocol::ibSchema::Args, args);
	return frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

} // namespace

ibDialogWebAddress::ibDialogWebAddress(wxWindow* parent, const ibProtocolNode& shown) :
	wxDialog(parent, wxID_ANY, _("Web addresses"), wxDefaultPosition, wxSize(720, 360),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxBoxSizer* const mainSizer = new wxBoxSizer(wxVERTICAL);

	m_table = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_table->AppendColumn(_("Path"), wxLIST_FORMAT_LEFT, FromDIP(220));
	m_table->AppendColumn(_("Kind"), wxLIST_FORMAT_LEFT, FromDIP(120));
	m_table->AppendColumn(_("Object"), wxLIST_FORMAT_LEFT, FromDIP(220));
	m_table->AppendColumn(_("Kept"), wxLIST_FORMAT_LEFT, FromDIP(100));
	m_table->AppendColumn(wxEmptyString, wxLIST_FORMAT_LEFT, 0);   // the id
	mainSizer->Add(m_table, 1, wxALL | wxEXPAND, FromDIP(5));

	wxBoxSizer* const buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* const add = new wxButton(this, wxID_ADD, _("Add..."));
	wxButton* const remove = new wxButton(this, wxID_REMOVE, _("Remove"));
	buttons->Add(add, 0, wxRIGHT, FromDIP(5));
	buttons->Add(remove, 0, wxRIGHT, FromDIP(5));
	buttons->AddStretchSpacer();
	buttons->Add(new wxButton(this, wxID_CANCEL, _("Close")), 0);
	mainSizer->Add(buttons, 0, wxALL | wxEXPAND, FromDIP(5));

	SetSizer(mainSizer);
	Layout();
	Centre(wxBOTH);

	add->Bind(wxEVT_BUTTON, &ibDialogWebAddress::OnAdd, this);
	remove->Bind(wxEVT_BUTTON, &ibDialogWebAddress::OnRemove, this);

	Fill(shown);
}

void ibDialogWebAddress::Fill(const ibProtocolNode& shown)
{
	m_shown = shown;
	m_table->DeleteAllItems();
	for (const ibProtocolNode& row : shown.FindChild(ibWeb::Addresses).Children()) {
		const long index = m_table->InsertItem(m_table->GetItemCount(), row.GetString(ibWeb::Path));
		m_table->SetItem(index, 1, row.GetString(ibWeb::Kind));
		m_table->SetItem(index, 2, row.GetString(ibWeb::Title));
		m_table->SetItem(index, 3, row.GetBool(ibWeb::Kept) ? _("In the base") : _("A session's"));
		m_table->SetItem(index, kIdColumn, row.GetString(ibWeb::Id));
	}
}

void ibDialogWebAddress::Reload()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode params, shown;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::WebAddress));
	if (frame != nullptr && frame->Call(ibProtocol::ibMethod::Schema, params, shown))
		Fill(shown);
}

void ibDialogWebAddress::OnAdd(wxCommandEvent&)
{
	// WHAT THE KINDS OFFER — each as the configuration declares it — and, last, a path of a kind the person names.
	const std::vector<ibProtocolNode> offers = m_shown.FindChild(ibWeb::Offers).Children();
	const std::vector<ibProtocolNode> kinds = m_shown.FindChild(ibWeb::Kinds).Children();
	if (offers.empty() && kinds.empty()) {
		wxMessageBox(_("No kind of web address is known to this platform."), GetTitle(), wxOK | wxICON_INFORMATION, this);
		return;
	}
	wxArrayString choices;
	for (const ibProtocolNode& offer : offers) {
		const wxString title = offer.GetString(ibWeb::Title);
		choices.Add(title.IsEmpty() ? wxString(offer.GetString(ibWeb::Path))
			: wxString::Format(wxT("%s - %s"), offer.GetString(ibWeb::Path), title));
	}
	if (!kinds.empty())
		choices.Add(_("Another path..."));
	const int picked = choices.size() == 1 ? 0 : wxGetSingleChoiceIndex(_("What the base is to answer:"), _("Add a web address"), choices, this);
	if (picked < 0)
		return;

	ibProtocolNode args;
	if (static_cast<std::size_t>(picked) < offers.size()) {
		const ibProtocolNode& offer = offers[picked];
		args.SetValue(ibWeb::Kind, offer.GetString(ibWeb::Kind));
		args.SetValue(ibWeb::Path, offer.GetString(ibWeb::Path));
		if (offer.Has(ibWeb::Object))
			args.SetValue(ibWeb::Object, offer.GetString(ibWeb::Object));
	}
	else {
		wxArrayString names;
		for (const ibProtocolNode& kind : kinds)
			names.Add(kind.GetString(ibWeb::Name));
		const int kind = names.size() == 1 ? 0 : wxGetSingleChoiceIndex(_("Which kind answers there:"), _("Add a web address"), names, this);
		if (kind < 0)
			return;
		const wxString path = wxGetTextFromUser(
			_("The path under every publication of the base, without a slash at either end (es/desk):"),
			_("Add a web address"), wxEmptyString, this).Strip(wxString::both);
		if (path.IsEmpty())
			return;
		args.SetValue(ibWeb::Kind, ibString(names[kind]));
		args.SetValue(ibWeb::Path, ibString(path));
	}
	if (WebAddressCommand(ibProtocol::ibSchema::ibCommand::Create, args))
		Reload();
}

void ibDialogWebAddress::OnRemove(wxCommandEvent&)
{
	const long row = m_table->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (row < 0)
		return;
	const wxString path = m_table->GetItemText(row, 0);
	if (wxMessageBox(ibString::Format(_("Take the web address '%s' off?"), path), GetTitle(),
		wxYES_NO | wxICON_QUESTION, this) != wxYES)
		return;
	ibProtocolNode args;
	args.SetValue(ibWeb::Id, ibString(m_table->GetItemText(row, kIdColumn)));
	if (WebAddressCommand(ibProtocol::ibSchema::ibCommand::Remove, args))
		Reload();
}
