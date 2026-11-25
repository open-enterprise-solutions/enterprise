////////////////////////////////////////////////////////////////////////////
//	Description : the two eras of the MCP protocol (frmserver/mcp/mcpEra.h) — which one a request is, what each
//	              asks of it at the door, how each writes a result.
//
//	              Pure: a request parsed from text, an era asked. No socket, no server, no application.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "backend/rpc/rpcMessage.h"
#include "core/serialize/dataBuilder.h"
#include "frmserver/mcp/mcpEra.h"

namespace {

ibRpcRequest Parse(const ibChar* text)
{
	ibRpcRequest request;
	ibString error;
	EXPECT_TRUE(ibRpcParseRequest(ibString(text), request, error)) << error.ToStdString();
	return request;
}

// tools/list, as each era sends it.
const ibChar* const kModern = ibT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":")
	ibT("{\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\"}}}");
const ibChar* const kLegacy = ibT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":{}}");

// The era a request is, by the revisions it answers in.
std::string EraOf(const ibChar* text)
{
	return ibMcpEra::Of(Parse(text)).GetVersions().front().ToStdString();
}

} // namespace

TEST(McpEra, Of_AVersionInMeta_IsModern_AnythingElseLegacy)
{
	EXPECT_EQ(EraOf(kModern), "2026-07-28");
	EXPECT_EQ(EraOf(kLegacy), "2025-11-25");
}

TEST(McpEra, SupportedVersionList_IsTheModernFirstThenTheLegacy)
{
	const std::vector<ibDataValue> list = ibMcpEra::SupportedVersionList();

	ASSERT_EQ(list.size(), 4u);
	EXPECT_EQ(list[0].AsString().ToStdString(), "2026-07-28");
	EXPECT_EQ(list[1].AsString().ToStdString(), "2025-11-25");
	EXPECT_EQ(list[2].AsString().ToStdString(), "2025-06-18");
	EXPECT_EQ(list[3].AsString().ToStdString(), "2025-03-26");

	EXPECT_TRUE(ibMcpEra::Speaks(ibT("2026-07-28")));
	EXPECT_TRUE(ibMcpEra::Speaks(ibT("2025-03-26")));
	EXPECT_FALSE(ibMcpEra::Speaks(ibT("2024-11-05")));
}

TEST(McpEra, Admit_AModernRequestThatAgrees_GoesOn)
{
	const ibRpcRequest request = Parse(kModern);

	ibMcpWireHeaders headers;
	headers.m_protocolVersion = ibT("2026-07-28");
	headers.m_method = ibT("tools/list");

	int code = 0;
	EXPECT_TRUE(ibMcpEra::Of(request).Admit(request, headers, &code).IsEmpty());
	EXPECT_EQ(code, 0);
}

TEST(McpEra, Admit_AHeaderThatDisagreesWithTheBody_IsRefused)
{
	const ibRpcRequest request = Parse(kModern);

	ibMcpWireHeaders headers;
	headers.m_method = ibT("tools/call");

	int code = 0;
	EXPECT_FALSE(ibMcpEra::Of(request).Admit(request, headers, &code).IsEmpty());
	EXPECT_EQ(code, static_cast<int>(ibRpcError::HeaderMismatch));
}

TEST(McpEra, Admit_ALegacyRevisionNamedInMeta_IsRefusedWithTheList)
{
	const ibRpcRequest request = Parse(
		ibT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":")
		ibT("{\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2025-11-25\"}}}"));

	int code = 0;
	const std::string refused = ibMcpEra::Of(request).Admit(request, ibMcpWireHeaders{}, &code).ToStdString();

	EXPECT_EQ(code, static_cast<int>(ibRpcError::UnsupportedVersion));
	EXPECT_NE(refused.find("2026-07-28"), std::string::npos) << refused;
}

TEST(McpEra, Admit_ALegacyRequest_GoesOn)
{
	const ibRpcRequest request = Parse(kLegacy);

	int code = 0;
	EXPECT_TRUE(ibMcpEra::Of(request).Admit(request, ibMcpWireHeaders{}, &code).IsEmpty());
	EXPECT_EQ(code, 0);
}

TEST(McpEra, Complete_AModernResult_SaysWhatItIsAndWhoGaveIt)
{
	ibDataNode result;
	ibMcpModernEra().Complete(result);
	ibMcpModernEra().FillCacheable(result);

	EXPECT_EQ(result.GetValue<ibString>(ibT("resultType")).ToStdString(), "complete");
	EXPECT_TRUE(result.Has(ibT("ttlMs")));
	EXPECT_EQ(result.GetValue<ibString>(ibT("cacheScope")).ToStdString(), "private");

	const auto meta = result.FindChild(ibT("_meta"));
	ASSERT_TRUE(meta);
	EXPECT_TRUE(meta->FindChild(ibT("io.modelcontextprotocol/serverInfo")));
}

TEST(McpEra, Complete_ALegacyResult_IsWrittenAsItIs)
{
	ibDataNode result;
	ibMcpLegacyEra().Complete(result);
	ibMcpLegacyEra().FillCacheable(result);

	EXPECT_FALSE(result.Has(ibT("resultType")));
	EXPECT_FALSE(result.Has(ibT("ttlMs")));
	EXPECT_FALSE(result.FindChild(ibT("_meta")));
}
