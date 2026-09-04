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

#include <wx/process.h>                       // wxProcess::Exists — has the previous run actually gone
#include <wx/utils.h>                         // wxMilliSleep — the wait while it goes

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgDebug()
{
	static const ibArg s_a(wxT("debug"), ibArg::Kind::Flag,
		ibMcpText("Start it with the debugger attached, so breakpoints set through debug_breakpoint "
		  "are honoured. Default true - the reason a tool starts an application is usually "
		  "to watch it."));
	return s_a;
}

const ibArg& ArgApplication()
{
	static const ibArg s_a(wxT("application"), ibArg::Kind::Text,
		ibMcpText("Which one. Default is the thick client."),
			/*required*/ false, { wxT("enterprise"), wxT("wenterprise-server") });
	return s_a;
}

// ⭐⭐ THE VERB THE LOOP WAS MISSING, AND IT IS NOT A NEW MECHANISM. Ending a run has been possible
// all along — `debug_attach {host, port, stop: true}` — but a caller who has just rewritten a module
// is not asking to end anything: they are asking for the NEW code to run, and the ending is a step
// on the way. Measured 2026-09-04: the refusal below named the state and not the door, so a whole
// session's worth of restarts went out to the operating system, and twice the answer that came back
// was produced by the PREVIOUS version of the module — a running application holds the bytecode it
// started with.
const ibArg& ArgRestart()
{
	static const ibArg s_a(wxT("restart"), ibArg::Kind::Flag,
		ibMcpText("End the run that is already going, then start a fresh one. This is what to pass "
		  "after changing a module: a running application keeps the bytecode it started with, so "
		  "the change is invisible until it comes up again."));
	return s_a;
}

// The last run STARTED FROM HERE. Kept so a restart can wait for the process to actually go before
// starting its replacement — two of them on one file base is a lock fight, and the loser is silent.
// Zero when this designer has started nothing, which is honest: a run somebody else started is one
// this cannot watch, and the wait falls back to a fixed one.
long s_lastStartedPid = 0;

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
		return wxString::Format(ibMcpText("starting %s"), what.IsEmpty() ? ibMcpText("the application") : what);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Start the application on this base - the thick client or the web server, with "
			"or without the debugger attached. The same launch the designer's Debug menu "
			"performs, so connection flags and the debug port are handled. Refuses when the "
			"configuration has changes the database does not have: read database_diff and apply "
			"them first, because a launch must never write to a base as a side effect.\n\n"
			"After changing a module, pass restart: true - a running application keeps the "
			"bytecode it came up with, so the new code is invisible until it is started again.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgApplication(), ArgDebug(), ArgRestart() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (appData == nullptr) {
			refusal = ibMcpText("The application is not up.");
			return false;
		}

		wxString application = ArgApplication().Text(params);
		if (application.IsEmpty())
			application = wxT("enterprise");

		if (!application.IsSameAs(wxT("enterprise"), false)
			&& !application.IsSameAs(wxT("wenterprise-server"), false)) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not something to start. Use enterprise or wenterprise-server."),
				application);
			return false;
		}

		// DEFAULTS TO TRUE, and asked of the field rather than of the value: `debug: false` has
		// to be distinguishable from "not said", or the default silently wins over an explicit no.
		bool withDebug = true;
		if (params.FindField(ArgDebug().Name()) != nullptr)
			withDebug = ArgDebug().Flag(params);

		const bool restart = ArgRestart().Flag(params);

		// ONE DEBUGGER AT A TIME — the same guard the menu keeps. A second debug launch attaches
		// nothing and leaves the caller waiting at a breakpoint that will never be hit.
		bool ended = false;
		if (debugClient != nullptr && debugClient->HasConnections()) {

			if (!restart && withDebug) {
				// ⭐ A REFUSAL THAT NAMES THE DOOR. "Let it finish" is advice a tool cannot act on,
				// and it is not even what the caller wants — they want the new code running.
				refusal = ibMcpText("A debug session is already running. Pass restart: true to end it "
					"and come up again with the current configuration - that is what to do after "
					"changing a module, because a running application keeps the bytecode it "
					"started with. (debug_sessions reports the address, and debug_attach with "
					"stop: true ends one without starting anything.)");
				return false;
			}

			if (restart) {
				// ⭐ THE PLATFORM'S OWN ENDING, asked of every connection rather than of a chosen
				// one: DetachConnection is a no-op on a connection that is not attached, so the
				// filtering that would otherwise be written here is already inside it.
				for (auto* connection : debugClient->GetListConnection())
					if (connection != nullptr)
						connection->DetachConnection(/*kill=*/true);

				ended = true;

				// ⚠ AND WAIT FOR IT TO ACTUALLY GO. The command travels to another process, which
				// then unwinds, closes its session and lets go of the base; starting the
				// replacement before that is two processes on one file base — Firebird gives the
				// second one a lock error that reads like a corrupt installation.
				if (s_lastStartedPid != 0) {
					for (int waited = 0; waited < 100 && wxProcess::Exists((int)s_lastStartedPid); ++waited)
						wxMilliSleep(50);
				}
				else {
					wxMilliSleep(1000);   // started by somebody else - there is no pid to watch
				}
			}
		}

		// ⚠ THE CONFIGURATION MUST ALREADY BE IN THE BASE. The application reads the database's
		// copy, so launching with unapplied changes runs the OLD configuration while the caller
		// believes it is testing the new one — a failure that looks like a bug in the code just
		// written.
		// ⭐ ASKED OF THE BASE, with nothing to recognise first. IsConfigSave is virtual on
		// ibMetaDataConfigurationBase and the active metadata is a configuration by definition, so
		// the cast to the storage class was a conversion to reach a question already in hand.
		if (!activeMetaData->IsConfigSave()) {
			refusal = ibMcpText("The configuration has changes the database does not have - the "
				"application would run the old one. database_diff lists them; apply them "
				"first.");
			return false;
		}

		const wxString useWeb = application.Lower();
		const bool manifest = useWeb.IsSameAs(wxT("wenterprise-server"));

		const long pid = appData->RunApplication(useWeb, withDebug, manifest);

		if (pid == 0) {
			refusal = wxString::Format(ibMcpText("%s could not be started."), application);
			return false;
		}

		s_lastStartedPid = pid;

		result.SetValue(wxT("started"), application);
		result.AddField(wxT("pid"), ibDataValue::Int((s64)pid));
		result.AddField(wxT("debug"), ibDataValue::Bool(withDebug));

		if (ended)
			result.AddField(wxT("endedPrevious"), ibDataValue::Bool(true));

		if (withDebug) {
			result.SetValue(wxT("note"),
				ibMcpText("The debugger attaches as the application comes up; debug_state says when it "
				  "has stopped somewhere."));
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolAppRun);
