#ifndef __CORE_SERVER_CLUSTER_CONTROL_H__
#define __CORE_SERVER_CLUSTER_CONTROL_H__

// THE CLUSTER'S CONTROL — what the application cluster (appcluster) and its tray (appcluster-tray) say to each other:
// one WebSocket at /cluster on the cluster's port, from this machine alone (ibIsLoopback), opened with the token the
// cluster writes into the server folder at every start (cluster.token — whoever may read the folder may steer its
// servers). The cluster sends the list of its servers as the socket opens and again whenever one of them changes; the
// tray sends a verb, for one base or for the whole cluster. Both are JSON (ibJsonProvider), written and read here alone:
//
//   cluster → tray   { "Port": 7373, "Servers": [ { "Base": "trade", "Route": 1, "State": "serving", "Pid": 4120,
//                      "Falls": 0, "Cause": "…" }, … ] }
//   tray → cluster   { "Verb": "restart", "Base": "trade", "Route": 2 }      { "Verb": "stop-cluster" }
//
// A base runs as many servers as server.conf's Processes says, numbered from 1 — the route (ibSessionToken::WithRoute).
// A verb is for one server, named by both.
//
// The tray starts the cluster too, when the socket finds none: the service of that name, if one is installed — the
// program beside it otherwise. Stopped, it is the same either way: the verb.

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "core/fstring.h"
#include "core/serialize/jsonProvider.h"

// ONE SERVER'S STATE — as the cluster holds it (ibClusterWorker) and the tray shows it.
enum class ibClusterState {
	Starting,     // started, its port not said yet
	Serving,      // its port said: the clients of its base are carried to it
	Stopping,     // told to stop
	Restarting,   // told to stop, to be started again once it has gone
	Fallen,       // gone untold — to be started again
	Stopped
};

// ONE SERVER, as the list says it.
struct ibClusterServer {
	ibString       m_base;
	unsigned       m_route = 1;   // which of the base's servers
	ibClusterState m_state = ibClusterState::Stopped;
	long           m_pid = 0;
	unsigned       m_falls = 0;
	ibString       m_cause;   // why it fell: the last error it said before it did — empty again once it is started
};

namespace ibClusterControl {

	inline constexpr const char* Path      = "/cluster";
	inline constexpr const char* TokenFile = "cluster.token";
	inline constexpr const char* Token     = "token";   // the parameter of the path's query

	// The cluster as a service of the system (Windows' service manager): its name, and what the list of services shows.
	inline constexpr const wchar_t* ServiceName  = L"appcluster";
	inline constexpr const wchar_t* ServiceTitle = L"OES Application Cluster";

	// The list of servers.
	inline constexpr const char* Port      = "Port";
	inline constexpr const char* Servers   = "Servers";
	inline constexpr const char* Base      = "Base";
	inline constexpr const char* Route     = "Route";
	inline constexpr const char* State     = "State";
	inline constexpr const char* Pid       = "Pid";
	inline constexpr const char* Falls     = "Falls";
	inline constexpr const char* Cause     = "Cause";

	// A verb: for the server of the Base named, or for the cluster.
	inline constexpr const char* Verb        = "Verb";
	inline constexpr const char* Start       = "start";
	inline constexpr const char* Stop        = "stop";
	inline constexpr const char* Restart     = "restart";
	inline constexpr const char* StopCluster = "stop-cluster";

	// A state as its word — and back: a word not one of them is Stopped.
	inline const char* Word(ibClusterState state)
	{
		switch (state) {
		case ibClusterState::Starting:   return "starting";
		case ibClusterState::Serving:    return "serving";
		case ibClusterState::Stopping:   return "stopping";
		case ibClusterState::Restarting: return "restarting";
		case ibClusterState::Fallen:     return "fallen";
		case ibClusterState::Stopped:    return "stopped";
		}
		return "stopped";
	}

	inline ibClusterState StateOf(const ibString& word)
	{
		for (const ibClusterState state : { ibClusterState::Starting, ibClusterState::Serving, ibClusterState::Stopping,
				ibClusterState::Restarting, ibClusterState::Fallen }) {
			if (word == Word(state))
				return state;
		}
		return ibClusterState::Stopped;
	}

	// A message as it goes: one line.
	inline ibString Text(const ibDataNode& message)
	{
		ibJsonProvider json;
		json.SetCompact(true);
		ibString text;
		json.Write(message, text);
		return text;
	}

	// THE LIST — written by the cluster, read by its tray. False: not the list.
	inline ibString WriteServers(std::uint16_t port, const std::vector<ibClusterServer>& servers)
	{
		std::vector<ibDataValue> items;
		items.reserve(servers.size());
		for (const ibClusterServer& server : servers) {
			const std::shared_ptr<ibDataNode> item = std::make_shared<ibDataNode>();
			item->SetValue(Base, server.m_base);
			item->SetValue(Route, static_cast<std::int32_t>(server.m_route));
			item->SetValue(State, ibString(Word(server.m_state)));
			item->SetValue(Pid, static_cast<std::int32_t>(server.m_pid));
			item->SetValue(Falls, static_cast<std::int32_t>(server.m_falls));
			item->SetValue(Cause, server.m_cause);
			items.push_back(ibDataValue::Child(item));
		}
		ibDataNode list;
		list.SetValue(Port, static_cast<std::int32_t>(port));
		list.SetField(Servers, ibDataValue::Array(std::move(items)));
		return Text(list);
	}

	inline bool ReadServers(const ibString& text, std::uint16_t& port, std::vector<ibClusterServer>& servers)
	{
		ibDataNode list;
		if (!ibJsonProvider().Read(text, list))
			return false;
		const ibDataValue* const items = list.FindField(Servers);
		if (items == nullptr || items->Kind() != ibDataKind::Array)
			return false;
		port = static_cast<std::uint16_t>(list.GetValue<std::int32_t>(Port));
		servers.clear();
		for (const ibDataValue& item : items->AsArray()) {
			if (item.Kind() != ibDataKind::Child || item.AsChild() == nullptr)
				return false;
			const ibDataNode& read = *item.AsChild();
			ibClusterServer server;
			server.m_base      = read.GetValue<ibString>(Base);
			server.m_route     = static_cast<unsigned>(read.GetValue<std::int32_t>(Route));
			server.m_state     = StateOf(read.GetValue<ibString>(State));
			server.m_pid       = read.GetValue<std::int32_t>(Pid);
			server.m_falls     = static_cast<unsigned>(read.GetValue<std::int32_t>(Falls));
			server.m_cause     = read.GetValue<ibString>(Cause);
			servers.push_back(std::move(server));
		}
		return true;
	}

	// A VERB — written by the tray, read by the cluster: for the cluster…
	inline ibString WriteVerb(const char* verb)
	{
		ibDataNode message;
		message.SetValue(Verb, ibString(verb));
		return Text(message);
	}

	// …or for one server of it, by its base and its route.
	inline ibString WriteVerb(const char* verb, const ibClusterServer& server)
	{
		ibDataNode message;
		message.SetValue(Verb, ibString(verb));
		message.SetValue(Base, server.m_base);
		message.SetValue(Route, static_cast<std::int32_t>(server.m_route));
		return Text(message);
	}

	// `server` — the base and the route named; the base empty — the verb is the cluster's. False: not a verb.
	inline bool ReadVerb(const ibString& text, ibString& verb, ibClusterServer& server)
	{
		ibDataNode message;
		if (!ibJsonProvider().Read(text, message))
			return false;
		verb = message.GetValue<ibString>(Verb);
		server.m_base  = message.GetValue<ibString>(Base);
		server.m_route = static_cast<unsigned>(message.GetValue<std::int32_t>(Route));
		return !verb.IsEmpty();
	}
}

#endif
