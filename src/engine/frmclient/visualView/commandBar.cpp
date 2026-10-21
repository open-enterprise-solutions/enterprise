////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a command bar of a view
////////////////////////////////////////////////////////////////////////////

#include "commandBar.h"

#include <functional>

#include "protocol/protocol.h"
#include "frmclient/win/picture.h"
#include "frmclient/win/ctrls/toolBar.h"
#include "frmclient/win/theme/luna_toolbarart.h"
#include "frmclient/visualView/visualHostClient.h"   // ibVisualHostClient::IsDesignerHost — the bar's real width in the editor

namespace {

// The ids the tools take — the entry's place after the first.
constexpr int kFirstToolId = wxID_HIGHEST + 1;

// The ids of a group's menu — a menu of its own: they need only differ inside it.
constexpr int kFirstMemberId = wxID_HIGHEST + 1;

constexpr long long kSeparatorId = -1;

// The entries as a list of what they are made of — the ids, captions, pictures, kinds, a group's commands — less
// whether they are enabled: two bars of one shape differ only in what may be pressed, and that is set, not built.
ibString ShapeOf(const ibProtocolNode& commandBar)
{
	ibString shape;
	for (const ibProtocolNode& entry : commandBar.Children()) {
		shape << entry.GetInt(ibProtocol::ibForm::ibCommandBar::Id) << wxT('|')
			<< entry.GetString(ibProtocol::ibForm::ibCommandBar::Caption) << wxT('|')
			<< static_cast<unsigned long long>(std::hash<ibString>()(entry.GetString(ibProtocol::ibForm::ibCommandBar::Picture)))
			<< wxT('|') << entry.GetInt(ibProtocol::ibForm::ibCommandBar::Kind) << wxT('|')
			<< static_cast<unsigned long long>(entry.Children().size()) << wxT(';');
	}
	return shape;
}

} // namespace

//***********************************************************************************
//*                                  commandBar                                     *
//***********************************************************************************

ibViewCommandBar::ibViewCommandBar(wxWindow* parent, const ibVisualHostClient& host, ibSendCommand sendCommand)
	: m_sendCommand(std::move(sendCommand))
{
	m_bar = new ibAuiToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxAUI_TB_NO_AUTORESIZE | wxAUI_TB_HORZ_TEXT | wxAUI_TB_OVERFLOW);
	m_bar->SetArtProvider(new wxAuiLunaToolBarArt());
	m_bar->SetDesignerMode(host.IsDesignerHost());   // show real width in the editor (no overflow collapse)

	// Hidden until it has a command: an empty strip takes no room.
	m_bar->Hide();

	m_bar->Bind(wxEVT_TOOL, &ibViewCommandBar::OnTool, this);
	m_bar->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &ibViewCommandBar::OnToolDropDown, this);
}

wxWindow* ibViewCommandBar::GetWindow() const
{
	return m_bar;
}

ibProtocolNode ibViewCommandBar::FindEntryByPosition(const wxPoint& point) const
{
	const wxAuiToolBarItem* const tool = m_bar->FindToolByPosition(point.x, point.y);
	const std::vector<ibProtocolNode> entries = m_entries.Children();
	const std::size_t idx = tool != nullptr ? static_cast<std::size_t>(tool->GetId() - kFirstToolId) : entries.size();
	return idx < entries.size() ? entries[idx] : ibProtocolNode();
}

void ibViewCommandBar::Update(const ibProtocolNode& commandBar)
{
	const std::vector<ibProtocolNode> entries = commandBar.Children();

	// One of the same shape — only what may be pressed is set.
	const ibString shape = ShapeOf(commandBar);
	if (shape == m_shape) {
		for (std::size_t idx = 0; idx < entries.size(); ++idx) {
			if (entries[idx].GetInt(ibProtocol::ibForm::ibCommandBar::Id) != kSeparatorId)
				m_bar->EnableTool(kFirstToolId + static_cast<int>(idx), entries[idx].GetBool(ibProtocol::ibForm::ibCommandBar::Enabled, true));
		}
		m_entries = commandBar.IsNode() ? commandBar.Clone() : ibProtocolNode();
		m_bar->Refresh();
		return;
	}

	// Built anew — the desktop's FillCommandBarToolBar: a separator only after a tool, a group with the arrow beside
	// it, a command not enabled greyed but there.
	m_shape = shape;
	m_entries = commandBar.IsNode() ? commandBar.Clone() : ibProtocolNode();
	// The pictures of the bar and of its groups' members asked for at once — not a call per tool.
	std::vector<wxString> pictures;
	for (const ibProtocolNode& entry : entries) {
		pictures.push_back(entry.GetString(ibProtocol::ibForm::ibCommandBar::Picture));
		for (const ibProtocolNode& member : entry.Children())
			pictures.push_back(member.GetString(ibProtocol::ibForm::ibCommandBar::Picture));
	}
	ibProtocolPictureReceive(pictures);
	m_bar->ClearTools();

	bool anyTool = false;
	for (std::size_t idx = 0; idx < entries.size(); ++idx) {
		const ibProtocolNode& entry = entries[idx];
		if (entry.GetInt(ibProtocol::ibForm::ibCommandBar::Id) == kSeparatorId) {
			if (anyTool)
				m_bar->AddSeparator();
			continue;
		}

		// The server has said what is shown: a caption it left empty is a picture alone, a picture it left empty is a
		// caption alone.
		const int toolId = kFirstToolId + static_cast<int>(idx);
		const wxString caption = entry.GetString(ibProtocol::ibForm::ibCommandBar::Caption);
		m_bar->AddTool(toolId, caption, ibProtocolPicture(entry.GetString(ibProtocol::ibForm::ibCommandBar::Picture)), wxNullBitmap,
			wxITEM_NORMAL, entry.GetString(ibProtocol::ibForm::ibCommandBar::Tooltip, caption), wxEmptyString, nullptr);
		if (entry.GetInt(ibProtocol::ibForm::ibCommandBar::Kind) == static_cast<int>(ibProtocol::ibForm::ibCommandBar::ibKind::Group))
			m_bar->SetToolDropDown(toolId, true);
		if (!entry.GetBool(ibProtocol::ibForm::ibCommandBar::Enabled, true))
			m_bar->EnableTool(toolId, false);
		anyTool = true;
	}
	m_bar->Realize();

	if (m_bar->IsShown() != anyTool) {
		m_bar->Show(anyTool);
		if (wxWindow* const parent = m_bar->GetParent())
			parent->Layout();
	}
}

bool ibViewCommandBar::PopupGroup(int toolId)
{
	const std::vector<ibProtocolNode> entries = m_entries.Children();
	const std::size_t idx = static_cast<std::size_t>(toolId - kFirstToolId);
	if (idx >= entries.size() || entries[idx].GetInt(ibProtocol::ibForm::ibCommandBar::Kind) != static_cast<int>(ibProtocol::ibForm::ibCommandBar::ibKind::Group))
		return false;

	// Each command in its place among the group's — a command gone since is there, not enabled, so the places are
	// the places the server counts.
	const ibProtocolNode group = entries[idx];
	const std::vector<ibProtocolNode> members = group.Children();
	wxMenu menu;
	for (std::size_t member = 0; member < members.size(); ++member) {
		wxMenuItem* const item = menu.Append(kFirstMemberId + static_cast<int>(member), members[member].GetString(ibProtocol::ibForm::ibCommandBar::Caption));
		const wxBitmap picture = ibProtocolPicture(members[member].GetString(ibProtocol::ibForm::ibCommandBar::Picture));
		if (picture.IsOk())
			item->SetBitmap(picture);
		item->Enable(members[member].GetBool(ibProtocol::ibForm::ibCommandBar::Enabled, true));
	}
	if (members.empty())
		return true;

	const int chosen = m_bar->GetPopupMenuSelectionFromUser(menu, m_bar->GetToolRect(toolId).GetBottomLeft());
	if (chosen == wxID_NONE)
		return true;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Id, group.GetInt(ibProtocol::ibForm::ibCommandBar::Id))
		.SetValue(ibProtocol::ibEvent::Member, static_cast<long long>(chosen - kFirstMemberId));
	m_sendCommand(args);
	return true;
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibViewCommandBar::OnTool(wxCommandEvent& event)
{
	// The focus onto the bar first: the field being typed into commits before the command runs (the desktop's
	// reason — a Write would read what was there before).
	m_bar->SetFocus();

	if (PopupGroup(event.GetId()))
		return;

	const std::vector<ibProtocolNode> entries = m_entries.Children();
	const std::size_t idx = static_cast<std::size_t>(event.GetId() - kFirstToolId);
	if (idx >= entries.size())
		return;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Id, entries[idx].GetInt(ibProtocol::ibForm::ibCommandBar::Id));
	m_sendCommand(args);
}

void ibViewCommandBar::OnToolDropDown(wxAuiToolBarEvent& event)
{
	// The arrow alone — a click on the tool's body comes as wxEVT_TOOL a moment later and opens the group there.
	if (!event.IsDropDownClicked()) {
		event.Skip();
		return;
	}
	m_bar->SetFocus();
	if (!PopupGroup(event.GetId()))
		event.Skip();
}
