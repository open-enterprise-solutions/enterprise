////////////////////////////////////////////////////////////////////////////
//	Description : RUN CODE ON THE RUNTIME - code_run / code_status /
//	              code_cancel. It runs over there as a background JOB, in a
//	              session of its own, WRITING for real.
////////////////////////////////////////////////////////////////////////////
//
// 🛑⭐⭐ THIS ONE CHANGES THE BASE, AND THERE IS NO UNDO. Everything else an assistant can reach here
// either reads, or writes metadata a person reviews before it is applied. This writes DATA, in
// somebody's working base, and a mistake is not a wrong answer on a screen — it is rows that were
// there and are not, or rows that are there and should not be. Say so before using it, every time.
//
// ⭐ THE TRIAL STAGE IS THE SANDBOX, MOVED OUT OF SOMEBODY'S FRAME. Same rule — the code really
// runs and the transaction is thrown away — and the difference is WHERE, not what: a session of its
// own instead of the person's parked one, and therefore no context to see. The committing stage is
// that same run with the throw-away removed.
//
// So "the sandbox with a commit" is right about the second stage and wrong about the road, and the
// road is what matters when something has to be built on it:
//
//                    | debug_sandbox                    | code_run
//   where it runs    | in the person's PARKED FRAME     | in a SESSION OF ITS OWN
//   their session    | the same one - they wait         | untouched - they keep working
//   context          | visible: their locals, their frame| none - there is no frame to see
//   CAN IT BE DEBUGGED| yes, step by step               | NO - there is no frame to stop in
//   what it takes    | a snippet, compiled against the  | a snippet, compiled by the APPLICATION
//                    | parked frame                     | against its own configuration
//   what survives    | nothing, the transaction is undone| whatever it committed
//   the analogy      | a debugger stop                  | a scheduled job
//
// ⭐⭐ AND THE "CAN IT BE DEBUGGED" ROW IS THE SAME FACT AS "they keep working", read the other way
// (Max, 2026-09-06: "filling can run and you will check it - but you will not be able to debug it").
// Parking nobody and having a frame to inspect are one choice, not two features one could have both
// of. So the two columns are not a good tool and a limited one: they are the two answers, and which
// is wanted is decided by whether somebody may be made to wait.
//
// 🛑 BOTH COLUMNS ARE ON THE APPLICATION SIDE. A designer has no runtime at all - its module manager
// is the lightweight one, by design - so neither of these can run in the process the MCP server
// happens to live in. Reading is what hid this: a query needs no runtime, so compose_run answers
// from a designer perfectly well, and the wall is met only by the verb that runs CODE.
//
// ⚠ WHICH MAKES THE ROAD A DEBUGGER COMMAND, not a job started here: sent the way the sandbox is
// sent, but WITHOUT requiring a stop - the shape CommandId_Screenshot already has ("it does not need
// a breakpoint... a running application can do it at any moment"). On that side the frame question
// answers itself, because that process HAS the ibProcUnit.
//
// So these three verbs are a FACE, and the work is ibJobRunByteCode in the application (job/
// jobRunByteCode.h). What is here: the arguments, the CHECK, the knock, and turning an answer into a
// result. What is not here: the compile that matters, the job, the transaction, the token registry -
// all of which belong to the process actually doing the work, which is also why a token outlives
// this connection.
//
// So it does NOT need the runtime parked. It needs one CONNECTED — a runtime is where a session and
// a compiled configuration come from, and that is all it takes from it.
//
// ⭐⭐ TWO STAGES, AND THE SECOND IS ONLY WORTH RUNNING AFTER THE FIRST (Max, 2026-09-06):
//
//   1. `commit: false` — the code runs FOR REAL and is then rolled back. What comes back is what
//      actually happened, not a description of what would: counts, errors, the objects it made. The
//      base is untouched.
//   2. `commit: true`  — the same code, committing.
//
// ⚠ AND THE FIRST STAGE IS A MEASURE, NOT A RITE. It is worth it exactly when there is an EXTERNAL
// SOURCE being interpreted — a file of opening balances, a bank statement — because that is where
// the risk lives: not in the writing, which is ordinary, but in whether the columns were understood.
// "Fill me a hundred counterparties" invents its own data and has nothing to misread; a dry run
// there proves nothing and costs time.
//
// ⚠⚠ WHAT A ROLLED-BACK RUN DOES **NOT** PROVE. It returns the DATA and nothing else: numbers a
// sequence handed out may stay spent, anything done outside the transaction (a file written, a
// request sent) is not undone, and a clash with somebody else's write only appears at the COMMIT
// this stage does not reach. It answers "did I read the file right", which is the question worth
// asking; it does not answer "will the write go through while people are working".
//
// ⚠⚠ ONE TRANSACTION FOR THE WHOLE RUN, and that is a TRADE, not an oversight. It is what makes the
// trial stage possible at all — a run that committed as it went could not be thrown away — and the
// price is that a long run holds its locks for its whole length, which people working in the same
// base will feel. So a large load is split across several CALLS rather than batched inside one.
//
// (An earlier draft of this comment said batching was the running code's business. It is not, and
// could not be: code committing its own batches would keep exactly what stage one exists to discard.)
//
// Cancellation is cooperative — the interpreter unwinds at a loop boundary — so sent code must
// loop over its records to be interruptible at all. That belongs in the tool's DESCRIPTION, not only
// here: it is a requirement on whoever writes the code, and they read the description.

#include "frmserver/mcp/mcpTool.h"

#include "frmserver/mcp/mcpDebugBridge.h"   // the run is asked for over the debugger's wire
#include "backend/debugger/debugClient.h" // …and there has to BE one to ask over
#include "core/startProcess.h"            // ibProcessExists — the application app_run started, still there

#include "backend/compiler/scriptCheck.h"     // the code is CHECKED here, before anything is sent
#include "backend/session/session.h"          // ibSession::Current — whose worker the check is handed to
#include "backend/session/workerPool.h"       // …on the thread that owns what it is checked against
#include "backend/metadataConfiguration.h"    // activeMetaData - whose configuration it checks against
#include "backend/system/systemEnum.h"        // ibStatusMessage - the level of what the code said
#include "backend/job/jobManager.h"           // ibJobManager::kKeptBackgroundJobs - how many ended runs are answered for
#include "frmserver/docView/templates/designer/docViewProfile.h"   // ShowProfile - a measurement, as a tab of the person's

#include <algorithm>
#include <map>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// ⭐⭐ WHAT RUNS IS CODE THE CALLER WROTE, and it took three answers to get there.
//
// THE FACT, which never changed: this runs in a background session with NOBODY STOPPED anywhere, so
// there is no frame on that side for a snippet to compile against a PERSON'S scope. The sandbox has
// one — it borrows the parked person's — and that is exactly what this refuses to do.
//
// THE FIRST CONCLUSION was "so it has to be a NAMED PROCEDURE of a common module". That cost more
// than it looked: the name had to be matched over there against every registered module in turn, and
// a run could not be TRIED without first becoming part of somebody's configuration.
//
// ⭐⭐ THE THIRD ANSWER: there is a scope to compile against over there — the configuration's ROOT —
// and the application can use it. So the TEXT crosses and the application compiles it against its
// own configuration, and nothing has to agree between two processes. This side compiles it too, but
// only to CHECK it, which is where the line numbers come from.
//
// ⭐ AND THE NAMED CASE IS EXPRESSIBLE IN THIS ONE — `StockManagement.LoadBalances();` is a line of
// code — where this one was not expressible in that. So there is one road rather than two. Writing
// the code as a common module is still the better form when somebody will read it or run it again
// next month; it is now a decision about the configuration instead of a toll on trying anything.
const ibArg& ArgText()
{
	static const ibArg s_a(ibT("text"), ibArg::Kind::Text,
		ibMcpText("The code to run - whole statements, in the configuration's own dialect "
			  "(platform_state says which). It is CHECKED HERE before anything is sent, so a "
			  "mistake in it comes back as a refusal with line numbers and never reaches the base "
			  "at all.\n"
			  "\n"
			  "It can call anything the configuration has: `StockManagement.LoadOpeningBalances();` "
			  "is a perfectly good run, and so is fifty lines that select documents and repost "
			  "them. Writing the code as a common module first is still worth doing when somebody "
			  "will want to read it or run it again next month - but it is a choice about the "
			  "configuration, not a toll on trying something once.\n"
			  "\n"
			  "TO HAND SOMETHING BACK, ASSIGN IT TO `Result` - `Result = someValue;` - and it comes "
			  "back under `result` when the run finishes. `Return` does not work here and cannot: "
			  "this is a module body, not a function. Without it a run has no answer at all, and the "
			  "only way to see what it worked out is to have it write journal lines and read them "
			  "back afterwards."), /*required*/ true);
	return s_a;
}

const ibArg& ArgSaid()
{
	static const ibArg s_a(ibT("said"), ibArg::Kind::Text,
		ibMcpText("One line saying what this run is doing, for the ACTIVITY LINE the person sees in "
			  "Active Users - 'loading January opening balances', 'making 200 test documents'. "
			  "Ad-hoc code has no name of its own, so without this they are shown a run with "
			  "nothing to identify it, in their own base, that they may want to stop."));
	return s_a;
}

const ibArg& ArgCommit()
{
	static const ibArg s_a(ibT("commit"), ibArg::Kind::Flag,
		ibMcpText("FALSE (the default) runs the code for real and then UNDOES it - the base is unchanged "
			  "and what comes back is what actually happened. TRUE keeps it, and NOTHING can undo that. "
			  "Run false first whenever a file is being interpreted; skip it when the data is invented "
			  "here and there is nothing to misread."));
	return s_a;
}

// ⭐⭐ THE KNOCK ON THE DOOR. Not a confirmation - a confirmation is a box you tick, and a box gets
// ticked without looking. This asks for a SENTENCE: what, in your own words, this run is about to
// change. You cannot write it without having looked, and you can be visibly WRONG in it, which is
// the whole difference.
//
// ⭐ And it is not private. What is written here is what the person at the designer READS while it
// happens (GetActivity), so it is also how they catch a run that was not what they meant - in the
// assistant's own account of it, before the rows move.
const ibArg& ArgUnderstood()
{
	static const ibArg s_a(ibT("understood"), ibArg::Kind::Text,
		ibMcpText("REQUIRED to commit. Say - to yourself, in your own words - that you know this run "
			  "changes data and that nothing undoes it.\n"
			  "\n"
			  "IT IS YOUR SIGNATURE, NOT A FORM FOR SOMEBODY ELSE. The point is not that a person "
			  "reads it (they do, and that is useful); the point is that you cannot get here without "
			  "STOPPING and stating what you are about to do. Nine harmless calls in a row build "
			  "momentum, and momentum is what carries an assistant through the tenth without looking. "
			  "This is the one step that cannot be taken on momentum.\n"
			  "\n"
			  "Naming the objects, roughly how many, and what becomes of what is already there is what "
			  "makes it worth the stop - and what makes a misunderstanding visible while it still "
			  "costs nothing. The gate does not grade it; it only refuses to move without it."));
	return s_a;
}

// ⭐ THE CODE MEASURED, instead of timings written into it (Max, 2026-11-14: *"you just run the procedure, say you
// need a measurement, and right after it finishes you get the detailed analytics of what ran how long"*).
const ibArg& ArgProfile()
{
	static const ibArg s_a(ibT("profile"), ibArg::Kind::Flag,
		ibMcpText("Measure the code while it runs. Once it has finished, code_status answers with `profile` beside "
			  "`result`: which lines of which modules - the code's own and everything it called - took how long, "
			  "the slowest first, and the same added up by procedure; each with its own time (`selfMs`), its time "
			  "with what it called (`totalMs`) and its `percent` of the time script ran. Under a line are the "
			  "queries it ran (`queries`: `source`, `count`, `totalMs`, `databaseMs`, `memoryMs`, `statements`) "
			  "and what it sent the database outside a query (`statements`: the `text` as sent, its `plan`, the "
			  "`dbms`, `count`, `rows`, `runMs`, `fetchMs`, and its slowest run - `slowestMs` and the `values` it "
			  "was sent with, as SQL writes them, in the order of its placeholders); a connection the pool opened is "
			  "a statement `CONNECT`, a wait while every one was busy `WAIT FOR A FREE CONNECTION`; the platform's own "
			  "work, done for no line, is a line marked `platform`. Use it instead of writing timings into the code. A "
			  "debugged application runs slower than a free one, evenly: read the shares rather than the absolute "
			  "figures."));
	return s_a;
}

const ibArg& ArgLimit()
{
	static const ibArg s_a(ibT("limit"), ibArg::Kind::Whole,
		ibMcpText("How many of each list at most - calls, lines (each call's too), procedures, queries, statements - "
			  "the slowest first. Default 50."));
	return s_a;
}

// ⭐ SHOWN WHEN ASKED (Max, 2026-11-14: *"a parameter that does not oblige - you may use the profiler for yourself, or to
// show me"*): a measurement read for the assistant's own work stays in the answer.
const ibArg& ArgShow()
{
	static const ibArg s_a(ibT("show"), ibArg::Kind::Flag,
		ibMcpText("Also open what was measured as a tab in the person's window - the same tree their own Debug > "
			  "Performance measurement opens. Leave it out when the measurement is for your own reading."));
	return s_a;
}

const ibArg& ArgCall()
{
	static const ibArg s_a(ibT("call"), ibArg::Kind::Text,
		ibMcpText("Only the lines one client call ran, by its method as `calls` names it (`event`, `fetch`...)."));
	return s_a;
}

const ibArg& ArgModule()
{
	static const ibArg s_a(ibT("module"), ibArg::Kind::Text,
		ibMcpText("Only the lines of modules whose name has this in it, any case."));
	return s_a;
}

const ibArg& ArgProcedure()
{
	static const ibArg s_a(ibT("procedure"), ibArg::Kind::Text,
		ibMcpText("Only the lines of this procedure, any case."));
	return s_a;
}

const ibArg& ArgQuery()
{
	static const ibArg s_a(ibT("query"), ibArg::Kind::Text,
		ibMcpText("Only the lines that ran a query whose source, or sent a statement whose text, has this in it, any "
			  "case - and of each line only those queries and statements."));
	return s_a;
}

// What it measured, as a tab of the person's window — on the session's worker, where its frame is.
void ShowProfile(const ibProfileData& profile)
{
	if (ibSession* const session = ibSession::Current())
		(void)session->Submit([profile]() { ibProfileDocument::ShowProfile(profile); });
}

const ibArg& ArgSession()
{
	static const ibArg s_a(ibT("session"), ibArg::Kind::Text,
		ibMcpText("The run to ask about, by the `session` code_run gave back - which is the run's own "
			  "row in Active Users and the id every one of its journal lines carries. So the same "
			  "string reaches it three ways: here, journal_read {session} for what it is doing, and "
			  "the person's own Active Users if they want to stop it themselves.\n"
			  "It names a run in the APPLICATION, not in this conversation - so it still answers "
			  "after a reconnect, and a run somebody else started can be asked after with theirs. It "
			  "dies when the application does.\n"
			  "AN ENDED RUN IS ANSWERED FOR A WHILE, NOT FOR EVER: the application keeps the runs that are "
			  "going and the last ") + ibString::Format(ibT("%zu"), ibJobManager::kKeptBackgroundJobs)
			+ ibMcpText(" that ended (its policy on ended runs). One that dropped out of them answers as unknown, "
			  "the way a run that never existed does - its journal lines, read by the same session id, stay."),
		/*required*/ true);
	return s_a;
}

// ⭐⭐ THE RUN IS NOT KEPT HERE, AND THERE IS NO REGISTRY ON THIS SIDE. The id names a run in the
// APPLICATION, which is where the run lives — so it survives this connection, and a second assistant
// (or the same one after a reconnect) can still ask after a run somebody else started. A map here
// would have made it mean "a run I personally launched", which is not what it is for.
//
// ⚠ THE ONE CONSEQUENCE WORTH KNOWING: it dies with the application, not with this session.
// Restarting the application forgets every run, and the tools say so rather than answering "unknown".

// The bridge, or a refusal that says which of the two things is missing — there is no debugger in
// this process at all, or nothing is attached to one.
ibMcpDebugBridge* JobBridge(ibString& refusal)
{
	if (debugClient == nullptr) {
		refusal = ibMcpText("This process has no debugger client, so there is no application to run "
			"code in.");
		return nullptr;
	}

	ibMcpDebugBridge* const bridge = ibMcpDebug();
	if (bridge == nullptr) {
		refusal = ibMcpText("The assistant is not attached to the debugger, so there is nothing to send "
			"the code to.");
		return nullptr;
	}

	// Asked of the wire, an absent application read as one that "did not answer in time", in milliseconds.
	//
	// ⭐ AND THE REFUSAL NAMES THE DEBUGGER (#362). The code reaches an application ONLY over the debugger's
	// connection, so one started with `debug: false` is running and still out of reach. "No application is
	// attached - app_run starts one" was read as "start another": app_run without restart sees no debug session
	// and starts a second application beside the first, on the same base.
	if (!bridge->GetStop().m_connected) {
		const long started = ibMcpStartedApplication();
		refusal = started != 0 && ibProcessExists(started)
			? ibMcpText("The application started from here is running without the debugger, and code is sent to "
				"an application only over the debugger's connection. app_run with restart: true brings it back "
				"with the debugger (debug is true unless debug: false is passed); then run the code again.")
			: ibMcpText("No application is attached to the debugger, so there is nothing to run the code in, and a "
				"run dies with the application it ran in. app_run starts one with the debugger attached - its "
				"default; one started with debug: false cannot be reached from here.");
		return nullptr;
	}

	return bridge;
}

// The bridge a measurement is asked over, or why there is none: it measures an application attached to the debugger.
ibMcpDebugBridge* ProfileBridge(ibString& refusal)
{
	ibMcpDebugBridge* const bridge = debugClient != nullptr ? ibMcpDebug() : nullptr;
	if (bridge == nullptr || !bridge->GetStop().m_connected) {
		refusal = ibMcpText("No application is attached to the debugger, so there is nothing to measure. app_run "
			"starts one with the debugger attached - its default.");
		return nullptr;
	}
	return bridge;
}

// ⭐⭐ CHECKED HERE, RUN THERE — and the check is the reason a mistake costs nothing. It is the same
// compile the designer's Syntax control button performs (`ibCheckScript`), parented to the
// configuration's module manager, so what it judges the code against is what the code will actually
// see. A failure comes back as DIAGNOSTICS — line, position, message — which become the refusal,
// and nothing crosses the wire.
//
// ⚠ IT IS A CHECK AND NOT THE COMPILE THAT MATTERS. The application compiles the text again, against
// ITS OWN configuration, and that is the one whose result runs.
//
// ⚠ THE COMPILING IS NOT DONE HERE — `ibCheckScript` already does exactly this, and doing it a
// second time would be a second answer to "what does a snippet see". It parents the compile to the
// configuration's own module manager, which is what puts `Message`, `Query`, `ValueTable`, the
// metatype collections and every common module in scope; and it carries the trap that road already
// paid for — the parent has to have been COMPILED, or the snippet is judged against an empty world.
// Asking it for the bytecode as well was one line there (Max, 2026-09-06: *"you can reuse it — my
// goal is that you can not just check syntax but get bytecode out"*).
//
// ⭐ SO THE DIAGNOSTICS ARE THE COMPILER'S OWN, with the line and position in them, rather than a
// sentence this file invents about a failure it did not witness.
//
// 🛑 ON THE THREAD THAT OWNS WHAT IT IS CHECKED AGAINST. Before it judges the snippet, ibCheckScript compiles
// the configuration's module manager — the designer's own, which its code editor uses as the session's task —
// and this tool runs on an MCP connection's thread (it waits for the application; see IsSessionTask). Two
// runs asked at once compiled that one module on two threads: one cleared the bytecode the other was reading,
// and the designer went down (2026-10-01, a dump with ibByteCode::Reset under ibCheckScript on an httplib
// worker). So the check is handed to the session's worker and waited for; the wait for the APPLICATION that
// follows stays on this thread, where it has to. Its state is shared, not borrowed from this frame: a check the
// worker reaches after we stopped waiting writes into its own.
bool CheckCode(const ibString& text, ibString& refusal)
{
	struct ibCheckCall {
		ibString                  m_text;
		std::vector<ibDiagnostic> m_found;
	};
	auto call = std::make_shared<ibCheckCall>();
	call->m_text = text;
	const auto check = [call]() { call->m_found = ibCheckScript(call->m_text, ibT("JobCode"), activeMetaData); };

	ibSession* const session = ibSession::Current();
	ibWorkerPool* const pool = session != nullptr ? session->GetWorkerPool() : nullptr;
	if (pool == nullptr)
		check();
	else {
		std::future<void> done = pool->Submit(session, check);
		// The same five minutes RunTool gives a main-thread call, for the same reason: a modal dialog is a
		// main loop busy until a person clicks, and a run is better refused than wedged behind it.
		if (done.wait_for(std::chrono::minutes(5)) != std::future_status::ready) {
			refusal = ibMcpText("The designer did not get to checking that code within five minutes - it is most "
				"likely waiting behind a dialog. Nothing was sent.");
			return false;
		}
		done.get();
	}

	const std::vector<ibDiagnostic>& found = call->m_found;
	if (found.empty())
		return true;

	ibString said;
	for (const ibDiagnostic& one : found)
		said += ibString::Format(ibT("\n  line %d: %s"), (int)one.m_line, one.m_message);
	refusal = ibString::Format(
		ibMcpText("That code did not compile, so NOTHING was sent - no application was asked to do "
			  "anything and nothing was written:%s"),
		said);
	return false;
}

// ⭐ A MEASUREMENT AS AN ASSISTANT READS IT (ibProfileData): the clients' calls by method — how often, how long, what of
// it their lines did not take, the bytes both ways — each with the lines it ran; the lines of no call; and the
// procedures the time went to, every line's added. The slowest first and the first `limit` of each, every line with its
// share of the time script ran — the sum of every line's own. Under a line are the queries it ran — how long, how much
// of it in the database and how much in memory — and the statements the database got, with their plans; the platform's
// own work, done for no line, is a line of no module (`platform`).
void DescribeProfile(const ibProfileData& measured, std::size_t limit, ibDataNode& into)
{
	using ibRow = ibProfileData::ibProfileRow;

	std::vector<const ibRow*> every;   // of a call or of none
	for (const ibProfileData::ibProfileCall& call : measured.m_calls) {
		for (const ibRow& line : call.m_rows)
			every.push_back(&line);
	}
	for (const ibRow& line : measured.m_rows)
		every.push_back(&line);

	std::int64_t spent = 0;
	for (const ibRow* line : every)
		spent += line->m_self;

	std::map<std::pair<ibString, ibString>, ibRow> byProcedure;
	for (const ibRow* line : every) {
		ibRow& procedure = byProcedure[std::make_pair(line->m_name, line->m_procedure)];
		procedure.m_name      = line->m_name;
		procedure.m_procedure = line->m_procedure;
		procedure.m_self  += line->m_self;
		procedure.m_total += line->m_total;
	}
	std::vector<ibRow> procedures;
	for (const auto& [key, procedure] : byProcedure)
		procedures.push_back(procedure);
	std::stable_sort(procedures.begin(), procedures.end(),
		[](const ibRow& a, const ibRow& b) { return a.m_self > b.m_self; });

	const auto milliseconds = [](std::int64_t microseconds) {
		return ibDataValue::Number(ibNumber(static_cast<long long>(microseconds)) / ibNumber(1000));
	};
	const auto percent = [spent](std::int64_t microseconds) {
		return ibDataValue::Number(spent > 0
			? (ibNumber(static_cast<long long>(microseconds)) * ibNumber(100) / ibNumber(static_cast<long long>(spent))).Round(1)
			: ibNumber());
	};
	const auto statementsOf = [&](const std::vector<ibQueryDebugData::ibStatement>& statements) {
		std::vector<ibDataValue> written;
		for (std::size_t i = 0; i < statements.size() && i < limit; i++) {
			const ibQueryDebugData::ibStatement& statement = statements[i];
			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(ibT("text"), statement.m_text);
			if (!statement.m_plan.IsEmpty())
				node->SetValue(ibT("plan"), statement.m_plan);
			node->SetValue(ibT("dbms"), statement.m_dbms);
			node->AddField(ibT("count"), ibDataValue::UInt(statement.m_count));
			node->AddField(ibT("rows"), ibDataValue::UInt(statement.m_rows));
			node->AddField(ibT("runMs"), milliseconds(statement.m_run));
			node->AddField(ibT("fetchMs"), milliseconds(statement.m_fetch));
			node->AddField(ibT("slowestMs"), milliseconds(statement.m_slowest));
			if (!statement.m_values.empty()) {
				std::vector<ibDataValue> values;
				for (const ibString& value : statement.m_values)
					values.push_back(ibDataValue::String(value));
				node->AddField(ibT("values"), ibDataValue::Array(values));
			}
			written.push_back(ibDataValue::Child(node));
		}
		return written;
	};
	const auto queriesOf = [&](const ibQueryDebugData& queries, ibDataNode& node) {
		std::vector<ibDataValue> written;
		for (std::size_t i = 0; i < queries.m_queries.size() && i < limit; i++) {
			const ibQueryDebugData::ibQuery& query = queries.m_queries[i];
			std::shared_ptr<ibDataNode> one = std::make_shared<ibDataNode>();
			one->SetValue(ibT("source"), query.m_source);
			one->AddField(ibT("count"), ibDataValue::UInt(query.m_count));
			one->AddField(ibT("totalMs"), milliseconds(query.m_total));
			one->AddField(ibT("databaseMs"), milliseconds(query.m_database));
			one->AddField(ibT("memoryMs"), milliseconds(std::max<std::int64_t>(query.m_total - query.m_database, 0)));
			if (!query.m_statements.empty())
				one->AddField(ibT("statements"), ibDataValue::Array(statementsOf(query.m_statements)));
			written.push_back(ibDataValue::Child(one));
		}
		if (!written.empty())
			node.AddField(ibT("queries"), ibDataValue::Array(written));
		if (!queries.m_statements.empty())
			node.AddField(ibT("statements"), ibDataValue::Array(statementsOf(queries.m_statements)));
	};
	const auto said = [&](const std::vector<ibRow>& rows, bool asLines) {
		std::vector<ibDataValue> written;
		for (std::size_t i = 0; i < rows.size() && i < limit; i++) {
			const ibRow& one = rows[i];
			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			if (one.m_name.IsEmpty())
				node->SetValue(ibT("platform"), true);
			else
				node->SetValue(ibT("module"), one.m_name);
			if (!one.m_procedure.IsEmpty())
				node->SetValue(ibT("procedure"), one.m_procedure);
			if (asLines) {
				node->AddField(ibT("line"), ibDataValue::UInt(one.m_line));
				node->AddField(ibT("count"), ibDataValue::UInt(one.m_count));
			}
			node->AddField(ibT("selfMs"), milliseconds(one.m_self));
			node->AddField(ibT("totalMs"), milliseconds(one.m_total));
			node->AddField(ibT("percent"), percent(one.m_self));
			if (asLines)
				queriesOf(one.m_queries, *node);
			written.push_back(ibDataValue::Child(node));
		}
		return written;
	};

	bool cut = measured.m_calls.size() > limit || measured.m_rows.size() > limit || procedures.size() > limit;
	std::vector<ibDataValue> calls;
	for (std::size_t i = 0; i < measured.m_calls.size() && i < limit; i++) {
		const ibProfileData::ibProfileCall& call = measured.m_calls[i];
		std::int64_t lines = 0;
		for (const ibRow& line : call.m_rows)
			lines += line.m_self;
		cut = cut || call.m_rows.size() > limit;

		std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
		node->SetValue(ibT("method"), call.m_method);
		node->AddField(ibT("count"), ibDataValue::UInt(call.m_count));
		node->AddField(ibT("totalMs"), milliseconds(call.m_total));
		node->AddField(ibT("selfMs"), milliseconds(std::max<std::int64_t>(call.m_total - lines, 0)));
		node->AddField(ibT("requestBytes"), ibDataValue::UInt(call.m_requestBytes));
		node->AddField(ibT("answerBytes"), ibDataValue::UInt(call.m_answerBytes));
		if (!call.m_rows.empty())
			node->AddField(ibT("lines"), ibDataValue::Array(said(call.m_rows, true)));
		calls.push_back(ibDataValue::Child(node));
	}

	into.AddField(ibT("durationMs"), milliseconds(measured.m_duration));
	into.AddField(ibT("measuredMs"), milliseconds(spent));
	if (!calls.empty())
		into.AddField(ibT("calls"), ibDataValue::Array(calls));
	into.AddField(ibT("lines"), ibDataValue::Array(said(measured.m_rows, true)));
	into.AddField(ibT("procedures"), ibDataValue::Array(said(procedures, false)));
	if (cut)
		into.SetValue(ibT("note"), ibString::Format(ibMcpText("The slowest %zu of each list, of %zu lines measured; "
			"profile_read narrows the measurement to a call, a module, a procedure or a query."), limit, every.size()));
}

// What profile_read narrows a measurement to: the lines of one call, of one module, of one procedure, and those that ran
// a query — each keeping only its queries and statements that have it. A call none of whose lines is left goes too.
ibProfileData Narrowed(const ibProfileData& profile, const ibString& call, const ibString& module, const ibString& procedure,
	const ibString& query)
{
	using ibRow = ibProfileData::ibProfileRow;

	const ibString moduleText = module.Lower();
	const ibString queryText  = query.Lower();
	const auto has = [&queryText](const ibString& text) { return text.Lower().Contains(queryText); };

	const auto narrowed = [&](const std::vector<ibRow>& rows) {
		std::vector<ibRow> kept;
		for (const ibRow& row : rows) {
			if (!moduleText.IsEmpty() && !row.m_name.Lower().Contains(moduleText))
				continue;
			if (!procedure.IsEmpty() && !row.m_procedure.IsSameAs(procedure, false))
				continue;
			ibRow line = row;
			if (!queryText.IsEmpty()) {
				line.m_queries = ibQueryDebugData();
				for (const ibQueryDebugData::ibQuery& one : row.m_queries.m_queries) {
					ibQueryDebugData::ibQuery found = one;
					if (!has(one.m_source)) {
						found.m_statements.clear();
						for (const ibQueryDebugData::ibStatement& statement : one.m_statements) {
							if (has(statement.m_text))
								found.m_statements.push_back(statement);
						}
						if (found.m_statements.empty())
							continue;
					}
					line.m_queries.m_queries.push_back(found);
				}
				for (const ibQueryDebugData::ibStatement& statement : row.m_queries.m_statements) {
					if (has(statement.m_text))
						line.m_queries.m_statements.push_back(statement);
				}
				if (line.m_queries.IsEmpty())
					continue;
			}
			kept.push_back(line);
		}
		return kept;
	};
	const bool linesNarrowed = !moduleText.IsEmpty() || !procedure.IsEmpty() || !queryText.IsEmpty();

	ibProfileData result;
	result.m_duration = profile.m_duration;
	for (const ibProfileData::ibProfileCall& one : profile.m_calls) {
		if (!call.IsEmpty() && !one.m_method.IsSameAs(call, false))
			continue;
		ibProfileData::ibProfileCall kept = one;
		kept.m_rows = narrowed(one.m_rows);
		if (linesNarrowed && kept.m_rows.empty())
			continue;
		result.m_calls.push_back(kept);
	}
	if (call.IsEmpty())
		result.m_rows = narrowed(profile.m_rows);
	return result;
}

// ⭐ WHAT A RUN SAYS ABOUT ITSELF, in one shape, so all three verbs answer alike. It is the far
// end's own words throughout: this side adds nothing and interprets nothing.
void DescribeRun(const ibJobRunByteCodeState& state, ibDataNode& into)
{
	// ⭐⭐ THE RUN'S ONE NAME. It is a row in Active Users, the id the REGISTRATION JOURNAL is filtered
	// by, and what code_status / code_cancel are addressed with. A background session cannot send
	// anybody a message — it is tied to no one's session — so the journal, read by this id, is where
	// a run says what it is doing.
	if (!state.m_session.IsEmpty())
		into.SetValue(ibT("session"), state.m_session);
	into.SetValue(ibT("complete"), state.m_complete);
	// …WHERE AND HOW LONG: the process doing it (app_run's `pid`) and its own clock, start to end — or to now.
	if (state.m_pid != 0) {
		into.AddField(ibT("pid"), ibDataValue::Int(static_cast<std::int64_t>(state.m_pid)));
		into.AddField(ibT("milliseconds"), ibDataValue::Int(state.m_milliseconds));
	}
	// ⭐ THE SAME LINE THE PERSON READS. A run says where it has got to in ONE place - its activity,
	// which is also written to sys_session.currentActivity and shows in Active Users. So what comes
	// back here and what is on their screen are the same sentence, and neither can drift from the
	// other. (There is no separate progress: a second way to say the same thing would be a second
	// road obliged to agree with the first.)
	if (!state.m_activity.IsEmpty())
		into.SetValue(ibT("activity"), state.m_activity);
	if (!state.m_error.IsEmpty())
		into.SetValue(ibT("error"), state.m_error);
	else if (state.m_complete && !state.m_result.IsEmpty())
		into.SetValue(ibT("result"), state.m_result);

	// ⭐ WHAT THE CODE SAID — its `Message` lines, the last of them, in the shape debug_state gives an
	// application's: what a person running it in a window would have read.
	std::vector<ibDataValue> said;
	for (const ibJobRunByteCodeState::ibLine& line : state.m_output) {
		std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
		node->SetValue(ibT("text"), line.m_text);
		node->SetValue(ibT("level"), ibString(
			  line.m_status == ibStatusMessage_Error   ? ibT("error")
			: line.m_status == ibStatusMessage_Warning ? ibT("warning")
			                                           : ibT("info")));
		said.push_back(ibDataValue::Child(node));
	}
	if (!said.empty())
		into.AddField(ibT("output"), ibDataValue::Array(said));

	// …AND WHAT IT MEASURED, when asked to (code_run {profile}) and finished.
	if (state.m_profile) {
		std::shared_ptr<ibDataNode> profile = std::make_shared<ibDataNode>();
		DescribeProfile(*state.m_profile, 50, *profile);
		into.AddField(ibT("profile"), ibDataValue::Child(profile));
	}
}

// ⚠ THREE OUTCOMES, AND A BOOL CANNOT CARRY THEM. `sent` false means no answer came back before the
// deadline - nothing is known, and the run may well be running. `m_accepted` false means the
// application considered the request and REFUSED it, and said why. Only the third is an answer.
bool Answered(bool sent, const ibJobRunByteCodeState& state, ibString& refusal)
{
	if (!sent) {
		refusal = ibMcpNoAnswer(ibMcpText("The application did not answer in time. That is not the same "
			"as 'it did not happen' - if a run was being started, it may be running. code_status "
			"with the token, when you have one, is the only thing that settles it."));
		return false;
	}
	if (!state.m_accepted) {
		refusal = state.m_refusal.IsEmpty()
			? ibMcpText("The application refused, and said nothing about why.")
			: state.m_refusal;
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------
// code_run
//---------------------------------------------------------------------------
class ibMcpToolRunCode : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("code_run"); }

	// 🛑 NOT AS THE SESSION'S TASK — see ibMcpTool::IsSessionTask. All three of these ask the
	// application and WAIT, and the answer comes back through the session's queue: waiting in that
	// queue is waiting for a task behind this one.
	bool IsSessionTask() const override { return false; }

	// ⚠ THE PERSON AT THE DESIGNER READS THIS LINE while it happens. It says what is being done to
	// their base, not what tool is running — it is the sentence they need in order to stop it if it
	// was not what they meant. So it prefers the caller's own one-line account over any wording this
	// file could invent: `said` names the actual job, where a generic "running code" names none.
	ibString GetActivity(const ibDataNode& params) const override
	{
		ibString said = ArgSaid().Text(params);
		if (said.IsEmpty())
			said = ArgUnderstood().Text(params);

		if (!ArgCommit().Flag(params))
			return said.IsEmpty()
				? ibMcpText("running code as a trial - everything written will be undone")
				: ibString::Format(
					ibMcpText("running code as a trial (everything written will be undone): %s"),
					said);
		// Their base, their sentence to read: what the assistant SAYS it is about to do, in the
		// assistant's own words, while there is still time to stop it.
		return said.IsEmpty()
			? ibMcpText("RUNNING CODE - writing to the base for real")
			: ibString::Format(ibMcpText("RUNNING CODE, writing for real: %s"), said);
	}

	ibString GetDescription() const override
	{
		return ibMcpText("RUN ARBITRARY CODE against a live base - the verb for every job that has to "
			"TOUCH DATA rather than describe it. It is one tool because they are one act:\n"
			"  * CREATE objects and WRITE them - catalog items, documents, MOVEMENTS (register "
			"records), constants;\n"
			"  * FILL a base, or POPULATE it with TEST DATA - a hundred counterparties, a year of "
			"documents;\n"
			"  * LOAD or IMPORT OPENING BALANCES and other figures read out of a FILE;\n"
			"  * POST or REPOST DOCUMENTS - a period of them, after a correction to the code that "
			"posts them;\n"
			"  * CHANGE, PROCESS or CORRECT data in bulk - recalculate a field, fix a wrong "
			"reference, renumber, mark for deletion;\n"
			"  * DELETE data - clear test rows, remove what was imported twice.\n"
			"Ask for it in the words of the job - 'fill the base with test data', 'repost January', "
			"'create a hundred customers', 'delete the test documents' - and this is the answer. "
			"There is no separate verb for any of them, and no other tool here writes data at all.\n"
			  "\n"
			"TO ASK THE DATA SOMETHING, THIS IS THE WRONG VERB. compose_run with `query: \"SELECT ...\"` "
			"answers with the rows in ONE call and writes nothing. This one answers with a session id: the "
			"value comes back only through code_status and the journal, and only while the application runs "
			"with the debugger - four calls for one number. Reach for it to CHANGE data, or when the answer "
			"needs the configuration's own code (a common module's function) rather than a query. (Measured "
			"2026-09-15: a day of checks on a payroll base asked this way round, then filed as friction of "
			"the platform.)\n"
			  "\n"
			"DANGEROUS: with commit it writes DATA into the base and nothing can undo it - rows can "
			"be overwritten or deleted and there is no rollback. LEFT OUT, COMMIT IS FALSE: the run "
			"answers as if it wrote everything and nothing stays - test data generated that way is "
			"not in the base afterwards.\n"
			  "\n"
			"WHAT IT RUNS IS THE CODE YOU WRITE - whole statements, in `text`, run on the "
			"APPLICATION'S runtime as a background job. It is checked here first, with the same "
			"compiler the designer's Syntax control uses, so a mistake is answered as a refusal "
			"with line numbers and never reaches the base; the application compiles it against its "
			"own configuration and runs it. Calling something the configuration already has is one "
			"line (`StockManagement.LoadOpeningBalances();`), so naming a procedure is a thing this "
			"does, not a thing it requires.\n"
			  "\n"
			"WRITING IT AS A COMMON MODULE IS STILL THE BETTER FORM when the code matters beyond "
			"today - somebody can read it before it runs, step through it in the debugger, and run "
			"it again next month (metadata_create, module_write, config_save). That is now a "
			"decision about the configuration rather than the price of trying anything once.\n"
			  "\n"
			"TWO STAGES. Run it with commit FALSE first whenever a file is being interpreted: the "
			"code runs for real and is then undone, so what comes back is what actually happened "
			"while the base stays untouched. Read that, then run the same thing with commit true. "
			"Skip the trial only when the data is invented here and there is nothing to misread - "
			"the risk is in understanding a file, not in the writing.\n"
			  "\n"
			"WRITING THE CODE, and these are requirements rather than advice:\n"
			"\n"
			"* LOOP OVER THE RECORDS. Cancelling is cooperative - the flag is raised and the code "
			"unwinds at its next loop boundary - so code that does its work in one unbroken stretch "
			"CANNOT BE STOPPED at all, however long it runs.\n"
			"\n"
			"* DO NOT MANAGE TRANSACTIONS. The whole run is wrapped in one, which is what makes the "
			"trial stage possible: your own commit would keep what the trial is meant to throw away.\n"
			"\n"
			"* SO A HUGE LOAD HOLDS ITS LOCKS for as long as it runs, and people working in the same "
			"base will feel it. Split a large file across several calls rather than sending one that "
			"runs for an hour.\n"
			"\n"
			"* AND IT RUNS ON WHEN YOU STOP WAITING. The run belongs to the application, not to the call "
			"that sent it: a caller that gives up - a script killed, a turn ended - leaves it writing and "
			"holding its transaction. The next run that takes the same numbers (codes, document numbers) "
			"then meets that lock as a DEADLOCK on the numbering table. Stop the first with code_cancel "
			"by its `session` before sending another.\n"
			"\n"
			"* AND A CAUGHT WRITE FAILURE COSTS THE WHOLE RUN. A refused posting or a cancelled write "
			"rolls its level back, and one transaction cannot keep part of itself: catch that error and "
			"carry on, and the very next write fails with 'Cannot commit: this transaction has already "
			"been rolled back', and nothing of the run is kept. To get past a refusal, check the "
			"condition BEFORE writing (the same one the handler checks), or write that document without "
			"posting it.\n"
			"\n"
			"* WRITE TO THE REGISTRATION JOURNAL as you go - what is about to be done, how many rows, "
			"what came of each stage. It is durable, the person can read it too, and journal_read with "
			"the `session` this returns shows exactly this run's rows and nobody else's. THAT IS HOW "
			"ARBITRARY CODE IS CHECKED. `Message` is heard as well, but only by you: a background "
			"session has no window, so the run keeps the last lines it said and code_status answers "
			"with them as `output` - which is how code written for a person's window is checked here.\n"
			  "\n"
			"WHERE IT RUNS, because that decides what you can do about it. It needs a RUNTIME, which a "
			"designer does not have - so the application has to be RUNNING WITH THE DEBUGGER "
			"ATTACHED (app_run with debug true), and the code is sent to that process over the same "
			"wire the sandbox uses. It does NOT need to be stopped at a breakpoint: it runs beside "
			"the person, in a session of its own, and nobody is blocked.\n"
			"\n"
			"AND IT MUST NOT BE STOPPED FOR A COMMIT. While the application stands at a breakpoint, "
			"what is run in it goes through the debugger's sandbox, which rolls EVERYTHING back - the "
			"run comes back complete, without an error, and keeps nothing. A committing run is refused "
			"in that state; debug_state says whether it is stopped, debug_run with action continue lets "
			"it go, and debug_breakpoint with all + remove clears breakpoints nobody needs - a copied "
			"base brings its breakpoints with it, and app_run with debug true then stops in BeforeStart "
			"before you have done anything. After a committing run, READ BACK WHAT IT WROTE (count the "
			"rows): 'complete' says it finished, not that anything was kept.\n"
			  "\n"
			"AND SO IT CANNOT BE STEPPED THROUGH. That is the same fact as 'nobody is blocked', read "
			"the other way: a run that parks nobody has no frame to stop in and no locals to look at. "
			"Your instruments are the trial stage and THE REGISTRATION JOURNAL, read back by the "
			"`session` this answers with - which is why writing to it is a requirement above and not "
			"a nicety. Code that writes nothing to the journal cannot be checked at all: there is no "
			"frame to look into and no message that reaches anyone.\n"
			  "\n"
			"NEED TO STEP THROUGH IT? That is debug_sandbox - the same code at a breakpoint, with the "
			"person's frame and their locals, and their session waiting on you. You cannot have both: "
			"leaving them working and having a frame to inspect are the same choice made twice.\n"
			  "\n"
			"There is no access to anybody's forms here - none exists - so a call that needs one "
			"fails rather than reaching a person's window. Comes back with a token; code_status "
			"watches it, code_cancel stops it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ArgText(), ArgSaid(), ArgCommit(), ArgUnderstood(), ArgProfile() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		const ibString text   = ArgText().Text(params);
		const bool     commit = ArgCommit().Flag(params);

		// ⚠ Strip, not Trim — wxString::Trim MUTATES and so is non-const; this text is not ours to
		// edit and the neighbouring `understood` check only gets away with it on a temporary.
		if (text.Strip(ibString::both).IsEmpty()) {
			refusal = ibMcpText("Nothing to run - `text` is the code itself.");
			return false;
		}

		// ⭐⭐ THE GATE, AND ONLY ON THE HALF THAT KEEPS ANYTHING. A trial is undone, so demanding a
		// statement before one would teach the habit of writing it without meaning it - which is
		// exactly how a safeguard stops working. It is asked once, where it costs something.
		if (commit && ArgUnderstood().Text(params).Trim().IsEmpty()) {
			// ⭐⭐ WHOSE SAFEGUARD THIS IS: THE CALLER'S OWN. It reads like a warning aimed at the
			// person whose base this is, and it does notify them - but that is the consequence, not
			// the purpose. What it actually does is force the CALLER to stop and say what is about to
			// happen. A guard aimed at a person works while they are paying attention; a guard aimed
			// at the one ACTING works against momentum - nine harmless calls in a row, and the tenth
			// goes through unexamined because the previous nine did.
			//
			// ⚠ Which is also why the wording is not judged. An earlier draft demanded objects and
			// counts and offered specimens of "enough", putting the tool in the business of grading
			// prose - and a gate that grades gets argued with, then routed around. It refuses to move
			// without an acknowledgement; what the acknowledgement is worth is read by the person.
			refusal = ibMcpText("Committing changes data and cannot be undone, so it does not start unasked: "
				"pass `understood` saying that you know what this run will change. What you write "
				"is shown to the person whose base this is, in their own window, before a row "
				"moves - so it is worth writing for them rather than for the gate. Unsure what it "
				"will do? Run it with commit false first: the code runs for real and is undone, and "
				"what comes back is what actually happened.");
			return false;
		}

		// 🛑⭐⭐ THE WORK IS NOT DONE HERE, AND CANNOT BE. The MCP server lives in the designer, and a
		// designer builds no runtime for any session — `ibSession::EnsureRoot` returns early on
		// `appData->DesignerMode()`, in as many words ("Designer never executes script"). So a
		// background job started on this side would get a session with nothing to call, every time.
		//
		// ⭐ READING IS THE ASYMMETRY, and it is what hid this for a while. A query needs no runtime,
		// so `compose_run` answers from a designer perfectly well; only the verb that runs CODE meets
		// the wall. The two tools sit side by side and do not share the constraint.
		//
		// So this sends a command to the APPLICATION, the way the sandbox is sent — but without
		// needing a stop, which is the exemption `CommandId_Screenshot` already has. What happens
		// over there is ibJobRunByteCode; what happens here is a request and an answer.
		ibMcpDebugBridge* const bridge = JobBridge(refusal);
		if (bridge == nullptr)
			return false;

		// 🛑⭐⭐ NOT WHILE THE APPLICATION STANDS AT A BREAKPOINT. A stopped process is the debugger's, and
		// what is run in it then goes the debugger's road — the sandbox, which rolls everything back (Max,
		// 2026-09-10: *"if you are in the debugger, in the sandbox, it rolls everything back — and you have
		// a separate mode for that"*). Nothing says so on the way: the run comes back complete, without an
		// error, having kept nothing it was meant to — measured on a thousand employees' payroll, started
		// while a breakpoint the copied base had brought along held the application in BeforeStart. A
		// trial is undone anyway, so only the committing half is refused, and the refusal names the way out.
		if (commit) {
			const ibMcpDebugBridge::Stop stop = bridge->GetStop();
			if (stop.m_stopped) {
				refusal = ibString::Format(
					ibMcpText("The application is stopped at a breakpoint (%s). A run started now goes "
						  "through the debugger's sandbox, which rolls everything back - it would come back "
						  "complete and keep nothing. Let the application go first: debug_run with action "
						  "continue, and debug_breakpoint with all + remove if the stop is a breakpoint "
						  "nobody needs (a copied base brings its breakpoints with it). Then commit."),
					stop.m_where.IsEmpty() ? stop.m_module : stop.m_where);
				return false;
			}
		}

		// ⭐⭐ CHECKED HERE, RUN THERE. This side compiles the code only to JUDGE it — same compiler
		// the designer's Syntax control button uses, so the answer carries line numbers — and then
		// sends the TEXT. Code that does not compile is refused without a byte crossing the wire; and
		// what actually runs is compiled by the application, against the configuration it is running,
		// so there is nothing that has to agree between two processes.
		if (!CheckCode(text, refusal))
			return false;

		ibJobRunRequest request;
		request.m_text       = text;
		request.m_said       = ArgSaid().Text(params);
		request.m_understood = ArgUnderstood().Text(params);
		request.m_commit     = commit;
		request.m_profile    = ArgProfile().Flag(params);

		ibJobRunByteCodeState state;
		const bool sent = bridge->StartJob(request, state);
		if (!Answered(sent, state, refusal))
			return false;

		DescribeRun(state, result);
		result.SetValue(ibT("committing"), commit);
		if (!commit)
			result.SetValue(ibT("note"),
				ibMcpText("A trial: everything this writes will be undone. What comes back is what really "
					  "happened - read it before running the same thing with commit."));

		// ⭐⭐ SAID WITH THE ANSWER, BECAUSE THE SYMPTOM IS AN ANSWER THAT LOOKS FINE. The application
		// read the configuration as it came up and kept it — metadata, bytecode, which registers a
		// document posts to. Change any of that afterwards and the run over there goes on being
		// right for the configuration it is holding: the code executes, nothing raises, and the
		// change simply does not appear. That is the one failure this surface cannot show as a
		// failure, so it is reported beside the result rather than instead of it.
		//
		// ⚠ A NOTE AND NOT A REFUSAL. Reading data, watching a journal, running a query against the
		// base a colleague is using are all perfectly good things to do while a configuration is
		// half-edited here; refusing them would trade a rare wrong belief for a constant obstacle.
		//
		// TWO DIFFERENT STATES, AND THEY NEED DIFFERENT DOORS. Changes the base does not have yet
		// cannot be reached by restarting anything — config_apply first. Changes the base HAS, made
		// after the run started, need the run to come up again.
		// ⭐⭐ ASKED OF THE DEBUGGER, WHICH IS ALREADY HOLDING THE ANSWER. The run this verb speaks
		// to is attached over that wire, and its handshake carries the configuration's GUID and its
		// DIGEST (CommandId_VerifyConnection). The GUID has always been checked — it is the guard
		// that stops two designers on physically different bases from attaching to one another,
		// which is a question about IDENTITY. The digest, arriving in the same message, answers a
		// different one: same configuration, is it the same VERSION. Nothing asked it.
		//
		// 🛑 The first version of this remembered the digest at `app_run` in a static of its own —
		// a second number for a fact already on the wire, and one that could not speak for a run
		// somebody else had started (Max, 2026-09-07: *"the running application is there in the
		// debugger"*).
		ibString running;
		if (debugClient != nullptr) {
			debugClient->ForEachConnection([&running](const auto& connection) {
				if (running.IsEmpty())
					running = connection.GetConfigMD5();
			});
		}

		// ⭐⭐ THE SAME PAIR THE DESIGNER'S OWN CAPTION PAINTS, and asked the same way round — so the
		// window and this answer cannot say different things about one configuration. The metadata
		// pane writes `Configuration *<!>`: the star for `IsEdited()`, edits that exist in THIS
		// PROCESS and nowhere else; the `<!>` for `!IsConfigSave()`, the database not holding this
		// configuration (mainFrameDesigner.cpp, and the state lives on the metadata precisely so
		// that anything else can reach it).
		//
		// 🛑 BOTH MARKS CAN BE LIT AT ONCE, which is the whole reason the caption paints two of them
		// rather than choosing (Max, 2026-09-07: *"they can both be true — then it is a
		// combination"*). Written here as `if edited … else if …`, the second fact disappeared the
		// moment the first held.
		const bool edited     = activeMetaData != nullptr && activeMetaData->IsEdited();
		const bool notApplied = activeMetaData != nullptr && !activeMetaData->IsConfigSave();

		// …AND THE THIRD CASE, which neither flag can see: nothing pending at all, and the run
		// simply older than the last applying. That comes off the debugger's own handshake, above.
		const bool moved = !running.IsEmpty() && activeMetaData != nullptr
			&& !running.IsSameAs(activeMetaData->GetConfigMD5());

		// EACH FACT ITS OWN CLAUSE. "Edited here" and "the database has not got this configuration"
		// together are the ordinary state of an afternoon's work, not an exotic one — and a reader
		// told only the first would apply their edits and expect to be level, being wrong by however
		// much the base was behind to begin with.
		ibString what;

		if (edited)
			what = ibMcpText("it has been edited in this process and nowhere else");

		if (notApplied) {
			if (!what.IsEmpty())
				what += ibMcpText(", and besides that ");
			what += ibMcpText("the database does not hold this configuration at all");
		}

		if (what.IsEmpty() && moved)
			what = ibMcpText("nothing is pending, but the run came up before the last applying");

		if (!what.IsEmpty()) {
			// ⭐ AND THE DOOR IS CHOSEN BY WHAT IS TRUE, not by which clause was written first. A
			// restart alone is enough only when the base already holds everything; anything pending
			// has to be published before a restart can pick it up.
			result.SetValue(ibT("configuration"), ibString::Format(
				ibMcpText("THE RUN IS NOT WORKING FROM WHAT IS OPEN HERE: %s. An application works "
				  "from what the DATABASE holds and keeps what it read at startup - modules, and "
				  "which registers a document posts to. %s"),
				what,
				(edited || notApplied)
					? ibMcpText("database_diff says what of it reaches the schema, config_apply "
						  "publishes it, and app_run {restart: true} after that.")
					: ibMcpText("app_run {restart: true} brings it up on the current one.")));
		}

		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolRunCode);

//---------------------------------------------------------------------------
// code_status
//---------------------------------------------------------------------------
class ibMcpToolRunCodeStatus : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("code_status"); }

	// 🛑 It waits on the wire like the one above — same reason, same answer.
	bool IsSessionTask() const override { return false; }

	ibString GetActivity(const ibDataNode& /*params*/) const override
	{
		return ibMcpText("checking a code run");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("How a run is getting on: `activity` - where it says it has got to - "
			"plus whether it has finished and what it answered or failed with, and `output` - what its "
			"code said with `Message`, the last lines of it, which a background session shows in no "
			"window. It does not block: a run is meant to be watched while the person carries on "
			"working.\n"
			  "\n"
			"`activity` is THE SAME LINE the person at the designer reads in Active Users, not a "
			"second account of the run written for a caller. So what you read here and what they see "
			"cannot drift apart - and code that reports as it goes is reporting to both of you.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgSession() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMcpDebugBridge* const bridge = JobBridge(refusal);
		if (bridge == nullptr)
			return false;

		// ⚠ ASKED OF THE APPLICATION, because that is where the run is. This side keeps no map of
		// tokens on purpose: a token names a run in the process doing the work, so it still answers
		// after a reconnect, and to a second assistant who did not start it.
		ibJobRunByteCodeState state;
		const bool sent = bridge->JobStatus(ArgSession().Text(params), state);
		if (!Answered(sent, state, refusal))
			return false;

		DescribeRun(state, result);
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolRunCodeStatus);

//---------------------------------------------------------------------------
// code_cancel
//---------------------------------------------------------------------------
class ibMcpToolRunCodeCancel : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("code_cancel"); }

	// 🛑 It waits on the wire like the two above — same reason, same answer.
	bool IsSessionTask() const override { return false; }

	ibString GetActivity(const ibDataNode& /*params*/) const override
	{
		return ibMcpText("stopping a code run");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Stop a background run - one code_run started. COOPERATIVE: the flag is raised and the code unwinds at its "
			"next loop boundary, so code that does not loop cannot be stopped at all - which is why "
			"looping is a requirement on the code and not a style note.\n"
			"IF IT WILL NOT STOP - a run that is wedged, or one written without a loop - the session "
			"it holds can be ENDED OUTRIGHT: session_kick with the same `session`. That is the blunt "
			"instrument and it is there for exactly this; it does not unwind anything, it removes the "
			"session.\n"
			"WARNING: whatever a COMMITTING run had already committed stays committed - stopping is not "
			"undoing, and there is nothing that undoes it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgSession() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMcpDebugBridge* const bridge = JobBridge(refusal);
		if (bridge == nullptr)
			return false;

		ibJobRunByteCodeState state;
		const bool sent = bridge->JobCancel(ArgSession().Text(params), state);
		if (!Answered(sent, state, refusal))
			return false;

		// ⚠ TOO LATE IS AN ANSWER, NOT AN ERROR. The far end raises the flag only on a run that is
		// still going and reports the state either way, so "it had already finished" comes back as a
		// finished run rather than as a refusal — which is what the caller needs to know, and it is
		// not a mistake to have asked.
		result.SetValue(ibT("note"), state.m_complete
			? ibMcpText("It had already finished - nothing to stop. What it ended with is here.")
			: ibMcpText("Asked it to stop; it unwinds at the next loop boundary. Anything already "
				    "committed stays."));
		DescribeRun(state, result);
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolRunCodeCancel);

//---------------------------------------------------------------------------
// profile_start / profile_stop / profile_read
//---------------------------------------------------------------------------
// ⭐ THE PERSON'S MEASUREMENT, from here — the one the designer's Debug → Performance measurement starts: either side
// begins it, either ends it. Ended by the person, it opens in their window; ended here, there only when asked (show) —
// and the last one heard is read again in detail (profile_read).
class ibMcpToolProfileStart : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("profile_start"); }

	ibString GetActivity(const ibDataNode& /*params*/) const override
	{
		return ibMcpText("starting a performance measurement of the application");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Begin a PERFORMANCE MEASUREMENT of the running application - every session of it, the "
			"person's clicks and background jobs alike - the same one the designer's Debug > Performance "
			"measurement starts. Every client call and every line of script that runs is timed until profile_stop, "
			"which answers with the calls, lines and procedures that took the time.\n"
			"To measure ONE PIECE OF CODE, code_run with `profile` is the shorter road: it measures that run "
			"alone and answers with it when it finishes.\n"
			"The application has to be running with the debugger attached (app_run, debug true - its default). "
			"A started measurement runs until it is stopped; starting one while one runs leaves that one going.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments;
		return s_arguments;
	}

	bool Call(const ibDataNode& /*params*/, ibDataNode& result, ibString& refusal) const override
	{
		ibMcpDebugBridge* const bridge = ProfileBridge(refusal);
		if (bridge == nullptr)
			return false;

		bridge->StartProfile();
		result.SetValue(ibT("started"), true);
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolProfileStart);

class ibMcpToolProfileStop : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("profile_stop"); }

	// 🛑 It waits on the wire like code_status — same reason, same answer.
	bool IsSessionTask() const override { return false; }

	ibString GetActivity(const ibDataNode& /*params*/) const override
	{
		return ibMcpText("ending the performance measurement");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("End the measurement profile_start - or the person's Debug > Performance measurement - "
			"began, and answer with what it measured: `durationMs` from its start to now, `measuredMs` - the "
			"time script actually ran - then `calls`, `lines` and `procedures`, the slowest first.\n"
			"`calls` are the clients' calls by method (`event`, `fetch`, `execute`...): how often (`count`), how "
			"long (`totalMs`), how much of it their lines did not take (`selfMs` - the request read, the answer "
			"written, the forms' own work), the bytes both ways (`requestBytes`, `answerBytes`), and the `lines` "
			"the call ran. `lines` beside them are the lines of no call - a background job's, code_run's.\n"
			"A line or a procedure has `selfMs` (its own time), `totalMs` (with what it called) and `percent` of "
			"the measured time; a line also says how often it ran (`count`), the queries it ran (`queries`: "
			"`source`, `count`, `totalMs`, `databaseMs` - spent in the database, `memoryMs` - in memory, and "
			"their `statements`) and what it sent the database outside a query (`statements`: the `text` as sent, "
			"its `plan`, the `dbms`, `count`, `rows`, `runMs`, `fetchMs`, and its slowest run - `slowestMs` and "
			"the `values` it was sent with, as SQL writes them, in the order of its placeholders: that run can be "
			"sent again). A connection the pool opened is a statement `CONNECT`, a wait while every connection was "
			"busy `WAIT FOR A FREE CONNECTION`, under the line or query that asked. The platform's own work, done "
			"for no line - posting's writes, say - is a line marked `platform`. profile_read reads the same "
			"measurement again, narrowed.\n"
			"The clock is the wall's: a line the application stood on - at a breakpoint, or waiting for the "
			"person to answer a question - is given the wait. A debugged application runs slower than a free one, "
			"evenly: read the shares rather than the absolute figures.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgLimit(), ArgShow() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMcpDebugBridge* const bridge = ProfileBridge(refusal);
		if (bridge == nullptr)
			return false;

		ibProfileData measured;
		bool          running = false;
		if (!bridge->StopProfile(measured, running)) {
			refusal = ibMcpNoAnswer(ibMcpText("The application did not answer in time; the measurement may still "
				"be running. profile_stop again asks again."));
			return false;
		}
		if (!running) {
			refusal = ibMcpText("No measurement was running - profile_start begins one.");
			return false;
		}

		const std::int64_t limit = ArgLimit().Whole(params);
		DescribeProfile(measured, limit > 0 ? (std::size_t)limit : 50, result);
		if (ArgShow().Flag(params))
			ShowProfile(measured);
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolProfileStop);

class ibMcpToolProfileRead : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("profile_read"); }

	ibString GetActivity(const ibDataNode& /*params*/) const override
	{
		return ibMcpText("reading the last performance measurement");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Read the last measurement again, in the shape profile_stop answers with, narrowed to what "
			"is being looked into: the lines one client call ran (`call`), of one module (`module`), of one "
			"procedure (`procedure`), the lines that ran a query or sent a statement with a given text (`query`) - "
			"any together. The last measurement is the last profile_stop's, the person's own stop's, or the last "
			"code_run {profile} code_status answered with - whichever came last. Nothing is measured again: the "
			"figures are the ones taken then.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ArgCall(), ArgModule(), ArgProcedure(), ArgQuery(), ArgLimit(), ArgShow() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMcpDebugBridge* const bridge = debugClient != nullptr ? ibMcpDebug() : nullptr;
		const std::shared_ptr<const ibProfileData> last = bridge != nullptr ? bridge->GetProfile() : nullptr;
		if (last == nullptr) {
			refusal = ibMcpText("No measurement has been heard here yet - profile_start and profile_stop take one, or "
				"code_run with `profile` and code_status after it.");
			return false;
		}

		const ibProfileData narrowed = Narrowed(*last, ArgCall().Text(params), ArgModule().Text(params),
			ArgProcedure().Text(params), ArgQuery().Text(params));
		const std::int64_t limit = ArgLimit().Whole(params);
		DescribeProfile(narrowed, limit > 0 ? (std::size_t)limit : 50, result);
		if (ArgShow().Flag(params))
			ShowProfile(narrowed);
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolProfileRead);

} // namespace
