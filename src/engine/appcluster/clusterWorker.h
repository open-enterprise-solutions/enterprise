#ifndef __CLUSTER_WORKER_H__
#define __CLUSTER_WORKER_H__

// ONE SERVER THE CLUSTER HOLDS — the application server of one base (appserver --worker=<base>), a process of its own:
// started with its pipes in hand, read through them (ibWorkerPipe: the port it serves on, and what it says), started
// again when it falls — sooner after a long life, later after a short one — and stopped when it is told to, killed when
// it does not go.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include <wx/process.h>
#include <wx/timer.h>

#include "core/fstring.h"
#include "core/server/clusterControl.h"   // ibClusterState

class ibAppCluster;

class ibClusterWorker {
public:

	ibClusterWorker(ibAppCluster& cluster, const ibString& base);
	~ibClusterWorker();

	void Start();
	void Stop();
	void Restart();

	// What it said since the last read: on stdout the port it serves on, on stderr its journal's lines — the last error
	// kept, the cause a fall is said with.
	void ReadPipes();

	const ibString& GetBase() const { return m_base; }
	ibClusterState  GetState() const { return m_state; }
	long            GetPid() const { return m_pid; }
	// 0 — not serving. Asked by the port's threads as a client comes in.
	std::uint16_t   GetPort() const { return m_port.load(); }
	unsigned        GetFalls() const { return m_falls; }
	const ibString& GetCause() const { return m_cause; }

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
	ibString                              m_base;
	wxProcess*                            m_process = nullptr;   // the running one: freed once it has ended
	long                                  m_pid = 0;
	std::atomic<std::uint16_t>            m_port{ 0 };
	ibClusterState                        m_state = ibClusterState::Stopped;
	unsigned                              m_falls = 0;
	std::chrono::steady_clock::time_point m_started;
	std::chrono::seconds                  m_delay{ 1 };   // before a fallen server is started again
	std::string                           m_stdout;       // a line not yet whole
	std::string                           m_stderr;
	ibString                              m_lastError;    // the last error it said in this life
	ibString                              m_cause;        // why it fell (ibClusterServer::m_cause)
	wxTimer                               m_timer;
};

#endif
