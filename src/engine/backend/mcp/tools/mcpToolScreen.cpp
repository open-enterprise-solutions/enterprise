////////////////////////////////////////////////////////////////////////////
//	Description : a picture of the window, when the person agrees to it
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ THE CASE THIS EXISTS FOR. "The list displays wrongly", "the numbers do not add up" — the
// hardest reports to act on, because the half that matters is never in the sentence: which form is
// open, which period, which filters, which column is actually being looked at. A person who can see
// it cannot always say it, and today they take a screenshot by hand and paste it in (Max,
// 2026-09-04: *"I send you screenshots all the time; you look and understand from the picture how
// it is built"*). This is that, without the hand.
//
// ⭐ AND IT LIVES ON THE FRONTEND SIDE, DELIBERATELY. Photographing a window is not something the
// engine can promise: it depends on what is drawing — a desktop GUI can, a web client is a
// different question entirely. So the ABILITY is the tool's presence: this file is built into the
// desktop designer, and a host that cannot do it simply does not carry the verb. Nothing declares a
// capability and nothing refuses at runtime; the tool list is the answer.
//
// 🛑⭐⭐ THREE THINGS HAPPEN, IN THIS ORDER, AND NONE MAY BE SKIPPED (Max, 2026-09-04): the person is
// ASKED — shown what is wanted and why; the act is WRITTEN DOWN — into the registration journal,
// which is the surface an auditor reads; and only then is the picture WORKED WITH. A screen holds
// somebody's business — their customers, their sums, their pay — and consent alone is not enough:
// there has to be a record of what was permitted and whether it happened. Both the asking and the
// entry live at the far end, in the process that owns the screen, so this side cannot bypass them.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/debugger/debugClient.h"
#include "backend/mcp/mcpDebugBridge.h"

#include <wx/base64.h>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgReason()
{
	static const ibArg s_a(wxT("reason"), ibArg::Kind::Text,
		ibMcpText("What you are trying to see, in words the PERSON will read - it is shown to them in "
		  "the question, so they agree to a purpose rather than to a verb. \"To see why the goods "
		  "list shows no rows\" is a reason; \"debugging\" is not, and will be declined more often "
		  "than not."), /*required*/ true);
	return s_a;
}

const ibArg& ArgArea()
{
	static const ibArg s_a(wxT("area"), ibArg::Kind::Text,
		ibMcpText("What to photograph. focus - just around the control they clicked on, with a margin: the "
		  "smallest picture and usually the right one, since somebody showing you something points at "
		  "it first. active - the whole window being worked in (the default). main - the application "
		  "main window. screen - the whole display, for when they move between windows."),
			/*required*/ false, { wxT("focus"), wxT("active"), wxT("main"), wxT("screen") });
	return s_a;
}

const ibArg& ArgFormat()
{
	static const ibArg s_a(wxT("format"), ibArg::Kind::Text,
		ibMcpText("png is the default and is almost always right. MEASURED 2026-09-04 on this platform: a "
		  "window came to 20 KB as png and 31 KB as jpeg - an interface is flat fills and sharp edges, "
		  "which png compresses better while jpeg spends bits smearing the text. Reach for jpeg only "
		  "when the screen is full of photographs or gradients. To make a picture SMALLER, narrow the "
		  "area instead: focus is about a quarter the size of the whole window."),
			/*required*/ false, { wxT("png"), wxT("jpeg") });
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// screen_capture
//---------------------------------------------------------------------------
class ibMcpToolScreenCapture : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("screen_capture"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("asking the person for a picture of their window");
	}

	// 🛑⭐⭐ NOT ON THE MAIN THREAD — the third verb here to need this, and it deadlocks the same way
	// the other two did. The request goes down the socket and the picture comes back through
	// wxTheApp::CallAfter, so waiting for it ON the main thread is waiting for a message only that
	// thread can dispatch: it never arrives, the deadline always expires, and the answer blames a
	// runtime that replied at once.
	//
	// Measured 2026-09-04, and the symptom named the culprit exactly: during the wait the DESIGNER
	// stopped responding while the application stayed responsive — the frozen process was the one
	// asking, not the one being asked.
	bool NeedsMainThread() const override { return false; }

	wxString GetDescription() const override
	{
		return ibMcpText("Ask the RUNNING APPLICATION for a picture of its window - the screen the "
			"person is actually looking at. They are asked first, shown your reason, and may say "
			"no; either way the request is written into the registration journal, because a screen "
			"holds their business and not yours.\n\n"
			"Use it when what is wrong is SEEN rather than computed: a list showing nothing, a "
			"column in the wrong place, a total that looks odd, a form that does not look like it "
			"should. It is the answer to \"I cannot explain it, look\".\n\n"
			"It is NOT the way to check a value - query_run and debug_sandbox say WHY a number is "
			"what it is, which a picture never can. And it does not need the runtime stopped: a "
			"window draws itself while the application runs.\n\n"
			"THE MOST EXPENSIVE THING HERE IS READING IT. Taking the picture costs tens of "
			"milliseconds; LOOKING at it costs a second and a large slice of the reply, and grows with "
			"every pixel. So ask last, after the journal and the sandbox have said what they can, and "
			"ask NARROW: area focus is a quarter the size of the window and holds the thing that was "
			"pointed at. Photograph the whole screen only when the question really is about several "
			"windows at once.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgReason(), ArgArea(), ArgFormat() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (debugClient == nullptr) {
			refusal = ibMcpText("This process has no debugger client.");
			return false;
		}

		ibMcpDebugBridge* bridge = ibMcpDebug();
		if (bridge == nullptr) {
			refusal = ibMcpText("The assistant is not attached to the debugger.");
			return false;
		}

		const wxString reason = ArgReason().Text(params);
		if (reason.IsEmpty()) {
			refusal = ibMcpText("Say what you are trying to see. The person is shown that sentence and "
				"decides by it, so it cannot be left out.");
			return false;
		}

		const wxString area = ArgArea().Given(params) ? ArgArea().Text(params) : wxString(wxT("active"));

		bool allowed = false;
		wxMemoryBuffer png;
		wxString focus;

		const wxString format = ArgFormat().Given(params) ? ArgFormat().Text(params) : wxString(wxT("png"));

		if (!bridge->Screenshot(reason, area, format, allowed, png, focus)) {
			refusal = ibMcpText("No answer from the running application. It has to be connected - "
				"app_run starts it - and somebody has to be at it to answer the question.");
			return false;
		}

		if (!allowed || png.GetDataLen() == 0) {
			// ⚠ TWO DIFFERENT NOES, AND THEY ASK FOR DIFFERENT NEXT MOVES. The far end explains itself
			// when it can — a runtime parked at a breakpoint is not drawing and says so, and the
			// answer to that is to continue the run, not to ask the person anything.
			refusal = !focus.IsEmpty()
				? focus
				: ibMcpText("The person did not allow it. Nothing was captured. Ask them in words "
					"what they are seeing, or say more plainly why the picture would help.");
			return false;
		}

		// ⭐⭐ HANDED BACK AS AN IMAGE, not as a description of one — the whole point is that it gets
		// LOOKED AT. `image` is the field the server turns into an image block, so this arrives as a
		// picture rather than as a wall of base64 inside a sentence.
		result.SetValue(wxT("image"),
			wxBase64Encode(png.GetData(), png.GetDataLen()));
		result.SetValue(wxT("imageType"), wxString(format.IsSameAs(wxT("jpeg"), false) ? wxT("image/jpeg") : wxT("image/png")));
		result.AddField(wxT("bytes"), ibDataValue::Int((s64)png.GetDataLen()));

		// ⭐⭐ WHAT THEY WERE POINTING AT. Somebody who cannot say what is wrong CAN click on it, and
		// that click is the missing half of the sentence: the control holding the focus is ringed in
		// the picture and named here, so "look, this row" arrives as an address rather than a gesture.
		if (!focus.IsEmpty())
			result.SetValue(wxT("focus"), focus);

		result.SetValue(wxT("note"),
			ibMcpText("The person allowed this and it is recorded in the registration journal. It is "
			  "their screen: read what you asked for and do not keep it around."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolScreenCapture);
