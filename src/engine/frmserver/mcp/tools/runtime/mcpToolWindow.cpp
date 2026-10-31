////////////////////////////////////////////////////////////////////////////
//	Description : WORKING IN THE PERSON'S WINDOW - window_run. Code run in the
//	              application's session, as their own work under their roles.
////////////////////////////////////////////////////////////////////////////
//
// ⭐ ONE VERB, BECAUSE THE LANGUAGE ALREADY SAYS ALL OF IT (Max, 2026-10-31: open it for me, put the cursor on the row
// as a finger would, add a value, correct one - only within this person). A form opens, a row is stood on and an
// object is written from script, and the doors those go through ask the person's roles themselves. So the text is
// compiled under the root and run in a frame of its own, and nothing here decides what is allowed.
//
// ⚠ code_run IS NOT THIS. It is a background job: a session of its own, no window, and a token to ask after.

#include "frmserver/mcp/mcpTool.h"

#include "backend/backend_exception.h"
#include "backend/backend_mainFrame.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/compileModule.h"
#include "backend/compiler/procUnit.h"
#include "backend/moduleInfo.h"                                  // GetByteCode - how the root is asked for
#include "backend/moduleManager/moduleManager.h"
#include "backend/session/session.h"
#include "core/stringUtils.h"

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgCode()
{
	static const ibArg s_a(ibT("code"), ibArg::Kind::Text,
		ibMcpText("The statements, in this configuration's script dialect (`platform_state` says which). What they "
			"assign to `Result` comes back as text - `Result = someValue;` - or why they did not run. To read "
			"the data, use compose_run."),
		/*required*/ true);
	return s_a;
}

const ibArg& ArgSaid()
{
	static const ibArg s_a(ibT("said"), ibArg::Kind::Text,
		ibMcpText("What this does, in one line of the person's language. It is on their screen while it runs."),
		/*required*/ true);
	return s_a;
}

// While the code runs its failures come back to the call: without this each one is a Critical failure window in front
// of the person, for the assistant's mistake, and the call waits until somebody closes it.
class ibFailureToCaller {
public:
	explicit ibFailureToCaller(ibSession* session) : m_session(session) { m_session->SetProcessingBackendError(true); }
	~ibFailureToCaller() { m_session->SetProcessingBackendError(false); }
	ibFailureToCaller(const ibFailureToCaller&) = delete;
	ibFailureToCaller& operator=(const ibFailureToCaller&) = delete;
private:
	ibSession* const m_session;
};

//---------------------------------------------------------------------------
// window_run
//---------------------------------------------------------------------------
class ibMcpToolWindowRun : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("window_run"); }

	bool IsDataAssistant() const override { return true; }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("in your window: %s"), ArgSaid().Text(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("WORK IN THE PERSON'S WINDOW - run code in their session, as if they did it themselves. "
			"Open a list or a document for them, put the cursor on the row they should look at, add a value, "
			"correct one.\n"
			"\n"
			"POINT AT A ROW, as a finger would:\n"
			"  form = Documents.GoodsReceipt.GetListForm();\n"
			"  form.Show();\n"
			"  form.Controls.FindControl(\"DynamicList\").CurrentRow = reference;\n"
			"`form.List` is the form's attribute, the data; the table is a control, and the form names it: walking "
			"`form.Controls` gives each control's `Key` (`DynamicList` on a usual list form). `CurrentRow` takes a reference, "
			"and the window moves there; read back, it is the row, and the list's columns are its fields (a list on a query has "
			"what that query selects, so read anything else through `row.Ref`). "
			"`ActiveWindow()` is the form in front of them now.\n"
			"\n"
			"IT REALLY HAPPENS. What this writes is written, with no undo and no trial run: say what you are about to "
			"change and get their yes first. Their roles decide: writing what they may not write is refused, and "
			"the refusal comes back to you.\n"
			"\n"
			"The names are this configuration's and the language is its own: `metadata_tree`, `syntax_search` and "
			"`type_members` before guessing. ONLY IN THE APPLICATION.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgCode(), ArgSaid() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibSession* const session = ibSession::Current();
		ibValueModuleManagerRuntimeConfiguration* const manager = session != nullptr ? session->GetManagerModule() : nullptr;
		ibBackendDocFrame* const frame = ibSession::CurrentFrame();
		if (!ibIsMcpInApplication() || manager == nullptr || frame == nullptr) {
			refusal = ibMcpText("This runs in the application, in the window of the person using it.");
			return false;
		}

		bool ran = true;
		ibString answer;
		try {
			const ibFailureToCaller failureToCaller(session);

			// No code is running here, so there is no frame to stand in: the run gets its own, under the root, as a
			// background job's does (jobRunByteCode.cpp says why each of these lines is needed).
			const ibByteCode* const root = manager->GetByteCode();
			const std::shared_ptr<ibProcUnit> rootUnit = manager->GetProcUnit();

			ibCompileCode compiler(ibT("WindowCode"), ibT("WindowCode"));
			compiler.SetParent(root);
			if (!compiler.Compile(ArgCode().Text(params))) {
				refusal = ibMcpText("The code did not compile.");
				return false;
			}

			ibProcUnit runner;
			runner.SetParent(rootUnit.get());
			runner.Execute(compiler.m_cByteCode, /*delta*/ true);

			// What it came out with: the variable `Result`, the word code_run and the sandbox answer with.
			for (const auto& declared : compiler.m_cByteCode.m_listVar) {
				if ((!declared.IsLocal() && !declared.IsExport()) || !stringUtils::IsSameAs(ibT("Result"), declared.m_strRealName))
					continue;
				ibValue produced;
				if (runner.GetPropVal((long)declared, produced))
					answer = produced.GetString();
				break;
			}
		}
		catch (const ibBackendInterruptException&) {
			throw;
		}
		catch (const ibCoreException& thrown) {
			refusal = thrown.GetErrorDescription();
			ran = false;
		}

		// Whatever it got done before it stopped is in front of the person: the client asks for its frame when it is
		// told, and this came by the assistant's channel.
		frame->RefreshFrame();
		if (!ran)
			return false;

		result.SetValue(ibT("done"), true);
		if (!answer.IsEmpty())
			result.SetValue(ibT("result"), answer);
		return true;
	}
};

MCP_TOOL_REGISTER_RUNTIME(ibMcpToolWindowRun);

} // namespace
