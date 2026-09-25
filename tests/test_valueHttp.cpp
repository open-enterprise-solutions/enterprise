// HTTP for scripts - HTTPConnection, HTTPRequest, HTTPResponse.
//
// The first half (HttpValues) asks the three values what needs no wire. The second half (HttpClient) brings a
// server up on 127.0.0.1 inside the test and holds what the client did against what that server SAW - the
// method, the path, the headers, the bytes. That is the truth computed outside the thing under test
// (development.md, section 5): a client checked against its own idea of what it sent confirms itself.
//
// No network and nothing installed is needed: the server is the library's own, on a port the system picks.

#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "backend/backend_exception.h"
#include "backend/system/value/valueBinaryData.h"
#include "backend/system/value/valueHttp.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/systemManagerEnum.h"   // the HTTPMethod enumeration's value, for the CallMethod road

// As in mcp/mcpServer.cpp, which says why: wx and cpp-httplib both alias ssize_t, and MSVC refuses the second.
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include "3rdparty/cpp-httplib/httplib.h"

namespace {

std::string Utf8(const wxString& text)
{
	const wxScopedCharBuffer utf8 = text.utf8_str();
	return std::string(utf8.data(), utf8.length());
}

// What a refusal says, or "" when the call went through.
template <class Call>
wxString Refusal(Call call)
{
	try {
		call();
	}
	catch (const ibBackendException& refusal) {
		const wxString why = refusal.GetErrorDescription();
		return why.empty() ? wxString(wxT("(a refusal with no sentence)")) : why;
	}
	return wxString();
}

ibValue Text(const wxString& text) { return ibValue(text); }

} // namespace

// ---------------------------------------------------------------------------
//  Without a wire
// ---------------------------------------------------------------------------

TEST(HttpValues, AHostIsANameNotAnAddress)
{
	for (const wxChar* host : { wxT("http://api.example"), wxT("https://api.example"), wxT("api.example/v1"), wxT(""), wxT("api example"), wxT("api\r\nexample"),
			wxT("user@api.example"), wxT("api.example?x"), wxT("api.example#x"), wxT("api\\example"),
			wxT("api.example:8080"), wxT("[::1]") }) {
		ibValueHttpConnection connection;
		const wxString why = Refusal([&] { connection.Open(host, 80, wxString(), wxString(), 30); });
		EXPECT_FALSE(why.empty()) << "taken for a host: '" << Utf8(host) << "'";
	}
	ibValueHttpConnection connection;
	EXPECT_TRUE(Refusal([&] { connection.Open(wxT("api.example"), 8080, wxString(), wxString(), 30); }).empty());
	EXPECT_TRUE(Refusal([&] { connection.Open(wxT("127.0.0.1"), 8080, wxString(), wxString(), 30); }).empty());
	EXPECT_TRUE(Refusal([&] { connection.Open(wxT("::1"), 8080, wxString(), wxString(), 30); }).empty());
	EXPECT_FALSE(Refusal([&] { connection.Open(wxT("api.example"), 80, wxT("user:name"), wxT("p"), 30); }).empty()) << "a ':' in a user name is what parts it from the password";

	// An international name goes to the resolver as bytes it does not read: its ASCII form is asked for, by name.
	const wxString why = Refusal([&] { connection.Open(wxString::FromUTF8("\xD0\xBA\xD0\xB0\xD1\x81\xD0\xB0.example"), 80, wxString(), wxString(), 30); });
	ASSERT_FALSE(why.empty());
	EXPECT_TRUE(why.Contains(wxT("xn--"))) << Utf8(why);
}

TEST(HttpValues, ThePortAndTheTimeAllowedAreRefusedWhenTheyMeanNothing)
{
	ibValueHttpConnection connection;
	EXPECT_FALSE(Refusal([&] { connection.Open(wxT("h"), 0, wxString(), wxString(), 30); }).empty());
	EXPECT_FALSE(Refusal([&] { connection.Open(wxT("h"), 65536, wxString(), wxString(), 30); }).empty());
	EXPECT_FALSE(Refusal([&] { connection.Open(wxT("h"), 80, wxString(), wxString(), 0); }).empty()) << "zero is not 'forever'";
	EXPECT_FALSE(Refusal([&] { connection.Open(wxT("h"), 80, wxString(), wxString(), -5); }).empty());
	EXPECT_TRUE(connection.IsEmpty()) << "a refused Open leaves nothing behind";

	connection.Open(wxT("h"), 80, wxString(), wxString(), 30);
	EXPECT_FALSE(connection.IsEmpty());
	EXPECT_FALSE(Refusal([&] { connection.SetTimeout(0); }).empty());

	// "Never forever" has to hold at the top too: past ~2 147 483 s the library's int arithmetic goes negative
	// and a negative wait IS forever. A day is the most.
	EXPECT_FALSE(Refusal([&] { connection.SetTimeout(24 * 60 * 60 + 1); }).empty());
	EXPECT_FALSE(Refusal([&] { connection.SetTimeout(2147484); }).empty());
	EXPECT_TRUE(Refusal([&] { connection.SetTimeout(24 * 60 * 60); }).empty());
}

TEST(HttpValues, ABodyIsBytesInTheEncodingSaid)
{
	const wxString text = wxString::FromUTF8("\xD0\x91\xD0\xBE\xD1\x80\xD1\x89");   // four Cyrillic letters
	ibValueHttpRequest request;
	request.SetBodyText(text, ibTextEncoding_UTF8);
	EXPECT_EQ(request.Body(), std::string("\xD0\x91\xD0\xBE\xD1\x80\xD1\x89")) << "UTF-8, and no byte-order mark in front";
	request.SetBodyText(text, ibTextEncoding_UTF16);
	EXPECT_EQ(request.Body().size(), 8u) << "two bytes a letter, and no mark";
	request.SetBodyText(wxString(), ibTextEncoding_UTF8);
	EXPECT_TRUE(request.Body().empty());

	const char zeros[] = { 'a', '\0', 'b', '\0', '\0' };
	request.SetBodyBytes(zeros, sizeof(zeros));
	EXPECT_EQ(request.Body(), std::string(zeros, sizeof(zeros))) << "bytes stay bytes, zeros included";
}

TEST(HttpValues, AResourceAddressIsAPathAndNothingThatBreaksTheRequestLine)
{
	ibValueHttpRequest request;
	EXPECT_EQ(request.ResourceAddress(), wxString(wxT("/"))) << "a request nobody addressed asks for the root";
	request.SetResourceAddress(wxT("/v1/sales?shift=42"));
	EXPECT_EQ(request.ResourceAddress(), wxString(wxT("/v1/sales?shift=42")));
	request.SetResourceAddress(wxString());
	EXPECT_EQ(request.ResourceAddress(), wxString(wxT("/")));

	EXPECT_FALSE(Refusal([&] { request.SetResourceAddress(wxT("v1/sales")); }).empty()) << "no leading slash";
	EXPECT_FALSE(Refusal([&] { request.SetResourceAddress(wxT("/a\r\nHost: evil")); }).empty()) << "a line break";
	EXPECT_FALSE(Refusal([&] { request.SetResourceAddress(wxT("http://other/a")); }).empty()) << "an address with a scheme";
	EXPECT_FALSE(Refusal([&] { request.SetResourceAddress(wxT("/a#section")); }).empty()) << "a fragment is never sent to a server";
	EXPECT_EQ(request.ResourceAddress(), wxString(wxT("/"))) << "a refused address leaves the one that stood";
}

TEST(HttpValues, HeadersAreAStructureAndFoldCaseAsHttpDoes)
{
	ibValueHttpRequest request;
	const ibValuePtr<ibValueStructure> headers(new ibValueStructure());
	headers->Insert(Text(wxT("Content-Type")), Text(wxT("application/json")));
	request.SetHeaders(headers);
	EXPECT_GE(request.Headers().FindProp(wxT("content-type")), 0);

	EXPECT_FALSE(Refusal([&] { request.SetHeaders(Text(wxT("Accept: x"))); }).empty()) << "a string is not a Structure";
	EXPECT_GE(request.Headers().FindProp(wxT("Content-Type")), 0) << "a refused assignment leaves the headers that stood";
}

TEST(HttpValues, AResponseSaysItsBytesAreNotTextRatherThanHandingOverGarbage)
{
	const ibValuePtr<ibValueStructure> headers(new ibValueStructure());
	const ibValuePtr<ibValueHttpResponse> broken(new ibValueHttpResponse(200, headers, std::string("\xFF\xFE\xFD")));
	EXPECT_FALSE(Refusal([&] { broken->BodyAsString(ibTextEncoding_UTF8); }).empty());
	EXPECT_EQ(broken->Body().size(), 3u) << "and the bytes are still there to be taken as bytes";

	const ibValuePtr<ibValueHttpResponse> marked(new ibValueHttpResponse(200, headers, std::string("\xEF\xBB\xBFok")));
	EXPECT_EQ(marked->BodyAsString(ibTextEncoding_UTF8), wxString(wxT("ok"))) << "a byte-order mark is a mark, not text";

	const ibValuePtr<ibValueHttpResponse> empty(new ibValueHttpResponse(204, headers, std::string()));
	EXPECT_TRUE(empty->BodyAsString(ibTextEncoding_UTF8).empty()) << "no body is an empty text, not a refusal";

	// UTF-16 either way round, its mark read as a mark - the same door a TextReader reads a file through.
	const ibValuePtr<ibValueHttpResponse> little(new ibValueHttpResponse(200, headers, std::string("\xFF\xFEo\0k\0", 6)));
	EXPECT_EQ(little->BodyAsString(ibTextEncoding_UTF16), wxString(wxT("ok")));
	const ibValuePtr<ibValueHttpResponse> big(new ibValueHttpResponse(200, headers, std::string("\xFE\xFF\0o\0k", 6)));
	EXPECT_EQ(big->BodyAsString(ibTextEncoding_UTF16), wxString(wxT("ok")));
}

// 🛑 A STRING WHERE AN OBJECT IS EXPECTED IS REFUSED, NOT DEREFERENCED. The first version took its arguments
// through ibValuePtr<T>(const ibValue&), which reads the reference out of a union it shares with a string's
// pointer - `request.Headers = "Accept: x"` was an access violation, not a sentence. Asked through the door a
// script comes in by.
TEST(HttpValues, AStringWhereAnObjectIsExpectedIsRefusedThroughTheScriptsDoor)
{
	ibValue text = Text(wxT("not an object"));
	ibValue* args[] = { &text };
	ibValue out;

	ibValueHttpRequest request;
	const long headers = request.FindProp(wxT("Headers"));
	ASSERT_GE(headers, 0);
	EXPECT_FALSE(Refusal([&] { request.SetPropVal(headers, text); }).empty());
	const long fromBinary = request.FindMethod(wxT("SetBodyFromBinaryData"));
	ASSERT_GE(fromBinary, 0);
	EXPECT_FALSE(Refusal([&] { request.CallAsFunc(fromBinary, out, args, 1); }).empty());

	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), 9, wxString(), wxString(), 1);
	const long get = connection.FindMethod(wxT("Get"));
	ASSERT_GE(get, 0);
	EXPECT_FALSE(Refusal([&] { connection.CallAsFunc(get, out, args, 1); }).empty()) << "and nothing was sent anywhere";
}

// A response comes out of a connection; `New HTTPResponse` has nothing to refer to. That is what registering a
// type as a SYSTEM one means, and the registry is asked how each of the three is registered.
TEST(HttpValues, AResponseIsHandedOutNeverCreated)
{
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("HTTPResponse"), ibCtorObjectType::ibCtorObjectType_object_system));
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("HTTPRequest"), ibCtorObjectType::ibCtorObjectType_object_value));
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("HTTPConnection"), ibCtorObjectType::ibCtorObjectType_object_value));
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("HTTPMethod"), ibCtorObjectType::ibCtorObjectType_object_enum));

	// A connection to nowhere is not one: `New HTTPConnection()` is refused, a request is made freely.
	ibValue made;
	EXPECT_FALSE(Refusal([&] { made = ibValue::CreateObject(wxT("HTTPConnection")); }).empty());
	EXPECT_TRUE(Refusal([&] { made = ibValue::CreateObject(wxT("HTTPRequest")); }).empty());
	EXPECT_FALSE(ibValue::IsRegisterCtor(wxT("HTTPResponse"), ibCtorObjectType::ibCtorObjectType_object_value)) << "New HTTPResponse has nothing to refer to";
}

// ---------------------------------------------------------------------------
//  Over the wire
// ---------------------------------------------------------------------------

namespace {

struct ibSeen {
	std::string      m_method, m_path, m_target, m_body;          // m_target is the request line's own spelling, undecoded
	httplib::Headers m_headers;
	httplib::Params  m_params;
};

class HttpClient : public ::testing::Test {
protected:
	void SetUp() override
	{
		const auto answer = [this](const httplib::Request& request, httplib::Response& response) {
			{
				const std::lock_guard<std::mutex> lock(m_mutex);
				m_seen.push_back({ request.method, request.path, request.target, request.body, request.headers, request.params });
			}
			if (request.path == "/status/404") { response.status = 404; response.set_content("no such thing", "text/plain"); return; }
			if (request.path == "/status/500") { response.status = 500; return; }
			if (request.path == "/moved") { response.status = 302; response.set_header("Location", "/echo"); return; }
			if (request.path == "/status/204") { response.status = 204; return; }
			if (request.path == "/see-other") { response.status = 303; response.set_header("Location", "/echo"); return; }
			if (request.path == "/drip") {
				// A byte every 0.4 s for 4 s: no single read ever waits long enough to time out, so only a bound
				// on the WHOLE request can end this.
				response.set_chunked_content_provider("text/plain", [](size_t, httplib::DataSink& sink) {
					for (int i = 0; i < 10; i++) {
						std::this_thread::sleep_for(std::chrono::milliseconds(400));
						if (!sink.write("x", 1)) {
							return false;
						}
					}
					sink.done();
					return true;
				});
				return;
			}
			if (request.path == "/quiet") { std::this_thread::sleep_for(std::chrono::seconds(3)); response.set_content("late", "text/plain"); return; }
			if (request.path == "/twice") {
				response.headers.emplace("X-Tag", "a");
				response.headers.emplace("X-Tag", "b");
				response.headers.emplace("Set-Cookie", "first=1");
				response.headers.emplace("Set-Cookie", "second=2");
				return;
			}
			if (request.path == "/not-text") { response.set_content(std::string("\xFF\xFE\xFD"), "application/octet-stream"); return; }
			response.set_content(request.body, "application/octet-stream");        // /echo and everything else
		};
		m_server.Get(".*", answer);
		m_server.Post(".*", answer);
		m_server.Put(".*", answer);
		m_server.Patch(".*", answer);
		m_server.Delete(".*", answer);
		m_server.Options(".*", answer);
		m_server.set_payload_max_length(64u * 1024u * 1024u);
		m_port = m_server.bind_to_any_port("127.0.0.1");
		ASSERT_GT(m_port, 0);
		m_thread = std::thread([this] { m_server.listen_after_bind(); });
		m_server.wait_until_ready();
	}

	void TearDown() override
	{
		m_server.stop();
		if (m_thread.joinable()) {
			m_thread.join();
		}
	}

	ibSeen Last()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_seen.empty() ? ibSeen() : m_seen.back();
	}

	size_t Count()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_seen.size();
	}

	// The response, held; it binds only if what came back IS a response.
	static ibValuePtr<ibValueHttpResponse> Ask(ibValueHttpConnection& connection, ibHttpMethod method, const ibValueHttpRequest& request)
	{
		return ibValuePtr<ibValueHttpResponse>(connection.Send(method, request));
	}

	httplib::Server     m_server;
	std::thread         m_thread;
	std::mutex          m_mutex;
	std::vector<ibSeen> m_seen;
	int                 m_port = 0;
};

} // namespace

TEST_F(HttpClient, EveryMethodArrivesAsItselfWithItsPathAndItsBytes)
{
	struct ibCase { ibHttpMethod m_method; const char* m_name; bool m_body; };
	const ibCase cases[] = {
		{ ibHttpMethod_Get, "GET", false }, { ibHttpMethod_Post, "POST", true }, { ibHttpMethod_Put, "PUT", true },
		{ ibHttpMethod_Patch, "PATCH", true }, { ibHttpMethod_Delete, "DELETE", false },
		{ ibHttpMethod_Head, "HEAD", false }, { ibHttpMethod_Options, "OPTIONS", false } };

	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	const char bytes[] = { 'a', '\0', 'b' };
	for (const ibCase& one : cases) {
		ibValueHttpRequest request;
		request.SetResourceAddress(wxT("/echo/path"));
		if (one.m_body) {
			request.SetBodyBytes(bytes, sizeof(bytes));
		}
		const ibValuePtr<ibValueHttpResponse> response = Ask(connection, one.m_method, request);
		ASSERT_TRUE(response != nullptr) << one.m_name;
		ASSERT_EQ(response->StatusCode(), 200) << one.m_name;
		const ibSeen seen = Last();
		EXPECT_EQ(seen.m_method, std::string(one.m_name));
		EXPECT_EQ(seen.m_path, std::string("/echo/path")) << one.m_name;
		EXPECT_EQ(seen.m_body, one.m_body ? std::string(bytes, sizeof(bytes)) : std::string()) << one.m_name;
		if (one.m_body) {
			EXPECT_EQ(response->Body(), std::string(bytes, sizeof(bytes))) << one.m_name << ": echoed";
		}
	}
	EXPECT_EQ(Count(), sizeof(cases) / sizeof(cases[0])) << "one request each, over one connection object";
}

// What the header's comment and the help say about a body nobody typed: the library sends it as text/plain.
// Pinned, because it is the library's decision and an update of the library is where it would change.
TEST_F(HttpClient, ABodyWithNoContentTypeGoesAsPlainTextAndTheOneGivenArrivesOnce)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	ibValueHttpRequest request;
	request.SetBodyText(wxT("{}"), ibTextEncoding_UTF8);
	Ask(connection, ibHttpMethod_Post, request);
	ASSERT_EQ(Last().m_headers.count("Content-Type"), 1u);
	EXPECT_EQ(Last().m_headers.find("Content-Type")->second, std::string("text/plain")) << "nobody said one";
	EXPECT_EQ(Last().m_body, std::string("{}"));

	ibValueHttpRequest bare;
	Ask(connection, ibHttpMethod_Get, bare);
	EXPECT_EQ(Last().m_headers.count("Content-Type"), 0u) << "no body, no type";

	const ibValuePtr<ibValueStructure> headers(new ibValueStructure());
	headers->Insert(Text(wxT("content-type")), Text(wxT("application/json")));
	headers->Insert(Text(wxT("X-Shift")), Text(wxT("42")));
	request.SetHeaders(headers);
	Ask(connection, ibHttpMethod_Post, request);
	const ibSeen seen = Last();
	ASSERT_EQ(seen.m_headers.count("Content-Type"), 1u);
	EXPECT_EQ(seen.m_headers.find("Content-Type")->second, std::string("application/json"));
	ASSERT_EQ(seen.m_headers.count("X-Shift"), 1u);
	EXPECT_EQ(seen.m_headers.find("X-Shift")->second, std::string("42"));
}

TEST_F(HttpClient, AHeaderThatWouldBreakTheMessageIsRefusedBeforeAnythingIsSent)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	const std::pair<const wxChar*, const wxChar*> bad[] = {
		{ wxT("X-A"), wxT("v\r\nX-Evil: 1") }, { wxT("Bad Name"), wxT("v") }, { wxT("X:B"), wxT("v") },
		// ...and the fields that say how the message is framed and carried, which are the engine's: a
		// Content-Length that is not the body's length is a request smuggled past a proxy.
		{ wxT("Content-Length"), wxT("5") }, { wxT("transfer-encoding"), wxT("chunked") }, { wxT("Host"), wxT("other") },
		{ wxT("Connection"), wxT("close") }, { wxT("Expect"), wxT("100-continue") }, { wxT("Upgrade"), wxT("h2c") },
		{ wxT("Proxy-Authorization"), wxT("Basic x") } };
	for (const auto& one : bad) {
		ibValueHttpRequest request;
		const ibValuePtr<ibValueStructure> headers(new ibValueStructure());
		headers->Insert(Text(one.first), Text(one.second));
		request.SetHeaders(headers);
		const size_t before = Count();
		const wxString why = Refusal([&] { Ask(connection, ibHttpMethod_Get, request); });
		EXPECT_FALSE(why.empty()) << Utf8(one.first);
		// The library refuses a bad field as well, with two words for the whole request. Ours is there to say WHICH.
		EXPECT_TRUE(why.Contains(wxString(wxT("'")) + one.first + wxT("'"))) << "the refusal does not name the header: " << Utf8(why);
		EXPECT_EQ(Count(), before) << "nothing reached the server";
	}
}

// A value pasted with a space at its end goes out without it - the library would refuse the whole request for
// that, in two words - and a value that is not a string or a number is refused by name rather than sent as the
// name of its type.
TEST_F(HttpClient, AHeaderValueIsTrimmedAndHasToBeAStringOrANumber)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	ibValueHttpRequest request;
	const ibValuePtr<ibValueStructure> headers(new ibValueStructure());
	headers->Insert(Text(wxT("Authorization")), Text(wxT("  Bearer t0ken \t")));
	headers->Insert(Text(wxT("X-Count")), ibValue(42));
	headers->Insert(Text(wxT("X-Name")), Text(wxString::FromUTF8("\xD0\x91\xD0\xBE\xD1\x80\xD1\x89")));
	request.SetHeaders(headers);
	Ask(connection, ibHttpMethod_Get, request);
	const ibSeen seen = Last();
	EXPECT_EQ(seen.m_headers.find("Authorization")->second, std::string("Bearer t0ken"));
	EXPECT_EQ(seen.m_headers.find("X-Count")->second, std::string("42"));
	EXPECT_EQ(seen.m_headers.find("X-Name")->second, std::string("\xD0\x91\xD0\xBE\xD1\x80\xD1\x89")) << "not ASCII goes out as its UTF-8 bytes, as the protocol's obs-text";

	const ibValuePtr<ibValueStructure> inner(new ibValueStructure());
	headers->Insert(Text(wxT("X-Object")), inner);
	const size_t before = Count();
	const wxString why = Refusal([&] { Ask(connection, ibHttpMethod_Get, request); });
	ASSERT_FALSE(why.empty());
	EXPECT_TRUE(why.Contains(wxT("'X-Object'"))) << Utf8(why);
	EXPECT_EQ(Count(), before);
}

// The library adds `Expect: 100-continue` to a body of 1 KB or more by itself, and against a server that does
// not speak it - an older proxy, a register's firmware - the body waits a second before it goes. Switched
// off for the whole build (CPPHTTPLIB_EXPECT_100_THRESHOLD=0 in the root CMakeLists.txt and in
// ConfigurationDefs.props): a body goes when the request goes, and a script cannot be surprised by a header
// it may not set.
TEST_F(HttpClient, ALargeBodyGoesWithoutAnExpectTheScriptCouldNotSet)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	ibValueHttpRequest request;
	const std::string body(4096, 'x');
	request.SetBodyBytes(body.data(), body.size());
	const ibValuePtr<ibValueHttpResponse> response = Ask(connection, ibHttpMethod_Post, request);
	ASSERT_TRUE(response != nullptr);
	EXPECT_EQ(response->StatusCode(), 200);
	EXPECT_EQ(Last().m_headers.count("Expect"), 0u) << "the library added the header on its own";
	EXPECT_EQ(Last().m_body.size(), body.size());
}

TEST_F(HttpClient, AnErrorStatusIsAnAnswerNotAFailure)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	ibValueHttpRequest request;
	request.SetResourceAddress(wxT("/status/404"));
	const ibValuePtr<ibValueHttpResponse> missing = Ask(connection, ibHttpMethod_Get, request);
	ASSERT_TRUE(missing != nullptr);
	EXPECT_EQ(missing->StatusCode(), 404);
	EXPECT_EQ(missing->BodyAsString(ibTextEncoding_UTF8), wxString(wxT("no such thing")));

	request.SetResourceAddress(wxT("/status/500"));
	const ibValuePtr<ibValueHttpResponse> failed = Ask(connection, ibHttpMethod_Get, request);
	ASSERT_TRUE(failed != nullptr);
	EXPECT_EQ(failed->StatusCode(), 500);
}

TEST_F(HttpClient, ARepeatedResponseHeaderIsJoinedAndTheLastCookieStays)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	ibValueHttpRequest request;
	request.SetResourceAddress(wxT("/twice"));
	const ibValuePtr<ibValueHttpResponse> response = Ask(connection, ibHttpMethod_Get, request);
	ASSERT_TRUE(response != nullptr);
	const ibValueStructure& headers = response->Headers();

	const long tag = headers.FindProp(wxT("x-tag"));
	ASSERT_GE(tag, 0) << "and a header's name folds case";
	EXPECT_EQ(headers.Entries()[static_cast<size_t>(tag)].second.GetString(), wxString(wxT("a, b")));

	const long cookie = headers.FindProp(wxT("Set-Cookie"));
	ASSERT_GE(cookie, 0);
	EXPECT_EQ(headers.Entries()[static_cast<size_t>(cookie)].second.GetString(), wxString(wxT("second=2")));
}

TEST_F(HttpClient, BasicAuthorizationGoesWithEveryRequestUnlessTheRequestSaysItsOwn)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxT("user"), wxT("pass"), 10);
	ibValueHttpRequest request;
	Ask(connection, ibHttpMethod_Get, request);
	ASSERT_EQ(Last().m_headers.count("Authorization"), 1u);
	EXPECT_EQ(Last().m_headers.find("Authorization")->second, std::string("Basic dXNlcjpwYXNz"));   // base64("user:pass"), worked out by hand

	const ibValuePtr<ibValueStructure> headers(new ibValueStructure());
	headers->Insert(Text(wxT("authorization")), Text(wxT("Bearer t0ken")));
	request.SetHeaders(headers);
	Ask(connection, ibHttpMethod_Get, request);
	const ibSeen seen = Last();
	ASSERT_EQ(seen.m_headers.count("Authorization"), 1u) << "one, not two";
	EXPECT_EQ(seen.m_headers.find("Authorization")->second, std::string("Bearer t0ken"));
}

TEST_F(HttpClient, TextThatIsNotAsciiInAnAddressArrivesAsTheSameText)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	ibValueHttpRequest request;
	request.SetResourceAddress(wxString::FromUTF8("/echo/a,b;c+d?name=\xD0\x91\xD0\xBE\xD1\x80\xD1\x89&pair=a%20b"));
	Ask(connection, ibHttpMethod_Get, request);
	const ibSeen seen = Last();
	// The request line as it went out: what is not ASCII is %XX of its UTF-8, and NOTHING ELSE is touched - the
	// library's own encoder would also rewrite ',' ';' and '+', and an address encoded by two rules is one
	// nobody can predict. Its encoding is switched off; this is what says it stays off.
	EXPECT_EQ(seen.m_target, std::string("/echo/a,b;c+d?name=%D0%91%D0%BE%D1%80%D1%89&pair=a%20b"));
	EXPECT_EQ(seen.m_path, std::string("/echo/a,b;c+d"));
	ASSERT_EQ(seen.m_params.count("name"), 1u);
	EXPECT_EQ(seen.m_params.find("name")->second, std::string("\xD0\x91\xD0\xBE\xD1\x80\xD1\x89"));
	ASSERT_EQ(seen.m_params.count("pair"), 1u);
	EXPECT_EQ(seen.m_params.find("pair")->second, std::string("a b")) << "what was already encoded is not encoded twice";
}

TEST_F(HttpClient, AServerThatSaysNothingIsLeftAfterTheTimeAllowedAndTheConnectionLivesOn)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 1);
	ibValueHttpRequest request;
	request.SetResourceAddress(wxT("/quiet"));
	const auto started = std::chrono::steady_clock::now();
	const wxString why = Refusal([&] { Ask(connection, ibHttpMethod_Get, request); });
	const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	ASSERT_FALSE(why.empty()) << "a late answer was taken";
	EXPECT_TRUE(why.Contains(wxT("127.0.0.1")) && why.Contains(wxString::Format(wxT("%d"), m_port))) << Utf8(why);
	EXPECT_TRUE(why.Contains(wxT("within the 1 s"))) << "it is time that ran out, and the sentence says how much: " << Utf8(why);
	EXPECT_GE(took, 0.9);
	EXPECT_LT(took, 2.8) << "the time allowed bounds the whole request";

	request.SetResourceAddress(wxT("/echo"));
	const ibValuePtr<ibValueHttpResponse> after = Ask(connection, ibHttpMethod_Get, request);
	ASSERT_TRUE(after != nullptr);
	EXPECT_EQ(after->StatusCode(), 200) << "the same connection, after a failure";
}

// A server that keeps talking, slowly. Every read is answered in time, so a per-read timeout never fires: the
// time allowed has to bound the request as a whole - and the connection has to be usable afterwards, with half
// an answer still on its way down the old socket.
TEST_F(HttpClient, AServerThatDripsIsLeftAfterTheTimeAllowedToo)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 1);
	ibValueHttpRequest request;
	request.SetResourceAddress(wxT("/drip"));
	const auto started = std::chrono::steady_clock::now();
	const wxString why = Refusal([&] { Ask(connection, ibHttpMethod_Get, request); });
	const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	ASSERT_FALSE(why.empty()) << "ten bytes over four seconds were waited for, with one second allowed";
	EXPECT_TRUE(why.Contains(wxT("within the 1 s"))) << Utf8(why);
	EXPECT_LT(took, 2.8);

	request.SetResourceAddress(wxT("/status/404"));
	const ibValuePtr<ibValueHttpResponse> after = Ask(connection, ibHttpMethod_Get, request);
	ASSERT_TRUE(after != nullptr);
	EXPECT_EQ(after->StatusCode(), 404) << "the next answer is the next request's, not the tail of the old one";
	EXPECT_EQ(after->BodyAsString(ibTextEncoding_UTF8), wxString(wxT("no such thing")));
}

TEST_F(HttpClient, AClosedPortIsSaidWithTheHostAndThePort)
{
	int closed = 0;
	{
		httplib::Server spare;                                // a port the system just gave out, and nobody listening on it
		closed = spare.bind_to_any_port("127.0.0.1");
		ASSERT_GT(closed, 0);
	}
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), closed, wxString(), wxString(), 3);
	ibValueHttpRequest request;
	const wxString why = Refusal([&] { Ask(connection, ibHttpMethod_Get, request); });
	ASSERT_FALSE(why.empty());
	EXPECT_TRUE(why.Contains(wxT("127.0.0.1")) && why.Contains(wxString::Format(wxT("%d"), closed))) << Utf8(why);
}

// A redirect is an answer. Followed by the library it decoded the Location before sending it, turned a POST
// into a GET on a 303 without a word, and raised for a server that DID answer when it could not go on.
TEST_F(HttpClient, ARedirectIsAnAnswerWithItsLocationNotARoadTakenBehindTheScriptsBack)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 5);
	ibValueHttpRequest request;
	request.SetResourceAddress(wxT("/moved"));
	const ibValuePtr<ibValueHttpResponse> moved = Ask(connection, ibHttpMethod_Get, request);
	ASSERT_TRUE(moved != nullptr);
	EXPECT_EQ(moved->StatusCode(), 302);
	const long location = moved->Headers().FindProp(wxT("location"));
	ASSERT_GE(location, 0);
	EXPECT_EQ(moved->Headers().Entries()[static_cast<size_t>(location)].second.GetString(), wxString(wxT("/echo")));
	EXPECT_EQ(Last().m_path, std::string("/moved")) << "one request went out, and it was this one";

	request.SetResourceAddress(wxT("/see-other"));
	request.SetBodyText(wxT("an order"), ibTextEncoding_UTF8);
	const ibValuePtr<ibValueHttpResponse> seeOther = Ask(connection, ibHttpMethod_Post, request);
	ASSERT_TRUE(seeOther != nullptr);
	EXPECT_EQ(seeOther->StatusCode(), 303);
	EXPECT_EQ(Last().m_method, std::string("POST")) << "and no GET was made of it";
}

// The enumeration's road through the script's door, the timeout changed between two requests, one Structure
// given to two requests, an answer with nothing in it.
TEST_F(HttpClient, WhatTheScriptsDoorAndTheOddAnswersDo)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 10);
	ibValueHttpRequest request;
	ibValue out;

	// CallMethod with a real HTTPMethod value - the only road the enumeration's own dispatch is on.
	ibValue method = ibValue::CreateEnumObject<ibValueEnumHttpMethod>(ibHttpMethod_Options);
	ibValue* args[] = { &method, &request };
	const long callMethod = connection.FindMethod(wxT("CallMethod"));
	ASSERT_GE(callMethod, 0);
	ASSERT_TRUE(connection.CallAsFunc(callMethod, out, args, 2));
	EXPECT_EQ(Last().m_method, std::string("OPTIONS"));
	EXPECT_EQ(Last().m_headers.count("Content-Type"), 0u) << "no body, no type";

	// A POST with nothing in it says so; a HEAD and a 204 come back with nothing and no refusal.
	Ask(connection, ibHttpMethod_Post, request);
	EXPECT_EQ(Last().m_headers.find("Content-Length")->second, std::string("0"));
	const ibValuePtr<ibValueHttpResponse> head = Ask(connection, ibHttpMethod_Head, request);
	ASSERT_TRUE(head != nullptr);
	EXPECT_TRUE(head->Body().empty());
	request.SetResourceAddress(wxT("/status/204"));
	const ibValuePtr<ibValueHttpResponse> nothing = Ask(connection, ibHttpMethod_Get, request);
	ASSERT_TRUE(nothing != nullptr);
	EXPECT_EQ(nothing->StatusCode(), 204);
	EXPECT_TRUE(nothing->BodyAsString(ibTextEncoding_UTF8).empty());

	// One Structure, two requests: what is added to it is seen by both.
	const ibValuePtr<ibValueStructure> shared(new ibValueStructure());
	shared->Insert(Text(wxT("X-Shared")), Text(wxT("1")));
	ibValueHttpRequest other;
	request.SetHeaders(shared);
	other.SetHeaders(shared);
	shared->Insert(Text(wxT("X-Later")), Text(wxT("2")));
	Ask(connection, ibHttpMethod_Get, other);
	EXPECT_EQ(Last().m_headers.count("X-Later"), 1u);

	// The time allowed, changed after a request was made, is the next request's.
	request.SetResourceAddress(wxT("/quiet"));
	connection.SetTimeout(1);
	const auto started = std::chrono::steady_clock::now();
	EXPECT_FALSE(Refusal([&] { Ask(connection, ibHttpMethod_Get, request); }).empty());
	EXPECT_LT(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(), 2.8);
}

// A peer that hangs up while the body is still on its way. Off Windows the write raises SIGPIPE, whose default
// ends the process - the daemon, on a bank that dropped the line - unless the send says MSG_NOSIGNAL (Linux, on
// the whole target) or the socket says SO_NOSIGPIPE (macOS). The library's Server ignores the signal in its
// constructor, which is why a test binary would never see this: the default is put back for the duration.
TEST_F(HttpClient, APeerThatHangsUpMidUploadIsASentenceNotTheEndOfTheProcess)
{
	httplib::Server tight;
	tight.set_payload_max_length(1024);                      // answers 413 after the headers and closes the line
	tight.Post(".*", [](const httplib::Request&, httplib::Response& response) { response.set_content("never", "text/plain"); });
	const int port = tight.bind_to_any_port("127.0.0.1");
	ASSERT_GT(port, 0);
	std::thread thread([&tight] { tight.listen_after_bind(); });
	tight.wait_until_ready();
#ifndef _WIN32
	struct ibDefaultSigpipe {
		void (*m_before)(int);
		ibDefaultSigpipe() : m_before(signal(SIGPIPE, SIG_DFL)) {}
		~ibDefaultSigpipe() { signal(SIGPIPE, m_before); }
	} const restored;
#endif

	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), port, wxString(), wxString(), 10);
	ibValueHttpRequest request;
	const std::string big(8u * 1024u * 1024u, 'x');
	request.SetBodyBytes(big.data(), big.size());
	ibValuePtr<ibValueHttpResponse> answer;
	const wxString why = Refusal([&] { answer = Ask(connection, ibHttpMethod_Post, request); });
	// Either the 413 got through before the line dropped, or the drop is a sentence. Never a dead process.
	EXPECT_TRUE(!why.empty() || (answer != nullptr && answer->StatusCode() == 413)) << Utf8(why);

	tight.stop();
	thread.join();
}

TEST_F(HttpClient, EightMegabytesGoThereAndBackByteForByte)
{
	std::string big(8u * 1024u * 1024u, '\0');
	unsigned int state = 12345u;
	for (char& c : big) {
		state = state * 1664525u + 1013904223u;
		c = static_cast<char>(state >> 24);
	}
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 60);
	ibValueHttpRequest request;
	request.SetBodyBytes(big.data(), big.size());
	const ibValuePtr<ibValueHttpResponse> response = Ask(connection, ibHttpMethod_Post, request);
	ASSERT_TRUE(response != nullptr);
	ASSERT_EQ(response->StatusCode(), 200);
	EXPECT_TRUE(Last().m_body == big) << "what arrived";
	EXPECT_TRUE(response->Body() == big) << "what came back";
}

TEST_F(HttpClient, BytesThatAreNotTextAreBytes)
{
	ibValueHttpConnection connection;
	connection.Open(wxT("127.0.0.1"), m_port, wxString(), wxString(), 5);
	ibValueHttpRequest request;
	request.SetResourceAddress(wxT("/not-text"));
	const ibValuePtr<ibValueHttpResponse> response = Ask(connection, ibHttpMethod_Get, request);
	ASSERT_TRUE(response != nullptr);
	EXPECT_EQ(response->Body().size(), 3u);
	EXPECT_FALSE(Refusal([&] { response->BodyAsString(ibTextEncoding_UTF8); }).empty());
}
