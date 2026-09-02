////////////////////////////////////////////////////////////////////////////
//	Description : the designer forwards what it says to the assistant
////////////////////////////////////////////////////////////////////////////
//
// TWO HALVES OF ONE IDEA, and the second is the one that matters.
//
//   messages_read ANSWERS what the platform has said. Correct, and passive: it
//   has to be called, and something that has to be called is noticed only by
//   whoever already suspects there is something to notice. An assistant that has
//   just built ten objects has no reason to suspect the fourth refused itself.
//
//   The LISTENER makes it arrive. Registered on the designer's window, it takes
//   every refusal, warning and modal box straight to the assistant's
//   conversation — which is a channel that wakes it.
//
// Both live here: the window only hands the message over — see mcpDesignerMessages.h. The
// backend's frame is untouched: this is the designer arranging its own delivery.
//
// PROGRESS CHATTER IS NOT DELIVERED. An informational line is the platform
// narrating itself; waking anything for it makes the channel worth ignoring, and
// a channel worth ignoring will be ignored on the day it matters.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/appData.h"
#include "backend/mcp/mcpServer.h"

#include "mcp/mcpDesignerMessages.h"
#include "mainFrame/mainFrameDesigner.h"

#include <wx/dialog.h>       // what a modal IS — window_dismiss looks for these among the top windows
#include <wx/modalhook.h>    // …and wx tells us the moment one opens, whoever opened it

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this tool takes — declared once, and read through the same objects in Call.
const ibArg& ArgProblemsOnly() { static const ibArg a(wxT("problemsOnly"), ibArg::Kind::Flag, ibMcpText("Only errors and warnings. Default true - the rest is progress chatter.")); return a; }
const ibArg& ArgLimit() { static const ibArg a(wxT("limit"), ibArg::Kind::Whole, ibMcpText("How many at most, newest last. Default 40.")); return a; }
const ibArg& ArgClear() { static const ibArg a(wxT("clear"), ibArg::Kind::Flag, ibMcpText("Empty it afterwards, so the next question answers about what happened next.")); return a; }

// …and the two window_dismiss takes. Declared here with the rest, read through the same objects.
const ibArg& ArgClose() { static const ibArg a(wxT("close"), ibArg::Kind::Flag, ibMcpText("Close them, rather than only saying what is open. Off by default: looking first is free, and a dialog may be one the person opened for themselves.")); return a; }
const ibArg& ArgAll() { static const ibArg a(wxT("all"), ibArg::Kind::Flag, ibMcpText("Every dialog standing, not only the topmost. Off by default.")); return a; }

wxString LevelName(ibStatusMessage status)
{
	switch (status) {
		case ibStatusMessage::ibStatusMessage_Error:   return wxT("error");
		case ibStatusMessage::ibStatusMessage_Warning: return wxT("warning");
		default: break;
	}
	return wxT("info");
}

//---------------------------------------------------------------------------
// the listener — a refusal reaches the assistant instead of waiting to be asked
//---------------------------------------------------------------------------
class ibMcpMessageListener : public ibDesignerMessages::Listener {
public:

	void OnMessage(const ibDesignerMessages::Message& message) override
	{
		if (message.m_status != ibStatusMessage::ibStatusMessage_Error
			&& message.m_status != ibStatusMessage::ibStatusMessage_Warning)
			return;

		if (appData == nullptr)
			return;

		ibMcpServer* server = appData->GetMcpServer();
		if (server == nullptr || !server->IsRunning())
			return;

		wxString text;
		if (message.m_modal)
			text << ibMcpText("The platform stopped to ask: ");
		else if (message.m_status == ibStatusMessage::ibStatusMessage_Error)
			text << ibMcpText("The platform refused: ");
		else
			text << ibMcpText("The platform warns: ");

		text << message.m_text;

		// WHERE IT CAME FROM, when it came from a module. A complaint about a
		// script is a sentence; with the module and the line it is an address.
		if (!message.m_docPath.IsEmpty()) {
			text << wxT("\n") << ibMcpText("module: ") << message.m_docPath;
			if (message.m_line != wxNOT_FOUND)
				text << wxT(", ") << ibMcpText("line ") << (int)message.m_line;
		}

		server->Say(text);
	}
};

// One instance, registered when the window is there to register with. Removed
// nowhere on purpose: it lives as long as the process, and outlives the frame.
ibMcpMessageListener g_listener;
bool g_registered = false;

void EnsureRegistered()
{
	if (g_registered || mainFrame == nullptr)
		return;

	ibDesignerMessages::AddListener(&g_listener);
	g_registered = true;
}

} // namespace

//---------------------------------------------------------------------------
// messages_read
//---------------------------------------------------------------------------
class ibMcpToolMessagesRead : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("messages_read"); }

	// Reading what the platform said is how a caller LEARNS a dialog is standing — and the text of
	// that dialog is usually in here.
	bool RunsWhileBusy() const override { return true; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ArgClear().Flag(params)
			? wxString(ibMcpText("clearing the message pane"))
			: wxString(ibMcpText("reading what the platform has said"));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Everything the platform has said in the designer - a metaobject explaining "
			"why it cannot be stored, a refused save, a modal warning, AND what the RUNNING "
			"APPLICATION reports back over the debugger. These also arrive on their own as "
			"they happen; this is for the history, and for checking after building something.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgProblemsOnly(), ArgLimit(), ArgClear() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (mainFrame == nullptr) {
			refusal = ibMcpText("There is no designer window in this process.");
			return false;
		}

		// The window exists by now, which is the earliest anything can be
		// registered on it. Idempotent.
		EnsureRegistered();

		bool problemsOnly = true;
		if (params.FindField(ArgProblemsOnly().Name()) != nullptr)
			problemsOnly = ArgProblemsOnly().Flag(params);

		const s32 asked = (s32)ArgLimit().Whole(params);
		const size_t limit = (asked > 0) ? (size_t)asked : 40;

		std::vector<ibDataValue> lines;

		for (const ibDesignerMessages::Message& message :
				ibDesignerMessages::All()) {

			const wxString level = LevelName(message.m_status);
			if (problemsOnly && level == wxT("info"))
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("level"), level);
			entry->SetValue(wxT("text"), message.m_text);

			if (message.m_modal)
				entry->AddField(wxT("modal"), ibDataValue::Bool(true));

			if (!message.m_docPath.IsEmpty())
				entry->SetValue(wxT("module"), message.m_docPath);
			if (message.m_line != wxNOT_FOUND)
				entry->AddField(wxT("line"), ibDataValue::Int((s64)message.m_line));

			lines.push_back(ibDataValue::Child(entry));
		}

		// Newest last, so a run of complaints reads in the order it happened.
		if (lines.size() > limit)
			lines.erase(lines.begin(), lines.begin() + (lines.size() - limit));

		result.AddField(wxT("count"), ibDataValue::Int((s64)lines.size()));
		result.AddField(wxT("messages"), ibDataValue::Array(lines));

		if (lines.empty())
			result.SetValue(wxT("note"), problemsOnly
				? ibMcpText("Nothing was reported as a problem.")
				: ibMcpText("Nothing has been said."));

		if (ArgClear().Flag(params))
			mainFrame->ClearMessage();

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMessagesRead);

//---------------------------------------------------------------------------
// window_dismiss
//---------------------------------------------------------------------------
//
// 🛑⭐⭐ A MODAL BOX STOPS EVERYTHING, INCLUDING THE WAY OUT OF IT. While one stands, the designer
// answers nothing: the person has to click it, and if they are not at the keyboard the session is
// simply frozen — with an assistant on the other end of a socket that can see the problem and can
// do nothing about it (measured 2026-09-02: `Cannot set breakpoint in unsaved copy!` appeared in
// front of Max as a modal, from a call the assistant made).
//
// ⭐ THE CALL STILL ARRIVES, WHICH IS WHY THIS CAN WORK AT ALL. A modal runs its own event loop and
// that loop goes on dispatching — so a tool delivered through CallAfter executes INSIDE the modal,
// and closing the dialog from there is what lets the outer work continue.
//
// ⚠ IT ANSWERS CANCEL, NEVER OK. Dismissing somebody's dialog is already a liberty; answering
// "yes" on their behalf to a question nobody read would be a different thing entirely — and the
// questions the platform asks modally are the ones with consequences ("apply?", "delete?"). What
// this is for is the boxes that only report, and the way out of a frozen session.
class ibMcpToolWindowDismiss : public ibMcpTool {

	// The dialogs standing right now, top first. wxTopLevelWindows holds every frame and dialog
	// there is; a dialog that is not shown is not in anybody's way.
	static std::vector<wxDialog*> Standing()
	{
		std::vector<wxDialog*> dialogs;

		for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst();
			 node != nullptr; node = node->GetNext()) {

			if (wxDialog* dialog = dynamic_cast<wxDialog*>(node->GetData()))
				if (dialog->IsShown())
					dialogs.push_back(dialog);
		}

		return dialogs;
	}

public:

	wxString GetName() const override { return wxT("window_dismiss"); }

	// THE WAY OUT of a standing dialog cannot itself be refused for standing behind one.
	bool RunsWhileBusy() const override { return true; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("closing a dialog that is standing in the way");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("CLOSE A MODAL DIALOG standing in the designer. While one is open the "
			"application answers nothing and the person has to click it - so a box raised by "
			"something you did leaves the session frozen until somebody is at the keyboard. "
			"Answers with what was standing and what was closed. It always answers CANCEL, never "
			"OK: a question with consequences is not one to answer on somebody's behalf. Call it "
			"with no argument to SEE what is open without touching it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgClose(), ArgAll() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const std::vector<wxDialog*> dialogs = Standing();

		std::vector<ibDataValue> standing;
		for (const wxDialog* dialog : dialogs)
			standing.push_back(ibDataValue::String(dialog->GetTitle().IsEmpty()
				? ibMcpText("(untitled dialog)") : dialog->GetTitle()));

		result.AddField(wxT("standing"), ibDataValue::Array(standing));

		if (dialogs.empty()) {
			result.SetValue(wxT("note"),
				ibMcpText("Nothing is standing in the way - no dialog is open."));
			return true;
		}

		if (!ArgClose().Flag(params)) {
			result.SetValue(wxT("note"),
				ibMcpText("These are open and the designer is waiting on them. Pass close:true to "
				  "dismiss the topmost, or close:true with all:true for every one."));
			return true;
		}

		const bool everyOne = ArgAll().Flag(params);
		std::vector<ibDataValue> closed;

		// TOP FIRST. wxTopLevelWindows keeps the newest last, and the newest is the one on top —
		// which is also the only one the person can interact with.
		for (auto it = dialogs.rbegin(); it != dialogs.rend(); ++it) {

			wxDialog* dialog = *it;
			closed.push_back(ibDataValue::String(dialog->GetTitle()));

			if (dialog->IsModal())
				dialog->EndModal(wxID_CANCEL);
			else
				dialog->Close(true);

			if (!everyOne)
				break;
		}

		result.AddField(wxT("closed"), ibDataValue::Array(closed));
		result.SetValue(wxT("note"),
			ibMcpText("Answered as Cancel. If that dialog was asking something that matters, ask the "
			  "person rather than assuming what it would have said."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolWindowDismiss);

//---------------------------------------------------------------------------
// …and the designer says when it is busy
//---------------------------------------------------------------------------
//
// ⭐⭐ EVERY MODAL, NOT ONLY OURS. wxModalDialogHook is called for anything that opens modally —
// a message box, a file dialog, a settings window somebody double-clicked — which is exactly the
// coverage this needs: what blocks the application is not our dialogs, it is ALL of them.
//
// 🛑 AND THE FAULT IT GUARDS AGAINST DID NOT EXIST BEFORE. One person cannot click in two places
// at once, so "a command arrives while a modal stands" was unreachable; an assistant that can open
// anything at any moment reaches it easily (Max, 2026-09-02, after the designer died exactly
// there). The server refuses the calls that would land in that gap, and says which dialog is in
// the way — see ibMcpBusyWith.
class ibMcpModalWatch : public wxModalDialogHook {
protected:

	int Enter(wxDialog* dialog) override
	{
		ibMcpBusyEnter(dialog != nullptr ? dialog->GetTitle() : wxString());
		return wxID_NONE;   // …and it is only WATCHING: the dialog opens exactly as it would have
	}

	void Exit(wxDialog* dialog) override
	{
		ibMcpBusyLeave();
	}
};

// Registered with the hook machinery for the life of the process — the designer always wants this,
// and there is nothing to configure about it.
static ibMcpModalWatch s_modalWatch;

struct ibMcpModalWatchInstaller {
	ibMcpModalWatchInstaller() { s_modalWatch.Register(); }
};

static ibMcpModalWatchInstaller s_modalWatchInstaller;
