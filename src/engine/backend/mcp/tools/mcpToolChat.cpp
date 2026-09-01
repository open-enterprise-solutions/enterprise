////////////////////////////////////////////////////////////////////////////
//	Description : the conversation tools — talking to the person at the designer
////////////////////////////////////////////////////////////////////////////
//
// The designer has a window where a person writes. The platform cannot call a
// model — no outbound TLS, no key, and neither belongs in an ERP engine — but
// whoever is CONNECTED can. So the exchange is turned around: what was typed
// waits in the server, the connected assistant collects it with one tool and
// answers with another.
//
// ⚠ THE SHORT ROAD, said plainly. The client has to come and look; nothing here
// wakes it. The long road is MCP sampling (the server asking the client
// unprompted), which needs a server→client stream. The WINDOW does not change
// when that lands — only who pulls this queue.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/appData.h"
#include "backend/mcp/mcpServer.h"

namespace {

ibMcpServer* Server(wxString& refusal)
{
	ibMcpServer* server = ibApplicationData::GetMcpServer();
	if (server == nullptr) {
		refusal = _("There is no assistant server in this process.");
		return nullptr;
	}

	return server;
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgLast()
{
	static const ibArg s_a(wxT("last"), ibArg::Kind::Whole,
		_("Only the last N turns. A long conversation read whole is mostly padding; the "
		  "recent end is where the current task is. Omit for all of it."));
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		_("What to say. Markdown: a heading, a list, a fenced snippet - the emphasis you "
			  "choose is the emphasis they see."), /*required*/ true);
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// chat_take
//---------------------------------------------------------------------------
class ibMcpToolChatTake : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("chat_take"); }

	wxString GetDescription() const override
	{
		return _("Collect what the person at the designer has written in the assistant window, and "
			"what has HAPPENED in their configuration while you were not looking. Answers empty "
			"lists when there is nothing new. Taking REMOVES both from the queue, so ask once and "
			"answer what you got - chat_history reads the whole conversation back without "
			"collecting anything, which is what to use for context.\n\n"
			"THE TWO ANSWERS OBLIGE DIFFERENTLY. `messages` is what the person SAID: a request, "
			"and it is addressed to you. `observed` is what the person DID - a metaobject created, "
			"renamed, removed, a configuration saved or applied - forwarded as it happened. It is "
			"information you may read and act on when it matters; it asks you for nothing, and "
			"answering it as though it were a message means replying to somebody who said nothing.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& WXUNUSED(params), ibDataNode& result, wxString& refusal) const override
	{
		ibMcpServer* server = Server(refusal);
		if (server == nullptr)
			return false;

		std::vector<ibDataValue> messages;
		for (const wxString& said : server->TakeSaid())
			messages.push_back(ibDataValue::String(said));

		result.AddField(wxT("messages"), ibDataValue::Array(messages));

		// …AND WHAT MERELY HAPPENED, under its own name — see the description for why the two are
		// not one list.
		std::vector<ibDataValue> observed;
		for (const wxString& noted : server->TakeNoted())
			observed.push_back(ibDataValue::String(noted));

		result.AddField(wxT("observed"), ibDataValue::Array(observed));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolChatTake);

//---------------------------------------------------------------------------
// chat_say
//---------------------------------------------------------------------------
class ibMcpToolChatSay : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("chat_say"); }

	wxString GetDescription() const override
	{
		return _("Say something to the person at the designer - it appears in their assistant "
			"window. Write for a person: what you did and what you propose, not the shape of "
			"the calls you made. Markdown is rendered.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgText() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMcpServer* server = Server(refusal);
		if (server == nullptr)
			return false;

		const wxString text = ArgText().Text(params);
		if (text.IsEmpty()) {
			refusal = _("Nothing to say.");
			return false;
		}

		server->Reply(text);
		result.AddField(wxT("delivered"), ibDataValue::Bool(true));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolChatSay);

//---------------------------------------------------------------------------
// chat_history
//---------------------------------------------------------------------------
//
// ⭐ TAKING IS NOT READING, and one destructive queue was being asked to be both. chat_take EMPTIES
// what it collects — rightly, so a message is answered once and not twice — but that made the
// conversation unrecoverable the moment it was collected: an assistant that reconnected knew
// nothing of what had been discussed, and the window that showed it started blank every time it
// was reopened.
//
// So the exchange is kept beside the queue, and this is the reading end of it. Ask it when you
// join a conversation already in progress, or after anything that lost your place.
//
// ⚠ THIS SESSION ONLY. It lives with the server and goes when the designer does. Persisting it
// needs somewhere to put it and a decision about how long it is kept — saying "history" and
// quietly meaning "since this morning" would be worse than saying so.
//
//---------------------------------------------------------------------------

class ibMcpToolChatHistory : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("chat_history"); }

	wxString GetActivity(const ibDataNode& WXUNUSED(params)) const override
	{
		return _("reading back the conversation");
	}

	wxString GetDescription() const override
	{
		return _("The whole conversation with the person at the designer, oldest first, WITHOUT "
			"collecting anything - chat_take empties its queue, this does not. Read it when you "
			"connect, or whenever you need the context of what was already discussed: what they "
			"asked for earlier is usually why the configuration looks the way it does. Covers "
			"this run of the designer only.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgLast() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMcpServer* server = Server(refusal);
		if (server == nullptr)
			return false;

		const std::vector<ibMcpServer::Turn> conversation = server->GetConversation();

		s32 last = (s32)ArgLast().Whole(params);
		if (last <= 0 || last > (s32)conversation.size())
			last = (s32)conversation.size();

		std::vector<ibDataValue> turns;

		// FROM THE END, so `last` means the RECENT ones. Trimming the tail instead would answer a
		// request for context with the beginning of a conversation nobody is having any more.
		for (size_t index = conversation.size() - (size_t)last;
			index < conversation.size(); ++index) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();

			// WHO, as a word. "them" and "me" is what a reader needs to follow it; a boolean
			// would make every consumer invent the same two words for itself.
			entry->SetValue(wxT("from"), wxString(
				conversation[index].fromPerson ? wxT("person") : wxT("assistant")));
			entry->SetValue(wxT("text"), conversation[index].text);

			turns.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("turns"), ibDataValue::Int((s64)turns.size()));
		result.AddField(wxT("total"), ibDataValue::Int((s64)conversation.size()));
		result.AddField(wxT("conversation"), ibDataValue::Array(turns));

		if (conversation.empty())
			result.SetValue(wxT("note"),
				_("Nothing has been said yet in this run of the designer."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolChatHistory);
