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

#include "backend/mcp/mcpTool.h"

#include "backend/mcp/mcpDebugBridge.h"   // the run is asked for over the debugger's wire
#include "backend/debugger/debugClient.h" // …and there has to BE one to ask over

#include "backend/compiler/scriptCheck.h"     // the code is CHECKED here, before anything is sent
#include "backend/metadataConfiguration.h"    // activeMetaData - whose configuration it checks against

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
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		ibMcpText("The code to run - whole statements, in the configuration's own dialect "
			  "(platform_state says which). It is CHECKED HERE before anything is sent, so a "
			  "mistake in it comes back as a refusal with line numbers and never reaches the base "
			  "at all.\n"
			  "\n"
			  "It can call anything the configuration has: `StockManagement.LoadOpeningBalances();` "
			  "is a perfectly good run, and so is fifty lines that select documents and repost "
			  "them. Writing the code as a common module first is still worth doing when somebody "
			  "will want to read it or run it again next month - but it is a choice about the "
			  "configuration, not a toll on trying something once."), /*required*/ true);
	return s_a;
}

const ibArg& ArgSaid()
{
	static const ibArg s_a(wxT("said"), ibArg::Kind::Text,
		ibMcpText("One line saying what this run is doing, for the ACTIVITY LINE the person sees in "
			  "Active Users - 'loading January opening balances', 'making 200 test documents'. "
			  "Ad-hoc code has no name of its own, so without this they are shown a run with "
			  "nothing to identify it, in their own base, that they may want to stop."));
	return s_a;
}

const ibArg& ArgCommit()
{
	static const ibArg s_a(wxT("commit"), ibArg::Kind::Flag,
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
	static const ibArg s_a(wxT("understood"), ibArg::Kind::Text,
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

const ibArg& ArgSession()
{
	static const ibArg s_a(wxT("session"), ibArg::Kind::Text,
		ibMcpText("The run to ask about, by the `session` code_run gave back - which is the run's own "
			  "row in Active Users and the id every one of its journal lines carries. So the same "
			  "string reaches it three ways: here, journal_read {session} for what it is doing, and "
			  "the person's own Active Users if they want to stop it themselves.\n"
			  "It names a run in the APPLICATION, not in this conversation - so it still answers "
			  "after a reconnect, and a run somebody else started can be asked after with theirs. It "
			  "dies when the application does."),
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
ibMcpDebugBridge* JobBridge(wxString& refusal)
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
bool CheckCode(const wxString& text, wxString& refusal)
{
	const std::vector<ibDiagnostic> found = ibCheckScript(text, wxT("JobCode"), activeMetaData);
	if (found.empty())
		return true;

	wxString said;
	for (const ibDiagnostic& one : found)
		said += wxString::Format(wxT("\n  line %d: %s"), (int)one.m_line, one.m_message);
	refusal = wxString::Format(
		ibMcpText("That code did not compile, so NOTHING was sent - no application was asked to do "
			  "anything and nothing was written:%s"),
		said);
	return false;
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
		into.SetValue(wxT("session"), state.m_session);
	into.SetValue(wxT("complete"), state.m_complete);
	// ⭐ THE SAME LINE THE PERSON READS. A run says where it has got to in ONE place - its activity,
	// which is also written to sys_session.currentActivity and shows in Active Users. So what comes
	// back here and what is on their screen are the same sentence, and neither can drift from the
	// other. (There is no separate progress: a second way to say the same thing would be a second
	// road obliged to agree with the first.)
	if (!state.m_activity.IsEmpty())
		into.SetValue(wxT("activity"), state.m_activity);
	if (!state.m_error.IsEmpty())
		into.SetValue(wxT("error"), state.m_error);
	else if (state.m_complete && !state.m_result.IsEmpty())
		into.SetValue(wxT("result"), state.m_result);
}

// ⚠ THREE OUTCOMES, AND A BOOL CANNOT CARRY THEM. `sent` false means no answer came back before the
// deadline - nothing is known, and the run may well be running. `m_accepted` false means the
// application considered the request and REFUSED it, and said why. Only the third is an answer.
bool Answered(bool sent, const ibJobRunByteCodeState& state, wxString& refusal)
{
	if (!sent) {
		refusal = ibMcpText("The application did not answer in time. That is not the same as 'it did "
			"not happen' - if a run was being started, it may be running. code_status with the "
			"token, when you have one, is the only thing that settles it.");
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

	wxString GetName() const override { return wxT("code_run"); }

	// 🛑 NOT ON THE MAIN THREAD — see ibMcpTool::NeedsMainThread. All three of these ask the
	// application and WAIT, and the answer comes back through the session's worker, which on the
	// desktop is the main loop. Waiting here is waiting for a message only this thread can deliver.
	bool NeedsMainThread() const override { return false; }

	// ⚠ THE PERSON AT THE DESIGNER READS THIS LINE while it happens. It says what is being done to
	// their base, not what tool is running — it is the sentence they need in order to stop it if it
	// was not what they meant. So it prefers the caller's own one-line account over any wording this
	// file could invent: `said` names the actual job, where a generic "running code" names none.
	wxString GetActivity(const ibDataNode& params) const override
	{
		wxString said = ArgSaid().Text(params);
		if (said.IsEmpty())
			said = ArgUnderstood().Text(params);

		if (!ArgCommit().Flag(params))
			return said.IsEmpty()
				? ibMcpText("running code as a trial - everything written will be undone")
				: wxString::Format(
					ibMcpText("running code as a trial (everything written will be undone): %s"),
					said);
		// Their base, their sentence to read: what the assistant SAYS it is about to do, in the
		// assistant's own words, while there is still time to stop it.
		return said.IsEmpty()
			? ibMcpText("RUNNING CODE - writing to the base for real")
			: wxString::Format(ibMcpText("RUNNING CODE, writing for real: %s"), said);
	}

	wxString GetDescription() const override
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
			"DANGEROUS: with commit it writes DATA into the base and nothing can undo it - rows can "
			"be overwritten or deleted and there is no rollback.\n"
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
			"* WRITE TO THE REGISTRATION JOURNAL as you go - what is about to be done, how many rows, "
			"what came of each stage. NOT `Message`: a background session is tied to nobody, so "
			"whatever it says reaches no one at all. The journal is the only channel there is, and "
			"it is the better one anyway - it is durable, the person can read it too, and "
			"journal_read with the `session` this returns shows exactly this run's rows and nobody "
			"else's. THAT IS HOW ARBITRARY CODE IS CHECKED.\n"
			  "\n"
			"WHERE IT RUNS, because that decides what you can do about it. It needs a RUNTIME, which a "
			"designer does not have - so the application has to be RUNNING WITH THE DEBUGGER "
			"ATTACHED (app_run with debug true), and the code is sent to that process over the same "
			"wire the sandbox uses. It does NOT need to be stopped at a breakpoint: it runs beside "
			"the person, in a session of its own, and nobody is blocked.\n"
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
			ArgText(), ArgSaid(), ArgCommit(), ArgUnderstood() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString text   = ArgText().Text(params);
		const bool     commit = ArgCommit().Flag(params);

		// ⚠ Strip, not Trim — wxString::Trim MUTATES and so is non-const; this text is not ours to
		// edit and the neighbouring `understood` check only gets away with it on a temporary.
		if (text.Strip(wxString::both).IsEmpty()) {
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

		ibJobRunByteCodeState state;
		const bool sent = bridge->StartJob(request, state);
		if (!Answered(sent, state, refusal))
			return false;

		DescribeRun(state, result);
		result.SetValue(wxT("committing"), commit);
		if (!commit)
			result.SetValue(wxT("note"),
				ibMcpText("A trial: everything this writes will be undone. What comes back is what really "
					  "happened - read it before running the same thing with commit."));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolRunCode);

//---------------------------------------------------------------------------
// code_status
//---------------------------------------------------------------------------
class ibMcpToolRunCodeStatus : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("code_status"); }

	// 🛑 It waits on the wire like the one above — same reason, same answer.
	bool NeedsMainThread() const override { return false; }

	wxString GetActivity(const ibDataNode& /*params*/) const override
	{
		return ibMcpText("checking a code run");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("How a run is getting on: `activity` - where it says it has got to - "
			"plus whether it has finished and what it answered or failed with. It does not block: a "
			"run is meant to be watched while the person carries on working.\n"
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

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
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

MCP_TOOL_REGISTER(ibMcpToolRunCodeStatus);

//---------------------------------------------------------------------------
// code_cancel
//---------------------------------------------------------------------------
class ibMcpToolRunCodeCancel : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("code_cancel"); }

	// 🛑 It waits on the wire like the two above — same reason, same answer.
	bool NeedsMainThread() const override { return false; }

	wxString GetActivity(const ibDataNode& /*params*/) const override
	{
		return ibMcpText("stopping a code run");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Stop a run. COOPERATIVE: the flag is raised and the code unwinds at its "
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

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
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
		result.SetValue(wxT("note"), state.m_complete
			? ibMcpText("It had already finished - nothing to stop. What it ended with is here.")
			: ibMcpText("Asked it to stop; it unwinds at the next loop boundary. Anything already "
				    "committed stays."));
		DescribeRun(state, result);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolRunCodeCancel);

} // namespace
