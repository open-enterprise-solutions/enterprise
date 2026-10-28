#include "clusterTray.h"

#include <algorithm>

#include <wx/artprov.h>
#include <wx/menu.h>
#include <wx/utils.h>   // wxLaunchDefaultApplication

#include "appClusterTray.h"

namespace {

// The menu's items: the tray's own, then three for each server, in the list's order.
enum {
	kOpenFolder = wxID_HIGHEST + 1,
	kStartCluster,
	kStopCluster,
	kExit,
	kFirstWorker
};
enum { kStart, kStop, kRestart, kVerbs };

// The cause shown is cut to this.
constexpr size_t kCauseShown = 100;

ibString StateText(const ibClusterServer& server)
{
	switch (server.m_state) {
	case ibClusterState::Starting:   return ibString::Format(_("starting, process %ld"), server.m_pid);
	case ibClusterState::Serving:    return ibString::Format(_("serving, process %ld"), server.m_pid);
	case ibClusterState::Stopping:   return _("stopping");
	case ibClusterState::Restarting: return _("restarting");
	case ibClusterState::Fallen:     return ibString::Format(_("fallen (%u), started again soon"), server.m_falls);
	case ibClusterState::Stopped:    return _("stopped");
	}
	return ibString();
}

} // namespace

ibClusterTray::ibClusterTray(ibAppClusterTray& app) :
	m_app(app)
{
	Bind(wxEVT_MENU, &ibClusterTray::OnMenu, this);
}

void ibClusterTray::Draw()
{
	if (!m_app.IsReached()) {
		SetIcon(wxArtProvider::GetBitmapBundle(wxART_CROSS_MARK, wxART_OTHER, wxSize(16, 16)),
			_("Application cluster: not running"));
		return;
	}

	size_t serving = 0, down = 0;
	for (const ibClusterServer& server : m_app.GetServers()) {
		if (server.m_state == ibClusterState::Serving)
			++serving;
		else if (server.m_state == ibClusterState::Fallen || server.m_state == ibClusterState::Stopped)
			++down;
	}
	const size_t all = m_app.GetServers().size();
	const wxArtID art = down != 0 ? wxART_ERROR : serving == all ? wxART_INFORMATION : wxART_WARNING;
	const ibString tip = ibString::Format(_("Application cluster, port %u: %u of %u server(s) serving"),
		static_cast<unsigned>(m_app.GetPort()), static_cast<unsigned>(serving), static_cast<unsigned>(all));
	SetIcon(wxArtProvider::GetBitmapBundle(art, wxART_OTHER, wxSize(16, 16)), tip);
}

wxMenu* ibClusterTray::CreatePopupMenu()
{
	wxMenu* const menu = new wxMenu();
	m_shown.clear();

	if (!m_app.IsReached()) {
		menu->Append(wxID_ANY, _("Application cluster: not running"))->Enable(false);
		if (!m_app.GetWhyNotReached().IsEmpty())
			menu->Append(wxID_ANY, m_app.GetWhyNotReached())->Enable(false);
		menu->AppendSeparator();
		menu->Append(kStartCluster, _("Start the cluster"));
	}
	else {
		menu->Append(wxID_ANY, ibString::Format(_("Application cluster, port %u"), static_cast<unsigned>(m_app.GetPort())))
			->Enable(false);
		menu->AppendSeparator();

		for (const ibClusterServer& server : m_app.GetServers()) {
			const bool running = server.m_state == ibClusterState::Starting || server.m_state == ibClusterState::Serving;
			const int first = kFirstWorker + static_cast<int>(m_shown.size()) * kVerbs;

			// A FALLEN SERVER is still the cluster's, waiting to be started again: start is "now", and stop is "not again" —
			// the same two verbs, said as what they do to it.
			const bool fallen = server.m_state == ibClusterState::Fallen;
			wxMenu* const verbs = new wxMenu();
			verbs->Append(first + kStart, fallen ? _("Start now") : _("Start"))
				->Enable(server.m_state == ibClusterState::Stopped || fallen);
			verbs->Append(first + kStop, fallen ? _("Do not start again") : _("Stop"))->Enable(running || fallen);
			verbs->Append(first + kRestart, _("Restart"))->Enable(running);
			if (!server.m_cause.IsEmpty()) {
				verbs->AppendSeparator();
				verbs->Append(wxID_ANY, server.m_cause.Len() > kCauseShown
					? server.m_cause.Left(kCauseShown) + wxT("...") : server.m_cause)->Enable(false);
			}
			// A base of several servers names each by its route.
			const bool several = std::count_if(m_app.GetServers().begin(), m_app.GetServers().end(),
				[&server](const ibClusterServer& other) { return other.m_base == server.m_base; }) > 1;
			menu->AppendSubMenu(verbs, several
				? ibString::Format(wxT("%s #%u - %s"), server.m_base, server.m_route, StateText(server))
				: ibString::Format(wxT("%s - %s"), server.m_base, StateText(server)));
			m_shown.push_back(server);
		}

		menu->AppendSeparator();
		menu->Append(kStopCluster, _("Stop the cluster"));
	}

	menu->AppendSeparator();
	menu->Append(kOpenFolder, _("Open the server folder"));
	menu->Append(kExit, _("Exit"));
	return menu;
}

void ibClusterTray::OnMenu(wxCommandEvent& event)
{
	const int id = event.GetId();
	switch (id) {
	case kOpenFolder:
		wxLaunchDefaultApplication(m_app.GetConfig().GetFolder());
		return;
	case kStartCluster:
		m_app.StartCluster();
		return;
	case kStopCluster:
		m_app.Send(ibClusterControl::StopCluster);
		return;
	case kExit:
		m_app.ExitMainLoop();
		return;
	}

	if (id < kFirstWorker)
		return;
	const size_t index = static_cast<size_t>((id - kFirstWorker) / kVerbs);
	if (index >= m_shown.size())
		return;
	const ibClusterServer& server = m_shown[index];
	switch ((id - kFirstWorker) % kVerbs) {
	case kStart:   m_app.Send(ibClusterControl::Start, server);   break;
	case kStop:    m_app.Send(ibClusterControl::Stop, server);    break;
	case kRestart: m_app.Send(ibClusterControl::Restart, server); break;
	}
}
