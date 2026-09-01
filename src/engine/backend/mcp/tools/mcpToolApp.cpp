////////////////////////////////////////////////////////////////////////////
//	Description : starting the application — the launch button, as a verb
////////////////////////////////////////////////////////////////////////////
//
// A file of its own because it is a subject of its own. It sat among the SESSION verbs, which
// answer about people already logged in — a neighbour by accident of when it was written, not by
// what it is about. Starting a process and listing who is in one are different questions.
//
//
// ⭐ THE LAST STEP OF THE LOOP. Everything else here builds a configuration and
// reads what came of it; this is what makes it RUN, so "update the database and
// start debugging" becomes one sequence instead of a sentence handed to a
// person to carry out.
//
// It is the same door the designer's Debug menu takes —
// ibApplicationData::RunApplication — which is why the connection flags, the
// manifest handshake for the web server and the debug port offset all come for
// free. What the MENU does around that door is ask questions: it saves a
// modified configuration first, and it refuses to start a second debug session.
// Those become a refusal and an argument here, because a tool has nobody to ask.
//
// ⚠ IT DOES NOT SAVE FOR YOU. The menu offers; this refuses. A launch that
// silently applied pending changes to the database would be the one action in
// this whole surface capable of altering a live base as a SIDE EFFECT of
// something that reads as "start the app" — database_diff says what is pending
// and config_apply is the verb that writes it.
//

#include "backend/mcp/mcpTool.h"

#include "backend/appData.h"
#include "backend/debugger/debugClient.h"     // one debug session at a time — the menu's own guard
#include "backend/metadataConfiguration.h"    // …and the configuration must already be in the base

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgDebug()
{
	static const ibArg s_a(wxT("debug"), ibArg::Kind::Flag,
		_("Start it with the debugger attached, so breakpoints set through debug_breakpoint "
		  "are honoured. Default true - the reason a tool starts an application is usually "
		  "to watch it."));
	return s_a;
}

const ibArg& ArgApplication()
{
	static const ibArg s_a(wxT("application"), ibArg::Kind::Text,
		_("Which one. Default is the thick client."),
			/*required*/ false, { wxT("enterprise"), wxT("wenterprise-server") });
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// app_run
//---------------------------------------------------------------------------
class ibMcpToolAppRun : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("app_run"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString what = ArgApplication().Text(params);
		return wxString::Format(_("starting %s"), what.IsEmpty() ? wxT("the application") : what);
	}

	wxString GetDescription() const override
	{
		return _("Start the application on this base - the thick client or the web server, with "
			"or without the debugger attached. The same launch the designer's Debug menu "
			"performs, so connection flags and the debug port are handled. Refuses when the "
			"configuration has changes the database does not have: read database_diff and apply "
			"them first, because a launch must never write to a base as a side effect.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgApplication(), ArgDebug() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (appData == nullptr) {
			refusal = _("The application is not up.");
			return false;
		}

		wxString application = ArgApplication().Text(params);
		if (application.IsEmpty())
			application = wxT("enterprise");

		if (!application.IsSameAs(wxT("enterprise"), false)
			&& !application.IsSameAs(wxT("wenterprise-server"), false)) {
			refusal = wxString::Format(
				_("'%s' is not something to start. Use enterprise or wenterprise-server."),
				application);
			return false;
		}

		// DEFAULTS TO TRUE, and asked of the field rather than of the value: `debug: false` has
		// to be distinguishable from "not said", or the default silently wins over an explicit no.
		bool withDebug = true;
		if (params.FindField(ArgDebug().Name()) != nullptr)
			withDebug = ArgDebug().Flag(params);

		// ONE DEBUGGER AT A TIME — the same guard the menu keeps. A second debug launch attaches
		// nothing and leaves the caller waiting at a breakpoint that will never be hit.
		if (withDebug && debugClient != nullptr && debugClient->HasConnections()) {
			refusal = _("A debug session is already running. Let it finish, or start without "
				"the debugger.");
			return false;
		}

		// ⚠ THE CONFIGURATION MUST ALREADY BE IN THE BASE. The application reads the database's
		// copy, so launching with unapplied changes runs the OLD configuration while the caller
		// believes it is testing the new one — a failure that looks like a bug in the code just
		// written.
		// ⭐ ASKED OF THE BASE, with nothing to recognise first. IsConfigSave is virtual on
		// ibMetaDataConfigurationBase and the active metadata is a configuration by definition, so
		// the cast to the storage class was a conversion to reach a question already in hand.
		if (!activeMetaData->IsConfigSave()) {
			refusal = _("The configuration has changes the database does not have - the "
				"application would run the old one. database_diff lists them; apply them "
				"first.");
			return false;
		}

		const wxString useWeb = application.Lower();
		const bool manifest = useWeb.IsSameAs(wxT("wenterprise-server"));

		const long pid = appData->RunApplication(useWeb, withDebug, manifest);

		if (pid == 0) {
			refusal = wxString::Format(_("%s could not be started."), application);
			return false;
		}

		result.SetValue(wxT("started"), application);
		result.AddField(wxT("pid"), ibDataValue::Int((s64)pid));
		result.AddField(wxT("debug"), ibDataValue::Bool(withDebug));

		if (withDebug) {
			result.SetValue(wxT("note"),
				_("The debugger attaches as the application comes up; debug_state says when it "
				  "has stopped somewhere."));
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolAppRun);
