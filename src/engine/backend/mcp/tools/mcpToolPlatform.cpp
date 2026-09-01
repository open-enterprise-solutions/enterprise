////////////////////////////////////////////////////////////////////////////
//	Description : the facts the handshake gave, asked again
////////////////////////////////////////////////////////////////////////////
//
// ⭐ A HANDSHAKE IS A SNAPSHOT. Everything a client is told at `initialize` — which dialect this
// configuration is written in, what languages it declares, which compatibility version gates it,
// who is logged in — was true at that moment, and the protocol has no way to say it changed.
//
// The dialect is the one that bites. It is a property of the configuration, a person can switch it
// between two of your calls, and code written in the other one is rejected by the compiler while
// reading perfectly to whoever wrote it. That is the worst failure shape there is: it looks like
// success until something runs.
//
// ⭐ SO THE ANSWER IS NOT A BETTER SNAPSHOT, IT IS A QUESTION. The orientation stays what it is —
// an introduction — and this is the same facts, live. Both come from `ibMcpDescribePlatform`, one
// function, because two places computing the dialect would agree right up until they did not.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

//---------------------------------------------------------------------------
// platform_state
//---------------------------------------------------------------------------

class ibMcpToolPlatformState : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("platform_state"); }

	wxString GetActivity(const ibDataNode& WXUNUSED(params)) const override
	{
		return _("checking how this configuration is written");
	}

	wxString GetDescription() const override
	{
		return _("What decides how anything here must be written, RIGHT NOW: the script dialect "
			"(word-fenced or C-style), the languages this configuration declares, its "
			"compatibility version, the platform build, and who you are acting as. ASK IT BEFORE "
			"WRITING CODE. The same facts are handed over at connection, but that was a snapshot "
			"- the dialect in particular can be switched while you are connected, and code in the "
			"wrong one reads correctly and does not compile.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& WXUNUSED(params), ibDataNode& result,
		wxString& WXUNUSED(refusal)) const override
	{
		// ⚠ NO REFUSAL PATH. With no configuration open this still answers - the build and the
		// account are facts either way, and `configurationOpen: false` is the answer to "what
		// dialect" rather than an error about it. A caller orienting itself should never be told
		// off for asking.
		ibMcpDescribePlatform(result);

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPlatformState);
