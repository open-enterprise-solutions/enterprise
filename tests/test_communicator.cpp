// The communicator — the client's side of the protocol (frmclient/communicator.h), against a connection scripted here:
// its ids stay monotonic when the socket drops and it logs in by token, a channel of its own opens again and a call it
// lost is not sent twice, and what it holds goes with the next call. How the server reads what it sent is
// tests/test_sessionFailover.cpp.

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <vector>

#include "frmclient/communicator.h"
#include "protocol/connection.h"
#include "protocol/protocolNode.h"

namespace {

// A connection that fails one exchange, then accepts the login and the resent call.
class ScriptedConnection : public ibProtocolConnection {
public:
	std::vector<ibMemoryBuffer> sent;
	std::vector<bool>           fail;
	std::vector<ibMemoryBuffer> answers;
	int                         reconnects = 0;

	bool Exchange(const ibMemoryBuffer& request, ibProtocolNode& answer, ibProtocol::ibRefusal& refusal,
		ibString& error) override
	{
		sent.push_back(request);
		if (fail.empty()) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = wxT("no script");
			return false;
		}
		const bool drop = fail.front();
		fail.erase(fail.begin());
		if (drop) {
			refusal = ibProtocol::ibRefusal::NoSession;
			error = wxT("the connection to the server is closed");
			return false;
		}
		// The packet scripted, read as a connection reads what came.
		const ibMemoryBuffer packet = answers.front();
		answers.erase(answers.begin());
		if (!ibProtocolNode::Read(packet, answer)) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = wxT("the scripted answer is not a packet");
			return false;
		}
		return true;
	}
	bool Reconnect(ibString&) override
	{
		++reconnects;
		return true;
	}
	void Listen(std::function<void(const ibProtocolNode&)>) override {}
};

ibMemoryBuffer Answer(long long id, const ibProtocolNode& result)
{
	ibProtocolNode root;
	root.SetValue(ibProtocol::ibRpc::Id, id)
		.SetValue(ibProtocol::ibRpc::Result, result);
	ibMemoryBuffer packet;
	root.Write(packet);
	return packet;
}

// An answer written by hand, as its JSON view — the packet of it.
ibMemoryBuffer Packet(const ibString& text)
{
	ibProtocolNode node;
	EXPECT_TRUE(ibProtocolNode::Read(text, node)) << text.ToUtf8();
	ibMemoryBuffer packet;
	node.Write(packet);
	return packet;
}

ibProtocolNode NodeOf(const ibMemoryBuffer& packet)
{
	ibProtocolNode node;
	EXPECT_TRUE(ibProtocolNode::Read(packet, node));
	return node;
}

long long IdOf(const ibMemoryBuffer& packet)
{
	return NodeOf(packet).GetInt(ibProtocol::ibRpc::Id);
}

} // namespace

TEST(SessionFailoverClient, IdsStayMonotonicAcrossReconnect)
{
	auto script = std::make_unique<ScriptedConnection>();
	ScriptedConnection* const raw = script.get();

	raw->fail = { false, true, false, false, false };
	// Features is a list of strings. The node writer has no string-list, so the password login's answer is the wire.
	raw->answers = {
		Packet("{\"id\":1,\"result\":{\"Client\":\"client-1\",\"Protocol\":2,\"Token\":\"tok\","
			"\"Features\":[\"resume\"],\"Frame\":1,\"Title\":\"Home\"}}"),
		Answer(3, [&]() {
			ibProtocolNode resumed;
			resumed.SetValue(ibProtocol::ibLogin::Client, ibString(wxT("client-1")))
				.SetValue(ibProtocol::ibLogin::Protocol, 2);
			return resumed;
		}()),
		Answer(2, [&]() {
			ibProtocolNode frame;
			frame.SetValue(ibProtocol::ibFrame::Frame, 2).SetValue(ibProtocol::ibFrame::Title, ibString(wxT("Home")));
			return frame;
		}()),
		Answer(4, [&]() {
			ibProtocolNode frame;
			frame.SetValue(ibProtocol::ibFrame::Frame, 3).SetValue(ibProtocol::ibFrame::Title, ibString(wxT("Home")));
			return frame;
		}()),
	};

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;

	ASSERT_TRUE(communicator.Login(wxT("user"), wxT("secret"), ibProtocol::ibLogin::ibMode::Runtime, result, refusal, error)) << error;

	ibProtocolNode params;
	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_EQ(raw->reconnects, 1);
	ASSERT_GE(raw->sent.size(), 4u);
	// 0 password login, 1 the frame that dropped, 2 login {Token}, 3 the same frame again.
	EXPECT_EQ(IdOf(raw->sent[1]), 2);
	EXPECT_EQ(IdOf(raw->sent[2]), 3);
	EXPECT_EQ(IdOf(raw->sent[3]), 2);
	EXPECT_TRUE(NodeOf(raw->sent[2]).FindChild(ibProtocol::ibRpc::Params).Has(ibProtocol::ibLogin::Token));

	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_EQ(IdOf(raw->sent.back()), 4);
}

namespace {

// A second connection whose socket can go: a dropped exchange closes it, as the real one's does.
class ScriptedChannel : public ScriptedConnection {
public:
	bool open = true;

	bool Exchange(const ibMemoryBuffer& request, ibProtocolNode& answer, ibProtocol::ibRefusal& refusal,
		ibString& error) override
	{
		const bool exchanged = ScriptedConnection::Exchange(request, answer, refusal, error);
		open = exchanged;
		return exchanged;
	}
	bool IsOpen() const override { return open; }
};

// …and the connection that hands those out, in order.
class ScriptedChannelOwner : public ScriptedConnection {
public:
	std::vector<std::unique_ptr<ibProtocolConnection>> channels;

	std::unique_ptr<ibProtocolConnection> OpenChannel(ibString& error) override
	{
		if (channels.empty()) {
			error = wxT("no script");
			return nullptr;
		}
		std::unique_ptr<ibProtocolConnection> channel = std::move(channels.front());
		channels.erase(channels.begin());
		return channel;
	}
};

ibMemoryBuffer ChannelKey(long long id, const ibString& key)
{
	ibProtocolNode result;
	result.SetValue(ibProtocol::ibMcp::Channel, key);
	return Answer(id, result);
}

ibMemoryBuffer McpAnswer(long long id, const ibString& message)
{
	ibProtocolNode result;
	result.SetValue(ibProtocol::ibMcp::Message, message).SetValue(ibProtocol::ibMcp::Status, 200);
	return Answer(id, result);
}

int McpCommandOf(const ibMemoryBuffer& packet)
{
	return static_cast<int>(NodeOf(packet).FindChild(ibProtocol::ibRpc::Params).GetInt(ibProtocol::ibMcp::Command));
}

} // namespace

TEST(SessionFailoverClient, AssistantChannelOpensAgainAndALostCallIsNotSentAgain)
{
	const int listen = static_cast<int>(ibProtocol::ibMcp::ibCommand::Listen);
	const int message = static_cast<int>(ibProtocol::ibMcp::ibCommand::Message);

	auto first = std::make_unique<ScriptedChannel>();
	ScriptedChannel* const firstRaw = first.get();
	firstRaw->fail = { false, true };
	firstRaw->answers = { McpAnswer(1, ibString()) };

	// The channel's ids go on across the connections, as the client's do.
	auto second = std::make_unique<ScriptedChannel>();
	ScriptedChannel* const secondRaw = second.get();
	secondRaw->fail = { false, false };
	secondRaw->answers = { McpAnswer(3, ibString()), McpAnswer(4, wxT("answered")) };

	auto script = std::make_unique<ScriptedChannelOwner>();
	ScriptedChannelOwner* const raw = script.get();
	raw->channels.push_back(std::move(first));
	raw->channels.push_back(std::move(second));
	raw->fail = { false, false, false };
	raw->answers = {
		Packet("{\"id\":1,\"result\":{\"Client\":\"client-1\",\"Protocol\":7,\"Token\":\"tok\","
			"\"Features\":[\"resume\"],\"Frame\":1,\"Title\":\"Home\"}}"),
		ChannelKey(2, wxT("key-1")),
		ChannelKey(3, wxT("key-2")),
	};

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	ASSERT_TRUE(communicator.Login(wxT("user"), wxT("secret"), ibProtocol::ibLogin::ibMode::Runtime, result, refusal, error)) << error;
	ASSERT_TRUE(communicator.ListenMcp([](const ibProtocolNode&) {}, refusal, error)) << error;

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibMcp::Command, message);

	// The socket goes under the call: false, and the call was offered once.
	EXPECT_FALSE(communicator.Mcp(params, result, refusal, error));
	ASSERT_EQ(firstRaw->sent.size(), 2u);
	EXPECT_EQ(McpCommandOf(firstRaw->sent[0]), listen);
	EXPECT_EQ(McpCommandOf(firstRaw->sent[1]), message);
	EXPECT_EQ(raw->sent.size(), 2u);

	// The next call finds another connection, bound by a new key, and goes on that.
	ASSERT_TRUE(communicator.Mcp(params, result, refusal, error)) << error;
	EXPECT_EQ(result.GetString(ibProtocol::ibMcp::Message), wxT("answered"));
	ASSERT_EQ(secondRaw->sent.size(), 2u);
	EXPECT_EQ(McpCommandOf(secondRaw->sent[0]), listen);
	EXPECT_EQ(NodeOf(secondRaw->sent[0]).FindChild(ibProtocol::ibRpc::Params).GetString(ibProtocol::ibMcp::Channel), wxT("key-2"));
	EXPECT_EQ(McpCommandOf(secondRaw->sent[1]), message);
	EXPECT_EQ(raw->sent.size(), 3u);
}

namespace {

// A state event as the client's host makes one — {Control, Event, Args}.
ibProtocolNode StateEvent(int control, ibProtocol::ibEvent::ibKind event, const char* name = nullptr, int value = 0, int column = 0)
{
	ibProtocolNode args;
	if (name != nullptr)
		args.SetValue(name, value);
	if (column != 0)
		args.SetValue(ibProtocol::ibEvent::Column, column);
	ibProtocolNode node;
	node.SetValue(ibProtocol::ibEvent::Control, control)
		.SetValue(ibProtocol::ibEvent::Event, static_cast<int>(event))
		.SetValue(ibProtocol::ibEvent::Args, args);
	return node;
}

std::vector<ibProtocolNode> PendingOf(const ibMemoryBuffer& packet)
{
	return NodeOf(packet).FindChild(ibProtocol::ibRpc::Params).GetList(ibProtocol::ibLogin::Pending);
}

ibMemoryBuffer LoginAnswer(int protocol)
{
	return Packet(ibString::Format(
		wxT("{\"id\":1,\"result\":{\"Client\":\"client-1\",\"Protocol\":%d,\"Frame\":1,\"Title\":\"Home\"}}"),
		protocol));
}

ibMemoryBuffer FrameAnswer(long long id, int number)
{
	ibProtocolNode frame;
	frame.SetValue(ibProtocol::ibFrame::Frame, number).SetValue(ibProtocol::ibFrame::Title, ibString(wxT("Home")));
	return Answer(id, frame);
}

} // namespace

// A walk down a list is one record: the latest of each state event per control, in the order they were last done, goes
// with the next call — and only with it.
TEST(SubscribedEventsClient, HeldEventsGoWithTheNextCall)
{
	auto script = std::make_unique<ScriptedConnection>();
	ScriptedConnection* const raw = script.get();
	raw->fail = { false, false, false };
	raw->answers = { LoginAnswer(3), FrameAnswer(2, 2), FrameAnswer(3, 3) };

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	ASSERT_TRUE(communicator.Login(wxT("user"), wxT("secret"), ibProtocol::ibLogin::ibMode::Runtime, result, refusal, error)) << error;

	for (int row = 1; row <= 100; ++row)
		EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Row, ibProtocol::ibEvent::Row, row)));
	for (int focus = 0; focus < 5; ++focus)
		EXPECT_TRUE(communicator.Hold(StateEvent(3, ibProtocol::ibEvent::ibKind::Focus)));
	EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Resize, "Width", 80, 21)));
	EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Resize, "Width", 90, 22)));
	EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Resize, "Width", 120, 21)));
	EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Row, ibProtocol::ibEvent::Row, 500)));

	ibProtocolNode params;
	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	ASSERT_EQ(raw->sent.size(), 2u);
	const std::vector<ibProtocolNode> pending = PendingOf(raw->sent[1]);
	ASSERT_EQ(pending.size(), 4u);
	EXPECT_EQ(pending[0].GetInt(ibProtocol::ibEvent::Event), static_cast<int>(ibProtocol::ibEvent::ibKind::Focus));
	EXPECT_EQ(pending[0].GetInt(ibProtocol::ibEvent::Control), 3);
	// Each column's width is its own: 22's stays, 21's is the later one.
	EXPECT_EQ(pending[1].FindChild(ibProtocol::ibEvent::Args).GetInt(ibProtocol::ibEvent::Column), 22);
	EXPECT_EQ(pending[2].FindChild(ibProtocol::ibEvent::Args).GetInt(ibProtocol::ibEvent::Column), 21);
	EXPECT_EQ(pending[2].FindChild(ibProtocol::ibEvent::Args).GetInt("Width"), 120);
	EXPECT_EQ(pending[3].GetInt(ibProtocol::ibEvent::Event), static_cast<int>(ibProtocol::ibEvent::ibKind::Row));
	EXPECT_EQ(pending[3].FindChild(ibProtocol::ibEvent::Args).GetInt(ibProtocol::ibEvent::Row), 500);

	// Taken once.
	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_TRUE(PendingOf(raw->sent.back()).empty());
}

// A server of protocol 2 would never hear a held event: the client sends it at once.
TEST(SubscribedEventsClient, NothingHeldBelowProtocol3)
{
	auto script = std::make_unique<ScriptedConnection>();
	ScriptedConnection* const raw = script.get();
	raw->fail = { false, false };
	raw->answers = { LoginAnswer(2), FrameAnswer(2, 2) };

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	EXPECT_FALSE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Row, ibProtocol::ibEvent::Row, 1)));
	ASSERT_TRUE(communicator.Login(wxT("user"), wxT("secret"), ibProtocol::ibLogin::ibMode::Runtime, result, refusal, error)) << error;
	EXPECT_FALSE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Row, ibProtocol::ibEvent::Row, 1)));

	ibProtocolNode params;
	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_TRUE(PendingOf(raw->sent.back()).empty());
}

