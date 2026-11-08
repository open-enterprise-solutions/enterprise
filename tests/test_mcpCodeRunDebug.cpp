// code_run reaches an application only over the debugger's connection (#362).
// An application started with `app_run {debug: false}` is running and out of
// its reach, and nothing said so: the refusal was "No application is attached -
// app_run starts one", which reads as "start another", and app_run then started
// a second application beside the first, on the same base.
//
// The refusal itself needs a running application to reach (frmserver's
// JobBridge, asked of the debugger's connection), so what is held here is the
// half a caller reads before calling: the two tools' published words. A machine
// caller reads `tools/list` and nothing else - the description IS the API
// (test_mcpToolContract.cpp).

#include <gtest/gtest.h>

#include "frmserver/mcp/mcpTool.h"

namespace {

const ibMcpTool::ibMcpArgument* Argument(const ibMcpTool& tool, const ibString& name)
{
	for (const ibMcpTool::ibMcpArgument& argument : tool.Arguments()) {
		if (argument.Name() == name)
			return &argument;
	}
	return nullptr;
}

} // namespace

TEST(McpCodeRunDebug, AppRunDebugSaysTheCodeVerbsNeedIt)
{
	const ibMcpTool* const appRun = ibFindMcpTool(ibT("app_run"));
	ASSERT_NE(appRun, nullptr);

	const ibMcpTool::ibMcpArgument* const debug = Argument(*appRun, ibT("debug"));
	ASSERT_NE(debug, nullptr);

	// The argument a caller decides by names what it costs to say no.
	EXPECT_TRUE(debug->Description().Contains(ibT("code_run"))) << debug->Description().ToStdString();
	EXPECT_TRUE(debug->Description().Contains(ibT("debug: false"))) << debug->Description().ToStdString();
}

TEST(McpCodeRunDebug, CodeRunSaysItNeedsTheDebugger)
{
	const ibMcpTool* const codeRun = ibFindMcpTool(ibT("code_run"));
	ASSERT_NE(codeRun, nullptr);

	const ibString said = codeRun->GetDescription();
	EXPECT_TRUE(said.Contains(ibT("app_run with debug true"))) << said.ToStdString();
}
