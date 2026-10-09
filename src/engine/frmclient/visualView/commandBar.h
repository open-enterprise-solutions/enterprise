#ifndef _FRMCLIENT_VIEW_COMMAND_BAR_H__
#define _FRMCLIENT_VIEW_COMMAND_BAR_H__

#include <functional>
#include <string>

#include <wx/wx.h>
#include <wx/aui/auibar.h>

#include "protocol/protocolNode.h"

class ibAuiToolBar;

// A COMMAND BAR — the State's CommandBar of a form, a table or a spreadsheet, drawn: the desktop's toolbar of a
// command bar (frontend/visualView/layers/commandBar.cpp, BuildCommandBarToolBar) over the entries the server wrote,
// not the commands it resolves. A press goes back as Command {Id[, Member]} to whoever the bar is — its owner's
// `sendCommand` sends it.
//
// The tools take ids of the bar's own, by the entries' places: the server's ids are its numbers, not wx's.
class ibViewCommandBar {
public:

	// How the bar's owner sends a command pressed — Command {Id[, Member]}, to the control the bar is.
	using ibSendCommand = std::function<void(const ibProtocolNode& args)>;

	ibViewCommandBar(wxWindow* parent, ibSendCommand sendCommand);

	wxWindow* GetWindow() const;

	// The entries drawn — built anew when they are others, only enabled or not when they are the same; the bar is
	// hidden while it has none.
	void Update(const ibProtocolNode& commandBar);

private:

	// A group pressed — its commands offered under it, the one picked sent. False: no group.
	bool PopupGroup(int toolId);

	void OnTool(wxCommandEvent& event);
	void OnToolDropDown(wxAuiToolBarEvent& event);

	ibAuiToolBar*       m_bar = nullptr;   // the parent's window
	const ibSendCommand m_sendCommand;
	std::string         m_shape;           // the entries as last built, less what is enabled
	ibProtocolNode      m_entries;         // a copy — a group's commands are read from it when it is pressed
};

#endif
