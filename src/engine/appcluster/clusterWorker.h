#ifndef __CLUSTER_WORKER_H__
#define __CLUSTER_WORKER_H__

// ONE SERVER THE CLUSTER HOLDS — an application server of one base (appserver --worker=<base> --route=<n>), a process
// of its own: started with its pipes in hand, read through them (ibWorkerPipe: the port it serves on, and what it says),
// started again when it falls — sooner after a long life, later after a short one — and stopped when it is told to,
// killed when it does not go. A base runs as many as server.conf's Processes says, numbered from 1 — the route its
// sessions' tokens end in (ibSessionToken::WithRoute), by which the cluster carries a client back to it.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include <wx/process.h>
#include <wx/timer.h>

#include "core/fstring.h"
#include "core/server/clusterControl.h"   // ibClusterServer

class ibAppCluster;

class ibClusterWorker {
public:

	// The server as the list says it: its base and its route given, the rest its life.
	ibClusterWorker(ibAppCluster& cluster, const ibClusterServer& server);
	~ibClusterWorker();

	void Start();
	void Stop();
	void Restart();

	// What it said since the last read: on stdout the port it serves on, on stderr its journal's lines — the last error
	// kept, the cause a fall is said with.
	void ReadPipes();

	// What the list says of it. Its base and route never change — the port's threads read them as a client comes in;
	// the rest is the main thread's.
	const ibClusterServer& GetServer() const { return m_server; }
	// How the journal names it: 'trade' #2.
	ibString               Name() const;
	// 0 — not serving. Asked by the port's threads as a client comes in.
	std::uint16_t          GetPort() const { return m_port.load(); }

	// THE CLIENTS' SOCKETS CARRIED TO IT — counted by the port's threads as each is carried and let go (ibClusterRoute):
	// a new client goes to the server of its base carrying the fewest.
	void                   AddSocket() { ++m_sockets; }
	void                   RemoveSocket() { --m_sockets; }
	unsigned               GetSockets() const { return m_sockets.load(); }

private:

	// The process gone — said, and what comes next by the state it went in: stopped, started again, fallen.
	void OnEnded(wxProcessEvent& event);
	// Fallen: started again. Stopping or restarting: killed, it did not go in time.
	void OnTimer(wxTimerEvent& event);

	// Told `stop` on its pipe — `state` is what it is while it goes: Stopping, or Restarting — and killed if it does not go.
	void TellToStop(ibClusterState state);

	void SetState(ibClusterState state);

	// What `stream` has added to `pending`, and the lines whole in it each given to `line`; the rest kept.
	template <typename Line>
	static void TakeLines(wxInputStream* stream, std::string& pending, Line line);

	ibAppCluster&                         m_cluster;
	ibClusterServer                       m_server;
	std::atomic<unsigned>                 m_sockets{ 0 };
	wxProcess*                            m_process = nullptr;   // the running one: freed once it has ended
	std::atomic<std::uint16_t>            m_port{ 0 };
	std::chrono::steady_clock::time_point m_started;
	std::chrono::seconds                  m_delay{ 1 };   // before a fallen server is started again
	std::string                           m_stdout;       // a line not yet whole
	std::string                           m_stderr;
	ibString                              m_lastError;    // the last error it said in this life
	wxTimer                               m_timer;
};

#endif
