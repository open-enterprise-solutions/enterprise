////////////////////////////////////////////////////////////////////////////
//	Description : the JSON-RPC envelope — ibRpcParseRequest / ibRpcWriteResult /
//	              ibRpcWriteError / ibRpcRenderNode (backend/rpc/rpcMessage.cpp).
//
//	              This is the layer with no opinions: it reads one JSON-RPC
//	              message and writes one back. Everything above it — the tools,
//	              the registry, the argument gate — assumes it parsed the id, the
//	              method and the params correctly, and none of them can notice if
//	              it did not. A request whose id came back wrong is answered into
//	              nowhere: the protocol matches a reply to its call by that value
//	              and by nothing else.
//
//	              ⭐ THE DISTINCTION WORTH PINNING is notification vs call. A
//	              message with no id wants NO answer, and answering it anyway
//	              leaves the client reading a reply to something it never asked.
//	              WantsAnswer() is the whole of that rule.
//
//	              Pure backend: strings in, strings out. No socket, no server, no
//	              configuration.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>
#include <string>

#include "backend/rpc/rpcMessage.h"
#include "core/serialize/dataBuilder.h"
#include "core/serialize/jsonProvider.h"   // kMaxNesting — the reader's ceiling, one number

//---------------------------------------------------------------------------
// reading
//---------------------------------------------------------------------------

TEST(RpcMessage,ParseRequest_ACallWithANumericId_IsReadWhole)
{
	ibRpcRequest request;
	wxString error;

	ASSERT_TRUE(ibRpcParseRequest(
		wxT("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/list\",\"params\":{}}"),
		request, error)) << error.ToStdString();

	EXPECT_EQ(request.m_method, wxT("tools/list"));
	EXPECT_TRUE(request.WantsAnswer());
	ASSERT_EQ(request.m_id.Kind(), ibDataKind::Number);
	EXPECT_EQ(request.m_id.AsInt(), 7);
}

TEST(RpcMessage,ParseRequest_AStringId_StaysAString)
{
	// The id is echoed VERBATIM, so its type has to survive the trip: a client
	// that called with "abc" and is answered with 0 has lost its reply.
	ibRpcRequest request;
	wxString error;

	ASSERT_TRUE(ibRpcParseRequest(
		wxT("{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"tools/list\"}"),
		request, error)) << error.ToStdString();

	ASSERT_EQ(request.m_id.Kind(), ibDataKind::String);
	EXPECT_EQ(request.m_id.AsString(), wxT("abc"));
}

TEST(RpcMessage,ParseRequest_ANotification_WantsNoAnswer)
{
	// No id — `notifications/initialized` and its kind. The protocol says answer
	// nothing at all, and this flag is how the server knows to stay quiet.
	ibRpcRequest request;
	wxString error;

	ASSERT_TRUE(ibRpcParseRequest(
		wxT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"),
		request, error)) << error.ToStdString();

	EXPECT_EQ(request.m_method, wxT("notifications/initialized"));
	EXPECT_FALSE(request.WantsAnswer());
	EXPECT_TRUE(request.m_id.IsEmpty());
}

TEST(RpcMessage,ParseRequest_ArgumentsArriveWhereTheToolsLookForThem)
{
	// A tool reads `params.arguments`, and the shape it finds is the shape the
	// undeclared-argument gate walks: scalars in the node's FIELDS, composites
	// in its PROPERTIES. Both halves are asserted, because checking only one of
	// them is precisely the bug that gate was written to catch.
	ibRpcRequest request;
	wxString error;

	ASSERT_TRUE(ibRpcParseRequest(
		wxT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":")
		wxT("{\"name\":\"metadata_get\",\"arguments\":{\"id\":42,\"nested\":{\"a\":1}}}}"),
		request, error)) << error.ToStdString();

	EXPECT_EQ(request.m_params.GetValue<wxString>(wxT("name")), wxT("metadata_get"));

	const ibDataNode* arguments = request.m_params.FindChild(wxT("arguments"));
	ASSERT_NE(arguments, nullptr);

	const ibDataValue* id = arguments->FindField(wxT("id"));
	ASSERT_NE(id, nullptr) << "a scalar argument must land in the node's fields";
	EXPECT_EQ(id->AsInt(), 42);

	EXPECT_NE(arguments->FindChild(wxT("nested")), nullptr)
		<< "an object argument must land in the node's properties";
}

TEST(RpcMessage,ParseRequest_Rubbish_IsRefusedInWords)
{
	ibRpcRequest request;
	wxString error;

	EXPECT_FALSE(ibRpcParseRequest(wxT("not json at all"), request, error));
	EXPECT_FALSE(error.IsEmpty()) << "a refusal with no words is unactionable";
}

TEST(RpcMessage,ParseRequest_WellFormedJsonThatIsNotARequest_IsRefused)
{
	// Valid JSON is not the question — the question is whether it is a CALL.
	ibRpcRequest request;
	wxString error;

	EXPECT_FALSE(ibRpcParseRequest(wxT("{\"jsonrpc\":\"2.0\",\"id\":1}"), request, error));
	EXPECT_FALSE(error.IsEmpty());
}

namespace {

// The root object counts, so (limit - 1) arrays under it is the deepest params
// value that is still read.
wxString LoginWithArrays(int arrays)
{
	std::string text;
	text.reserve(static_cast<size_t>(arrays) * 2 + 64);
	text += "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"login\",\"params\":";
	text.append(static_cast<size_t>(arrays), '[');
	text.append(static_cast<size_t>(arrays), ']');
	text += '}';
	return wxString::FromUTF8(text.data(), text.size());
}

} // namespace

TEST(RpcMessage,ParseRequest_NestingAtTheLimit_IsRead)
{
	ibRpcRequest request;
	wxString error;

	ASSERT_TRUE(ibRpcParseRequest(LoginWithArrays(ibJsonProvider::kMaxNesting - 1), request, error))
		<< error.ToStdString();
	EXPECT_EQ(request.m_method, wxT("login"));
	EXPECT_EQ(request.m_id.AsInt(), (s64)1);
}

// ibClientHost::Call parses before it authenticates, and on a refusal writes a
// JSON-RPC parse error. One array past the ceiling must take that road.
TEST(RpcMessage,ParseRequest_NestingPastTheLimit_AnswersAParseError)
{
	ibRpcRequest request;
	wxString error;

	ASSERT_FALSE(ibRpcParseRequest(LoginWithArrays(ibJsonProvider::kMaxNesting), request, error));
	ASSERT_FALSE(error.IsEmpty());

	const wxString answer = ibRpcWriteError(ibDataValue(), ibRpcError::Parse, error);
	EXPECT_NE(answer.Find(wxT("-32700")), wxNOT_FOUND) << answer.ToStdString();
	EXPECT_NE(answer.Find(wxT("error")), wxNOT_FOUND);
}

// N ≈ 5000–6000 of this shape SIGSEGV'd a Linux Debug build (about 12 KB).
TEST(RpcMessage,ParseRequest_TheReportedCrashDepth_IsAParseError)
{
	ibRpcRequest request;
	wxString error;

	EXPECT_FALSE(ibRpcParseRequest(LoginWithArrays(8000), request, error));
	EXPECT_FALSE(error.IsEmpty());
}

//---------------------------------------------------------------------------
// writing
//---------------------------------------------------------------------------

TEST(RpcMessage,WriteResult_CarriesTheIdBackVerbatim)
{
	ibDataNode result;
	result.SetValue(wxT("answer"), wxString(wxT("yes")));

	const wxString numeric = ibRpcWriteResult(ibDataValue::Int(7), result);
	EXPECT_NE(numeric.Find(wxT("\"id\":7")), wxNOT_FOUND) << numeric.ToStdString();
	EXPECT_NE(numeric.Find(wxT("result")), wxNOT_FOUND);
	// …and on one line: a message on the wire carries no breaks nor indents.
	EXPECT_EQ(numeric.Find(wxT('\n')), wxNOT_FOUND) << numeric.ToStdString();

	const wxString textual = ibRpcWriteResult(ibDataValue::String(wxT("abc")), result);
	EXPECT_NE(textual.Find(wxT("\"abc\"")), wxNOT_FOUND) << textual.ToStdString();
}

TEST(RpcMessage,WriteError_SaysTheCodeAndTheReason)
{
	const wxString written = ibRpcWriteError(
		ibDataValue::Int(3), ibRpcError::MethodNotFound, wxT("No tool named 'nope'"));

	EXPECT_NE(written.Find(wxT("-32601")), wxNOT_FOUND) << written.ToStdString();
	EXPECT_NE(written.Find(wxT("No tool named")), wxNOT_FOUND);
	EXPECT_NE(written.Find(wxT("error")), wxNOT_FOUND);
}

TEST(RpcMessage,WriteError_AndWriteResult_AreToldApart)
{
	ibDataNode empty;

	const wxString ok = ibRpcWriteResult(ibDataValue::Int(1), empty);
	const wxString bad = ibRpcWriteError(ibDataValue::Int(1), ibRpcError::Internal, wxT("boom"));

	EXPECT_EQ(ok.Find(wxT("\"error\"")), wxNOT_FOUND) << "a result must not look like a failure";
	EXPECT_EQ(bad.Find(wxT("\"result\"")), wxNOT_FOUND) << "a failure must not look like a result";
}

//---------------------------------------------------------------------------
// rendering
//---------------------------------------------------------------------------

TEST(RpcMessage,RenderNode_WritesTheFieldsACallerWillRead)
{
	ibDataNode node;
	node.SetValue(wxT("name"), wxString(wxT("Goods")));
	node.AddField(wxT("count"), ibDataValue::Int(3));
	node.AddField(wxT("ok"), ibDataValue::Bool(true));

	const wxString json = ibRpcRenderNode(node);

	EXPECT_NE(json.Find(wxT("Goods")), wxNOT_FOUND) << json.ToStdString();
	EXPECT_NE(json.Find(wxT("3")), wxNOT_FOUND);
	EXPECT_NE(json.Find(wxT("true")), wxNOT_FOUND);
}

TEST(RpcMessage,RenderNode_EscapesWhatWouldOtherwiseBreakTheEnvelope)
{
	// A refusal carries the engine's own words, and those words can contain a
	// quote or a newline. Unescaped, they end the string early and the client
	// reads a parse error instead of the sentence that explains its mistake.
	ibDataNode node;
	node.SetValue(wxT("message"), wxString(wxT("he said \"no\"\nand left")));

	const wxString json = ibRpcRenderNode(node);

	EXPECT_NE(json.Find(wxT("\\\"")), wxNOT_FOUND) << json.ToStdString();
	EXPECT_NE(json.Find(wxT("\\n")), wxNOT_FOUND) << json.ToStdString();
}

TEST(RpcMessage,RenderNode_KeepsNonAsciiReadable)
{
	// Tool descriptions and engine refusals are localised; a caller has to get
	// the sentence, not a row of question marks.
	ibDataNode node;
	node.SetValue(wxT("message"), wxString::FromUTF8("\xD0\x9E\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0"));

	const wxString json = ibRpcRenderNode(node);

	EXPECT_EQ(json.Find(wxT('?')), wxNOT_FOUND)
		<< "a lost character means the locale ate it: " << json.ToStdString();
}

//---------------------------------------------------------------------------
// the round trip
//---------------------------------------------------------------------------

TEST(RpcMessage,AnAnswer_IsReadableAsOne)
{
	// What the server writes, ibRpcParseResponse must be able to read back — the
	// two halves of one protocol. And a RESPONSE is told from a REQUEST by one
	// fact: it names no method.
	ibDataNode result;
	result.SetValue(wxT("answer"), wxString(wxT("yes")));

	const wxString written = ibRpcWriteResult(ibDataValue::Int(11), result);

	ibDataValue id;
	wxString payload;
	bool isError = true;

	ASSERT_TRUE(ibRpcParseResponse(written, id, payload, isError)) << written.ToStdString();

	EXPECT_FALSE(isError);
	ASSERT_EQ(id.Kind(), ibDataKind::Number);
	EXPECT_EQ(id.AsInt(), 11);
	EXPECT_NE(payload.Find(wxT("yes")), wxNOT_FOUND);
}

TEST(RpcMessage,ARequest_IsNotMistakenForAnAnswer)
{
	ibDataValue id;
	wxString payload;
	bool isError = false;

	EXPECT_FALSE(ibRpcParseResponse(
		wxT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"), id, payload, isError))
		<< "anything carrying a method is a call, not a reply";
}
