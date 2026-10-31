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
#include "backend/moduleInfo.h"                                  // GetCompileModule - how the root is asked for
#include "backend/moduleManager/moduleManager.h"
#include "backend/session/session.h"

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgCode()
{
	static const ibArg s_a(ibT("code"), ibArg::Kind::Text,
		ibMcpText("The statements, in this configuration's script dialect (`platform_state` says which). Nothing "
			"comes back but that they ran, or why they did not: to read something, use compose_run."),
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

//---------------------------------------------------------------------------
// window_run
//---------------------------------------------------------------------------
class ibMcpToolWindowRun : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("window_run"); }

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
			"  form.List.CurrentRow = reference;\n"
			"`List` is the name of the table on that form. `CurrentRow` takes a reference, and the window moves there. "
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
		const ibSession* const session = ibSession::Current();
		ibValueModuleManagerRuntimeConfiguration* const manager = session != nullptr ? session->GetManagerModule() : nullptr;
		ibBackendDocFrame* const frame = ibSession::CurrentFrame();
		if (!ibIsMcpInApplication() || manager == nullptr || frame == nullptr) {
			refusal = ibMcpText("This runs in the application, in the window of the person using it.");
			return false;
		}

		bool ran = true;
		try {
			// No code is running here, so there is no frame to stand in: the run gets its own, under the root, as a
			// background job's does (jobRunByteCode.cpp says why each of these lines is needed).
			ibCompileModule* const root = manager->GetCompileModule();
			const std::shared_ptr<ibProcUnit> rootUnit = manager->GetProcUnit();

			ibCompileCode compiler(ibT("WindowCode"), ibT("WindowCode"));
			compiler.SetParent(root);
			if (!compiler.Compile(ArgCode().Text(params))) {
				refusal = ibMcpText("The code did not compile.");
				return false;
			}
			compiler.m_cByteCode.m_parent = &root->m_cByteCode;

			ibProcUnit runner;
			runner.SetParent(rootUnit.get());
			runner.Execute(compiler.m_cByteCode, /*delta*/ true);
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
		return true;
	}
};

MCP_TOOL_REGISTER_DATA(ibMcpToolWindowRun);

} // namespace
