////////////////////////////////////////////////////////////////////////////
//	Description : a base's web addresses - the core's mounts a web server makes of them, and the registry that holds
//	              them (backend/served/servedAddressRegistry.h)
////////////////////////////////////////////////////////////////////////////
//
// THE MOUNTS. ibHttpServer::Mount(prefix, handler) answers every method under the prefix, the longest prefix first, and
// Unmount takes it off while the server serves: what a web server does with each address its base tells it of.
//
// THE REGISTRY. The entries a session registers live in memory and go with the session; a path is refused by name; a
// listener hears every change; a request is handed to its entry's kind. The kept entries (no owner) are written to the
// base under its lock manager, so with no base open they are refused - a base's own suite covers them.

#include <gtest/gtest.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "backend/appDataCtorToken.h"
#include "backend/backend_exception.h"
#include "backend/served/servedAddressRegistry.h"
#include "core/net/httpServer.h"
#include "protocol/protocol.h"

#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include <cpp-httplib/httplib.h>

namespace {

// A server on a port the system chooses, stopped with the test.
struct Served {
	ibHttpServer m_server;
	int          m_port = 0;
	std::string  m_error;

	void Start()
	{
		ibHttpServerOptions options;
		options.m_threads = 1;
		options.m_workers = 2;
		options.m_host = "127.0.0.1";
		options.m_httpPort = 0;
		if (m_server.Start(options, m_error))
			m_port = m_server.GetHttpPort();
	}
	~Served() { m_server.Stop(); }
};

// A handler that answers with what it was asked: the method and the path.
ibHttpHandler Mirror(const std::string& who)
{
	return [who](const ibHttpRequest& request, ibHttpResponder respond) {
		ibHttpResponse response;
		response.SetContent(who + " " + request.m_method + " " + request.m_path + " " + request.m_body, "text/plain");
		respond(std::move(response));
	};
}

// THE TEST KIND — answers with its entry's path and the prefix it was handed; refuses an entry whose path ends in
// "refused".
class ibTestServedKind : public ibServedKind {
public:
	ibHttpResponse Answer(const ibServedAddress& address, const ibHttpRequest& request, const ibString& prefix) const override
	{
		ibHttpResponse response;
		response.m_status = 201;
		response.SetContent(std::string(address.m_path.ToUtf8()) + "|" + std::string(prefix.ToUtf8()) + "|" + request.m_method,
			"text/plain");
		return response;
	}

	void Check(const ibServedAddress& address) const override
	{
		if (address.m_path.EndsWith(ibT("refused")))
			ibBackendCoreException::Error(ibT("the test kind refuses %s"), address.m_path);
	}

	std::vector<ibServedAddress> Offer() const override
	{
		ibServedAddress offer;
		offer.m_kind = ibT("test");
		offer.m_path = ibT("es/offered");
		return { offer };
	}
};

SERVED_KIND_REGISTER(ibTestServedKind, "test");

// An entry of a session — the kind that answers it, its path.
ibServedAddress Owned(const ibString& path, const ibGuid& owner)
{
	ibServedAddress address;
	address.m_kind = ibT("test");
	address.m_path = path;
	address.m_owner = owner;
	return address;
}

ibString Refusal(const std::function<void()>& register_)
{
	try {
		register_();
	}
	catch (const ibCoreException& err) {
		return err.GetErrorDescription();
	}
	return ibString();
}

} // namespace

//*******************************************************************************************
//*                                       The mounts                                        *
//*******************************************************************************************

// A handler mount takes any method under its prefix; the longest prefix answers; an exact route comes first.
TEST(ServedMount, AHandlerTakesEveryMethodAndTheLongestPrefixAnswers)
{
	Served served;
	served.m_server.Get("/pub/client", Mirror("route"));
	served.m_server.Mount("/pub/", Mirror("pub"));
	served.m_server.Mount("/pub/es/desk/", Mirror("desk"));
	served.Start();
	ASSERT_NE(served.m_port, 0) << served.m_error;

	httplib::Client client("127.0.0.1", served.m_port);
	const auto put = client.Put("/pub/es/desk/orders/1", "body", "text/plain");
	ASSERT_TRUE(put);
	EXPECT_EQ(put->status, 200);
	EXPECT_EQ(put->body, "desk PUT /pub/es/desk/orders/1 body");

	const auto other = client.Delete("/pub/es/other");
	ASSERT_TRUE(other);
	EXPECT_EQ(other->body, "pub DELETE /pub/es/other ");

	const auto route = client.Get("/pub/client");
	ASSERT_TRUE(route);
	EXPECT_EQ(route->body, "route GET /pub/client ");
}

// Mounted and unmounted while the server serves: the next request sees the change; a method with nothing to take it is
// 405, a path with nothing there 404.
TEST(ServedMount, MountsChangeWhileTheServerServes)
{
	Served served;
	served.Start();
	ASSERT_NE(served.m_port, 0) << served.m_error;
	httplib::Client client("127.0.0.1", served.m_port);

	const auto before = client.Get("/pub/es/desk/");
	ASSERT_TRUE(before);
	EXPECT_EQ(before->status, 404);

	served.m_server.Mount("/pub/es/desk/", Mirror("desk"));
	const auto mounted = client.Post("/pub/es/desk/x", "", "text/plain");
	ASSERT_TRUE(mounted);
	EXPECT_EQ(mounted->status, 200);

	// A second mount of the prefix replaces the first.
	served.m_server.Mount("/pub/es/desk/", Mirror("again"));
	const auto replaced = client.Get("/pub/es/desk/x");
	ASSERT_TRUE(replaced);
	EXPECT_EQ(replaced->body, "again GET /pub/es/desk/x ");

	served.m_server.Unmount("/pub/es/desk/");
	const auto gone = client.Get("/pub/es/desk/x");
	ASSERT_TRUE(gone);
	EXPECT_EQ(gone->status, 404);
	const auto put = client.Put("/pub/es/desk/x", "", "text/plain");
	ASSERT_TRUE(put);
	EXPECT_EQ(put->status, 405);
}

// A file mount answers GET alone: another method under it is 405, not the file.
TEST(ServedMount, AFileMountAnswersGetAlone)
{
	Served served;
	served.m_server.Mount("/site/", std::map<std::string, std::string>{ { "index.html", "<p>page</p>" } });
	served.Start();
	ASSERT_NE(served.m_port, 0) << served.m_error;
	httplib::Client client("127.0.0.1", served.m_port);

	const auto page = client.Get("/site/index.html");
	ASSERT_TRUE(page);
	EXPECT_EQ(page->status, 200);
	EXPECT_EQ(page->body, "<p>page</p>");
	const auto put = client.Put("/site/index.html", "", "text/plain");
	ASSERT_TRUE(put);
	EXPECT_EQ(put->status, 405);
}

//*******************************************************************************************
//*                                      The registry                                       *
//*******************************************************************************************

// A session's entry: registered with an id minted, listed, answered by its kind, gone with the session.
TEST(ServedAddressRegistry, ASessionsEntryIsServedAndGoesWithTheSession)
{
	ibServedAddressRegistry registry{ ib::AppDataCtorToken{} };
	const ibGuid session = ibGuid::newGuid();

	const ibGuid id = registry.Register(Owned(ibT("es/desk"), session));
	ASSERT_TRUE(id.isValid());
	const std::vector<ibServedAddress> listed = registry.GetServedAddressArray();
	ASSERT_EQ(listed.size(), 1u);
	EXPECT_EQ(listed[0].m_id, id);
	EXPECT_FALSE(listed[0].IsKept());

	ibHttpRequest request;
	request.m_method = "POST";
	const ibHttpResponse answer = registry.Serve(id, request, ibT("/pub/es/desk"));
	EXPECT_EQ(answer.m_status, 201);
	EXPECT_EQ(answer.m_body, "es/desk|/pub/es/desk|POST");

	// Another session's end takes nothing.
	registry.OnSessionEnd(ibGuid::newGuid());
	EXPECT_EQ(registry.GetServedAddressArray().size(), 1u);

	registry.OnSessionEnd(session);
	EXPECT_TRUE(registry.GetServedAddressArray().empty());
	EXPECT_EQ(registry.Serve(id, request, ibT("/pub/es/desk")).m_status, 404);
}

// What a path is not, refused by name — and a path another entry has, in any case.
TEST(ServedAddressRegistry, APathThatIsNotOneIsRefused)
{
	ibServedAddressRegistry registry{ ib::AppDataCtorToken{} };
	const ibGuid session = ibGuid::newGuid();

	for (const ibString path : { ibString(), ibString(ibT("/es")), ibString(ibT("es/")), ibString(ibT("es//desk")),
		ibString(ibT("es/../desk")), ibString(ibT("es/./desk")), ibString(ibT("client")), ibString(ibT("client/x")),
		ibString(ibT("es/d esk")), ibString(ibT("es?x")) }) {
		const ibString refusal = Refusal([&]() { registry.Register(Owned(path, session)); });
		EXPECT_FALSE(refusal.IsEmpty()) << path.ToUtf8();
		EXPECT_NE(refusal.Find(ibT("is not registered")), wxNOT_FOUND) << refusal.ToUtf8();
	}
	EXPECT_TRUE(registry.GetServedAddressArray().empty());

	// A segment named client further down is a path like any other.
	EXPECT_TRUE(registry.Register(Owned(ibT("es/client"), session)).isValid());

	registry.Register(Owned(ibT("es/Desk"), session));
	const ibString taken = Refusal([&]() { registry.Register(Owned(ibT("ES/desk"), session)); });
	EXPECT_NE(taken.Find(ibT("answers there already")), wxNOT_FOUND) << taken.ToUtf8();
}

// A kind nobody registered, and an entry its kind does not take, are refused before anything is listed.
TEST(ServedAddressRegistry, AKindAndItsCheckAreAsked)
{
	ibServedAddressRegistry registry{ ib::AppDataCtorToken{} };
	const ibGuid session = ibGuid::newGuid();

	ibServedAddress unknown = Owned(ibT("es/x"), session);
	unknown.m_kind = ibT("no such kind");
	EXPECT_NE(Refusal([&]() { registry.Register(unknown); }).Find(ibT("no kind of that name")), wxNOT_FOUND);

	EXPECT_NE(Refusal([&]() { registry.Register(Owned(ibT("es/refused"), session)); }).Find(ibT("the test kind refuses")),
		wxNOT_FOUND);
	EXPECT_TRUE(registry.GetServedAddressArray().empty());

	// The kinds, by name, with what each offers.
	const ibServedKind* const kind = ibServedKind::Find(ibT("test"));
	ASSERT_NE(kind, nullptr);
	ASSERT_EQ(kind->Offer().size(), 1u);
	EXPECT_EQ(kind->Offer()[0].m_path, ibT("es/offered"));
	const std::vector<ibString> names = ibServedKind::GetKindNames();
	EXPECT_NE(std::find(names.begin(), names.end(), ibString(ibT("test"))), names.end());
}

// A listener hears the whole list at every change, until it is forgotten.
TEST(ServedAddressRegistry, AListenerHearsEveryChange)
{
	ibServedAddressRegistry registry{ ib::AppDataCtorToken{} };
	const ibGuid session = ibGuid::newGuid();

	std::vector<std::size_t> heard;
	const std::uint64_t listening = registry.Listen([&heard](const std::vector<ibServedAddress>& addresses) {
		heard.push_back(addresses.size());
	});

	const ibGuid first = registry.Register(Owned(ibT("es/b"), session));
	registry.Register(Owned(ibT("es/a"), session));
	ASSERT_EQ(heard, (std::vector<std::size_t>{ 1, 2 }));

	// In the order of path.
	const std::vector<ibServedAddress> listed = registry.GetServedAddressArray();
	ASSERT_EQ(listed.size(), 2u);
	EXPECT_EQ(listed[0].m_path, ibT("es/a"));

	registry.Unregister(first);
	EXPECT_EQ(heard.back(), 1u);

	registry.Forget(listening);
	registry.OnSessionEnd(session);
	EXPECT_EQ(heard.size(), 3u);
	EXPECT_TRUE(registry.GetServedAddressArray().empty());
}

// An entry of nobody's is the base's to keep: with no base open it is refused, as is taking off an id that names
// nothing.
TEST(ServedAddressRegistry, AKeptEntryNeedsTheBase)
{
	ibServedAddressRegistry registry{ ib::AppDataCtorToken{} };
	EXPECT_NE(Refusal([&]() { registry.Register(Owned(ibT("es/kept"), ibGuid())); }).Find(ibT("the base is not open")),
		wxNOT_FOUND);
	EXPECT_FALSE(Refusal([&]() { registry.Unregister(ibGuid::newGuid()); }).IsEmpty());
	EXPECT_TRUE(registry.GetServedAddressArray().empty());
}

// The protocol's two methods, by the names they go by on the wire — and back.
TEST(ServedAddressProtocol, TheMethodsGoByTheirNames)
{
	EXPECT_STREQ(ibProtocol::MethodName(ibProtocol::ibMethod::Addresses), "addresses");
	EXPECT_STREQ(ibProtocol::MethodName(ibProtocol::ibMethod::Serve), "serve");
	EXPECT_EQ(ibProtocol::MethodFromName("addresses"), ibProtocol::ibMethod::Addresses);
	EXPECT_EQ(ibProtocol::MethodFromName("serve"), ibProtocol::ibMethod::Serve);
	EXPECT_EQ(ibProtocol::MethodFromName("mcp"), ibProtocol::ibMethod::Mcp);
	EXPECT_STREQ(ibProtocol::ibRpc::Addresses, "addresses");
}
