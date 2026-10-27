#include "clusterWorker.h"

#include <algorithm>
#include <cstdlib>

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/utils.h>   // wxExecute, wxKill

#include "core/fileSystem.h"
#include "core/server/serverConfig.h"   // ibWorkerPipe

#include "appCluster.h"

namespace {

// A server told to stop and not gone after this is killed.
constexpr int kStopWaitMs = 30000;
// A server that fell after serving this long is started again at once; one that fell sooner waits twice as long as the
// time before, up to the longest wait.
constexpr std::chrono::seconds kLongLife(60);
constexpr std::chrono::seconds kLongestWait(60);

// The application server beside the cluster.
ibString ServerProgram()
{
	wxFileName program(ibProgramFolder(), wxT("appserver"));
#ifdef __WXMSW__
	program.SetExt(wxT("exe"));
#endif
	return program.GetFullPath();
}

} // namespace

ibClusterWorker::ibClusterWorker(ibAppCluster& cluster, const ibClusterServer& server) :
	m_cluster(cluster), m_server(server)
{
	m_timer.Bind(wxEVT_TIMER, &ibClusterWorker::OnTimer, this);
}

ibClusterWorker::~ibClusterWorker()
{
	m_timer.Stop();
	if (m_process != nullptr) {
		// Left running — the cluster going without stopping it (the system's shutdown): its stdin closes with this
		// process, and it stops itself (ibWorkerPipe). The process object frees itself once it has ended.
		m_process->Unbind(wxEVT_END_PROCESS, &ibClusterWorker::OnEnded, this);
		m_process->Detach();
		m_process = nullptr;
	}
}

void ibClusterWorker::Start()
{
	m_timer.Stop();
	if (m_process != nullptr)
		return;

	ibString command = ibString::Format(ibT("\"%s\" --dir=\"%s\" --worker=\"%s\" --route=%u"), ServerProgram(),
		m_cluster.GetConfig().GetFolder(), m_server.m_base, m_server.m_route);
	if (!m_cluster.GetLocale().IsEmpty())
		command += ibString::Format(ibT(" --locale=%s"), m_cluster.GetLocale());
	// Started again after a fall: its first start took the dump of its first error, and one at every start would fill
	// the disk while the fault stays (a DBMS that is down).
	if (m_server.m_falls != 0)
		command += ibT(" --again");

	m_process = new wxProcess();
	m_process->Redirect();
	m_process->Bind(wxEVT_END_PROCESS, &ibClusterWorker::OnEnded, this);
	m_stdout.clear();
	m_stderr.clear();
	m_lastError.clear();
	m_server.m_cause.clear();
	m_started = std::chrono::steady_clock::now();
	// A group of its own: Ctrl+C in the cluster's console is the cluster's, which stops its servers itself — reaching a
	// server past it, it would be one gone untold, a fall.
	m_server.m_pid = wxExecute(command, wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE | wxEXEC_MAKE_GROUP_LEADER, m_process);
	if (m_server.m_pid == 0) {
		// Not started (wx said why) — the process object is still ours, and it falls as one that started would.
		delete m_process;
		m_process = nullptr;
		m_server.m_cause = ibString::Format(ibT("%s did not start"), ServerProgram());
		ibAppClusterSay(ibJournalMark::Error, ibT("base %s: %s"), Name(), m_server.m_cause);
		++m_server.m_falls;
		m_delay = std::min(m_delay * 2, kLongestWait);
		m_timer.StartOnce(static_cast<int>(std::chrono::milliseconds(m_delay).count()));
		SetState(ibClusterState::Fallen);
		return;
	}

	ibAppClusterSay(ibJournalMark::Info, ibT("base %s: its server started, process %ld"), Name(), m_server.m_pid);
	SetState(ibClusterState::Starting);
}

void ibClusterWorker::Stop()
{
	m_timer.Stop();
	if (m_process == nullptr) {
		SetState(ibClusterState::Stopped);
		return;
	}
	TellToStop(ibClusterState::Stopping);
}

void ibClusterWorker::Restart()
{
	if (m_process == nullptr) {
		Start();
		return;
	}
	TellToStop(ibClusterState::Restarting);
}

void ibClusterWorker::TellToStop(ibClusterState state)
{
	// The word, and the pipe closed after it: a server that missed the word stops on the closing.
	if (wxOutputStream* const in = m_process->GetOutputStream()) {
		const std::string word = std::string(ibWorkerPipe::Stop) + "\n";
		in->Write(word.data(), word.size());
	}
	m_process->CloseOutput();
	m_timer.StartOnce(kStopWaitMs);
	SetState(state);
}

void ibClusterWorker::ReadPipes()
{
	if (m_process == nullptr)
		return;

	TakeLines(m_process->GetInputStream(), m_stdout, [this](const std::string& line) {
		const std::string serving = ibWorkerPipe::Serving;
		if (line.compare(0, serving.size(), serving) != 0)
			return;
		const unsigned long port = std::strtoul(line.c_str() + serving.size(), nullptr, 10);
		if (port == 0 || port > 65535)
			return;
		m_port.store(static_cast<std::uint16_t>(port));
		ibAppClusterSay(ibJournalMark::Info, ibT("base %s is served: process %ld, port %lu"), Name(), m_server.m_pid, port);
		SetState(ibClusterState::Serving);
	});
	TakeLines(m_process->GetErrorStream(), m_stderr, [this](const std::string& line) {
		// Its journal's lines, read back by the journal (the rest — a message's own second line — is passed over).
		ibJournalMark mark = ibJournalMark::Info;
		ibString source, message;
		if (ibTechJournal::Read(ibString::FromUTF8(line.data(), line.size()), mark, source, message)
			&& mark == ibJournalMark::Error)
			m_lastError = message;
	});
}

template <typename Line>
void ibClusterWorker::TakeLines(wxInputStream* stream, std::string& pending, Line line)
{
	if (stream == nullptr)
		return;

	char buffer[4096];
	while (stream->CanRead()) {
		stream->Read(buffer, sizeof(buffer));
		const std::size_t read = stream->LastRead();
		if (read == 0)
			break;
		pending.append(buffer, read);
	}

	std::size_t from = 0;
	for (std::size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n', from)) {
		std::string one = pending.substr(from, end - from);
		if (!one.empty() && one.back() == '\r')
			one.pop_back();
		line(one);
		from = end + 1;
	}
	pending.erase(0, from);
}

void ibClusterWorker::OnEnded(wxProcessEvent& event)
{
	ReadPipes();   // what it said last
	m_timer.Stop();

	// Freed once this event is done with it — wx leaves that to whoever handled it.
	wxTheApp->ScheduleForDestruction(m_process);
	m_process = nullptr;
	const long pid = m_server.m_pid;
	m_server.m_pid = 0;
	m_port.store(0);

	switch (m_server.m_state) {
	case ibClusterState::Stopping:
		ibAppClusterSay(ibJournalMark::Info, ibT("base %s: its server stopped, process %ld"), Name(), pid);
		SetState(ibClusterState::Stopped);
		break;
	case ibClusterState::Restarting:
		ibAppClusterSay(ibJournalMark::Info, ibT("base %s: its server stopped, process %ld - started again"), Name(), pid);
		Start();
		break;
	default: {
		// FALLEN — started again: at once after a long life, later and later after short ones (a base that does not open
		// would otherwise be started without a pause).
		++m_server.m_falls;
		const bool longLife = std::chrono::steady_clock::now() - m_started >= kLongLife;
		m_delay = longLife ? std::chrono::seconds(1) : std::min(m_delay * 2, kLongestWait);
		m_server.m_cause = m_lastError;
		// A WARNING, not an error: the cluster did what it is for. The fault is the server's, in the server's journal; an
		// error here would take the cluster's own dump for a fall it handled.
		ibAppClusterSay(ibJournalMark::Warning, ibT("base %s: its server fell, process %ld, code %d: %s - started again in %lld s"),
			Name(), pid, event.GetExitCode(),
			m_server.m_cause.IsEmpty() ? ibString(ibT("no error said")) : m_server.m_cause,
			static_cast<long long>(m_delay.count()));
		m_timer.StartOnce(static_cast<int>(std::chrono::milliseconds(m_delay).count()));
		SetState(ibClusterState::Fallen);
		break;
	}
	}
}

void ibClusterWorker::OnTimer(wxTimerEvent&)
{
	const ibClusterState state = m_server.m_state;
	if (state == ibClusterState::Fallen) {
		Start();
		return;
	}
	if ((state == ibClusterState::Stopping || state == ibClusterState::Restarting) && m_server.m_pid != 0) {
		ibAppClusterSay(ibJournalMark::Warning, ibT("base %s: its server did not stop in %d s - killed, process %ld"),
			Name(), kStopWaitMs / 1000, m_server.m_pid);
		wxKill(m_server.m_pid, wxSIGKILL, nullptr, wxKILL_CHILDREN);
	}
}

void ibClusterWorker::SetState(ibClusterState state)
{
	m_server.m_state = state;
	m_cluster.OnWorkerChanged();
}

ibString ibClusterWorker::Name() const
{
	return ibString::Format(ibT("'%s' #%u"), m_server.m_base, m_server.m_route);
}
