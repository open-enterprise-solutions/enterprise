#ifndef _FRMCLIENT_VIEW_COMMAND_BAR_H__
#define _FRMCLIENT_VIEW_COMMAND_BAR_H__

#include <functional>
#include <string>

#include <wx/wx.h>
#include "deskgui/univ/aui/auibar.h"

#include "frmclient/frmclient.h"
#include "protocol/protocol.h"
#include "protocol/protocolNode.h"

class ibAuiToolBar;
class ibSearchCtrl;

// A COMMAND BAR — the State's CommandBar of a form, a table or a spreadsheet, drawn: the old desktop's toolbar of a
// command bar (BuildCommandBarToolBar) over the entries the server wrote,
// not the commands it resolves. A press goes back as Command {Id} — the entry's, on the bar or in a submenu at any
// depth — to whoever the bar is — its owner's
// `sendEvent` sends it; and where the server wrote the bar a search field (Search), the field stands at its end, one of
// its items and the last the bar gives up to its ▾ — and what it holds goes back as Search {Text} at every change of it.
//
// The tools take ids of the bar's own, by the entries' places: the server's ids are its numbers, not wx's.
class FRMCLIENT_API ibViewCommandBar {
public:

	// How the bar's owner sends what was done on the bar — Command {Id}, Search {Text} — to the control the bar is.
	using ibSendEvent = std::function<void(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args)>;

	// The host it is drawn in says whether it is edited — there it shows its real width (the desktop's SetDesignerMode).
	ibViewCommandBar(wxWindow* parent, const class ibVisualHostClient& host, ibSendEvent sendEvent);

	wxWindow* GetWindow() const;
	// The entry drawn at that point of its window — none: no tool there (wx's FindToolByPosition).
	ibProtocolNode FindEntryByPosition(const wxPoint& point) const;

	// The entries drawn — built anew when they are others, only enabled or not when they are the same — and the search
	// field at the end when the bar has one; the bar is hidden while it has neither.
	void Update(const ibProtocolNode& commandBar);

	// The menu id of a command AppendEntries puts — its place in `ids` after this one: a menu of its own, so the ids need
	// only differ inside it.
	static constexpr int kFirstEntryId = wxID_HIGHEST + 1;

	// ⭐ ENTRIES AS A MENU into `menu` — a group's as its submenu, to any depth; each command's id kept in `ids`, its menu
	// id its place there after kFirstEntryId; its key written after its caption (as a menu writes it, past a tab), and a
	// command about the current cell (CurrentCell) saying `cell` after its caption — «Find: Customer - Store 2». A group's
	// dropdown on the bar and a table's context menu alike.
	static void AppendEntries(wxMenu& menu, const std::vector<ibProtocolNode>& entries, const wxString& cell,
		std::vector<long long>& ids);

private:

	// A group pressed — its entries offered under it, a group of them a submenu of its own, the command picked sent by
	// its own id. False: no group.
	bool PopupGroup(int toolId);
	// The search field, made the first time a bar has one; the text the server holds put into it — unless the person is
	// typing there, when what they typed is newer than what the server answered.
	void UpdateSearch(const wxString& text);

	void OnTool(wxCommandEvent& event);
	void OnToolDropDown(ibAuiToolBarEvent& event);
	// What the search field holds changed (the × too): it is sent.
	void OnSearch(wxCommandEvent& event);

	ibAuiToolBar*       m_bar = nullptr;     // the parent's window
	ibSearchCtrl*       m_search = nullptr;  // the bar's window, its last item; null until a bar has a search field
	const ibSendEvent   m_sendEvent;
	ibString            m_shape;             // the entries as last built, less what is enabled
	ibProtocolNode      m_entries;           // a copy — a group's commands are read from it when it is pressed
};

#endif
