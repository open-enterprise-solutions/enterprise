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

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this tool takes — declared once, and read through the same objects in Call.
const ibArg& ArgProblemsOnly() { static const ibArg a(wxT("problemsOnly"), ibArg::Kind::Flag, _("Only errors and warnings. Default true - the rest is progress chatter.")); return a; }
const ibArg& ArgLimit() { static const ibArg a(wxT("limit"), ibArg::Kind::Whole, _("How many at most, newest last. Default 40.")); return a; }
const ibArg& ArgClear() { static const ibArg a(wxT("clear"), ibArg::Kind::Flag, _("Empty it afterwards, so the next question answers about what happened next.")); return a; }

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
			text << _("The platform stopped to ask: ");
		else if (message.m_status == ibStatusMessage::ibStatusMessage_Error)
			text << _("The platform refused: ");
		else
			text << _("The platform warns: ");

		text << message.m_text;

		// WHERE IT CAME FROM, when it came from a module. A complaint about a
		// script is a sentence; with the module and the line it is an address.
		if (!message.m_docPath.IsEmpty()) {
			text << wxT("\n") << _("module: ") << message.m_docPath;
			if (message.m_line != wxNOT_FOUND)
				text << wxT(", ") << _("line ") << (int)message.m_line;
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

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ArgClear().Flag(params)
			? wxString(_("clearing the message pane"))
			: wxString(_("reading what the platform has said"));
	}

	wxString GetDescription() const override
	{
		return _("Everything the platform has said in the designer - a metaobject explaining "
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
			refusal = _("There is no designer window in this process.");
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
				? _("Nothing was reported as a problem.")
				: _("Nothing has been said."));

		if (ArgClear().Flag(params))
			mainFrame->ClearMessage();

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMessagesRead);
