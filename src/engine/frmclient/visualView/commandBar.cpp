////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a command bar of a view
////////////////////////////////////////////////////////////////////////////

#include "commandBar.h"

#include <functional>
#include <string_view>

#include "protocol/protocol.h"
#include "frmclient/win/picture.h"
#include "deskgui/univ/aui/auibar.h"
#include "deskgui/univ/menu.h"
#include "deskgui/univ/ctrl/searchCtrl.h"   // ibSearchCtrl — the bar's search field
#include "frmclient/visualView/visualHostClient.h"   // ibVisualHostClient::IsStudioHost — the bar's real width in the editor

namespace {

// The ids the tools take — the entry's place after the first.
constexpr int kFirstToolId = wxID_HIGHEST + 1;

constexpr long long kSeparatorId = -1;

// An entry's picture as its field names it — its id, or a file's PNG — hashed.
std::size_t PictureHash(const ibProtocolNode& entry)
{
	const ibProtocolNode picture = entry.FindChild(ibProtocol::ibForm::ibCommandBar::Picture);
	if (!picture.IsBinary())
		return std::hash<ibString>()(picture.AsString());
	const ibMemoryBuffer png = picture.AsBinary();
	return std::hash<std::string_view>()(std::string_view(png, png.GetDataLen()));
}

// The entries as a list of what they are made of — the ids, captions, pictures, kinds, a group's commands — less
// whether they are enabled: two bars of one shape differ only in what may be pressed, and that is set, not built.
ibString ShapeOf(const ibProtocolNode& commandBar)
{
	ibString shape;
	for (const ibProtocolNode& entry : commandBar.Children()) {
		shape << entry.GetInt(ibProtocol::ibForm::ibCommandBar::Id) << wxT('|')
			<< entry.GetString(ibProtocol::ibForm::ibCommandBar::Caption) << wxT('|')
			<< static_cast<unsigned long long>(PictureHash(entry))
			<< wxT('|') << entry.GetInt(ibProtocol::ibForm::ibCommandBar::Kind) << wxT('|')
			<< entry.GetBool(ibProtocol::ibForm::ibCommandBar::Default, false) << wxT('|')
			<< static_cast<unsigned long long>(entry.Children().size()) << wxT(';');
	}
	// …and whether it ends with a search field — the field's text is set, not built.
	if (commandBar.Has(ibProtocol::ibForm::ibCommandBar::Search))
		shape << wxT("search;");
	return shape;
}

} // namespace

//***********************************************************************************
//*                                  commandBar                                     *
//***********************************************************************************

ibViewCommandBar::ibViewCommandBar(wxWindow* parent, const ibVisualHostClient& host, ibSendEvent sendEvent)
	: m_sendEvent(std::move(sendEvent))
{
	m_bar = new ibAuiToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		ibAUI_TB_NO_AUTORESIZE | ibAUI_TB_HORZ_TEXT | ibAUI_TB_OVERFLOW);
	m_bar->SetDesignerMode(host.IsStudioHost());   // show real width in the editor (no overflow collapse)

	// Hidden until it has a command: an empty strip takes no room.
	m_bar->Hide();

	m_bar->Bind(wxEVT_TOOL, &ibViewCommandBar::OnTool, this);
	m_bar->Bind(ibEVT_AUITOOLBAR_TOOL_DROPDOWN, &ibViewCommandBar::OnToolDropDown, this);
}

wxWindow* ibViewCommandBar::GetWindow() const
{
	return m_bar;
}

ibProtocolNode ibViewCommandBar::FindEntryByPosition(const wxPoint& point) const
{
	const ibAuiToolBarItem* const tool = m_bar->FindToolByPosition(point.x, point.y);
	const std::vector<ibProtocolNode> entries = m_entries.Children();
	const std::size_t idx = tool != nullptr ? static_cast<std::size_t>(tool->GetId() - kFirstToolId) : entries.size();
	return idx < entries.size() ? entries[idx] : ibProtocolNode();
}

void ibViewCommandBar::Update(const ibProtocolNode& commandBar)
{
	const std::vector<ibProtocolNode> entries = commandBar.Children();

	const bool searched = commandBar.Has(ibProtocol::ibForm::ibCommandBar::Search);
	const wxString searchText = searched ? wxString(commandBar.GetString(ibProtocol::ibForm::ibCommandBar::Search)) : wxString();

	// One of the same shape — only what may be pressed is set, and what the search field holds.
	const ibString shape = ShapeOf(commandBar);
	if (shape == m_shape) {
		for (std::size_t idx = 0; idx < entries.size(); ++idx) {
			if (entries[idx].GetInt(ibProtocol::ibForm::ibCommandBar::Id) != kSeparatorId)
				m_bar->EnableTool(kFirstToolId + static_cast<int>(idx), entries[idx].GetBool(ibProtocol::ibForm::ibCommandBar::Enabled, true));
		}
		if (searched)
			UpdateSearch(searchText);
		m_entries = commandBar.IsNode() ? commandBar.Clone() : ibProtocolNode();
		m_bar->Refresh();
		return;
	}

	// Built anew — the desktop's FillCommandBarToolBar: a separator only after a tool, a group with the arrow beside
	// it, a command not enabled greyed but there.
	m_shape = shape;
	m_entries = commandBar.IsNode() ? commandBar.Clone() : ibProtocolNode();
	// The pictures of the bar and of its groups' members asked for at once — not a call per tool.
	ibProtocolPictureReceive(commandBar);
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
		ibAuiToolBarItem* const tool = m_bar->AddTool(toolId, caption,
			ibProtocolPicture(entry, ibProtocol::ibForm::ibCommandBar::Picture), wxNullBitmap, wxITEM_NORMAL,
			entry.GetString(ibProtocol::ibForm::ibCommandBar::Tooltip, caption), wxEmptyString, nullptr);
		// the bar's default command stands out - a form's «… and close», or the item the author marked
		tool->SetDefault(entry.GetBool(ibProtocol::ibForm::ibCommandBar::Default, false));
		if (entry.GetInt(ibProtocol::ibForm::ibCommandBar::Kind) == static_cast<int>(ibProtocol::ibForm::ibCommandBar::ibKind::Group))
			m_bar->SetToolDropDown(toolId, true);
		if (!entry.GetBool(ibProtocol::ibForm::ibCommandBar::Enabled, true))
			m_bar->EnableTool(toolId, false);
		anyTool = true;
	}

	// THE SEARCH FIELD AT THE END — one of the bar's items, pushed to its end, and the last it gives up to its ▾: a bar
	// too narrow takes the tools under ▾ first (SetOverflowLast; Max, 2026-11-15). The tools are cleared above and the
	// field is not destroyed with them: it is the same window, added again, so what is being typed into it stays.
	if (searched) {
		UpdateSearch(searchText);
		m_bar->AddStretchSpacer();
		m_bar->AddControl(m_search)->SetOverflowLast(true);
	}
	if (m_search != nullptr)
		m_search->Show(searched);
	m_bar->Realize();

	const bool shown = anyTool || searched;
	if (m_bar->IsShown() != shown) {
		m_bar->Show(shown);
		if (wxWindow* const parent = m_bar->GetParent())
			parent->Layout();
	}
}

void ibViewCommandBar::UpdateSearch(const wxString& text)
{
	if (m_search == nullptr) {
		m_search = new ibSearchCtrl(m_bar, wxID_ANY, wxEmptyString, wxDefaultPosition, m_bar->FromDIP(wxSize(220, -1)));
		m_search->ShowCancelButton(true);
		m_search->SetDescriptiveText(_("Search"));
		// as the studio's tree looks for what is typed (ibMetaTreeBase::OnStartSearch): at every change, the "×" too
		m_search->Bind(wxEVT_TEXT, &ibViewCommandBar::OnSearch, this);
	}
	// the bar's font, as its tools are written in — the owner gives it to the bar (ApplyLook), and a window's font is not
	// its children's
	if (m_search->GetFont() != m_bar->GetFont())
		m_search->SetFont(m_bar->GetFont());
	if (!m_search->HasFocus() && m_search->GetValue() != text)
		m_search->ChangeValue(text);
}

bool ibViewCommandBar::PopupGroup(int toolId)
{
	const std::vector<ibProtocolNode> entries = m_entries.Children();
	const std::size_t idx = static_cast<std::size_t>(toolId - kFirstToolId);
	if (idx >= entries.size() || entries[idx].GetInt(ibProtocol::ibForm::ibCommandBar::Kind) != static_cast<int>(ibProtocol::ibForm::ibCommandBar::ibKind::Group))
		return false;

	// Its entries under it, a group of them a submenu of its own — the command picked sent by its own id.
	const std::vector<ibProtocolNode> members = entries[idx].Children();
	if (members.empty())
		return true;
	ibMenu menu;   // the theme's - the bar is a window of the theme's (ibAuiToolBar : ibControl)
	std::vector<long long> ids;
	AppendEntries(menu, members, wxString(), ids);

	const int chosen = m_bar->GetPopupMenuSelectionFromUser(menu, m_bar->GetToolRect(toolId).GetBottomLeft());
	if (chosen == wxID_NONE)
		return true;
	const std::size_t place = static_cast<std::size_t>(chosen - kFirstEntryId);
	if (place >= ids.size())
		return true;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Id, ids[place]);
	m_sendEvent(ibProtocol::ibEvent::ibKind::Command, args);
	return true;
}

void ibViewCommandBar::AppendEntries(wxMenu& menu, const std::vector<ibProtocolNode>& entries, const wxString& cell,
	std::vector<long long>& ids)
{
	for (const ibProtocolNode& entry : entries) {
		wxString caption = entry.GetString(ibProtocol::ibForm::ibCommandBar::Caption);
		wxMenuItem* item = nullptr;
		if (entry.GetInt(ibProtocol::ibForm::ibCommandBar::Kind) == static_cast<int>(ibProtocol::ibForm::ibCommandBar::ibKind::Group)) {
			ibMenu* const submenu = new ibMenu;
			AppendEntries(*submenu, entry.Children(), cell, ids);
			item = menu.AppendSubMenu(submenu, caption);
			item->Enable(entry.GetBool(ibProtocol::ibForm::ibCommandBar::Enabled, true));
		}
		else {
			if (entry.GetBool(ibProtocol::ibForm::ibCommandBar::CurrentCell, false) && !cell.empty()) {
				wxString shown = cell;
				shown.Replace(wxT("&"), wxT("&&"));   // the cell's text, not a mnemonic
				caption << wxT(": ") << shown;
			}
			const wxString shortcut = entry.GetString(ibProtocol::ibForm::ibCommandBar::Shortcut);
			if (!shortcut.empty())
				caption << wxT('\t') << shortcut;
			item = menu.Append(kFirstEntryId + static_cast<int>(ids.size()), caption);
			ids.push_back(entry.GetInt(ibProtocol::ibForm::ibCommandBar::Id));
			item->Enable(entry.GetBool(ibProtocol::ibForm::ibCommandBar::Enabled, true));
		}
		const wxBitmap picture = ibProtocolPicture(entry, ibProtocol::ibForm::ibCommandBar::Picture);
		if (picture.IsOk())
			item->SetBitmap(picture);
	}
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
	m_sendEvent(ibProtocol::ibEvent::ibKind::Command, args);
}

void ibViewCommandBar::OnToolDropDown(ibAuiToolBarEvent& event)
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

void ibViewCommandBar::OnSearch(wxCommandEvent& WXUNUSED(event))
{
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Text, ibString(m_search->GetValue()));
	m_sendEvent(ibProtocol::ibEvent::ibKind::Search, args);
}
