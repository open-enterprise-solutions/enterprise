#include "clusterRoute.h"

#include "core/crypto/sessionToken.h"
#include "core/exception.h"
#include "core/serialize/jsonProvider.h"
#include "protocol/protocol.h"

#include "appCluster.h"
#include "clusterWorker.h"

namespace {

// THE ROUTE A CLIENT'S FIRST MESSAGE NAMES — the one its token ends in: a login's Token, a debugger's Channel. 0 — none
// named, or a message the cluster cannot read: what it is, the server says.
unsigned RouteOf(const ibString& message)
{
	ibDataNode read;
	try {
		if (!ibJsonProvider().Read(message, read))
			return 0;
	}
	catch (const ibCoreException&) {
		return 0;
	}
	const ibDataNode* const params = read.FindChild(ibProtocol::ibRpc::Params);
	if (params == nullptr)
		return 0;
	ibString token = params->GetValue<ibString>(ibProtocol::ibLogin::Token);
	if (token.IsEmpty())
		token = params->GetValue<ibString>(ibProtocol::ibDebug::Channel);
	wxString hex;
	unsigned route = 0;
	ibSessionToken::SplitRoute(token, hex, route);
	return route;
}

} // namespace

ibClusterRoute::ibClusterRoute(asio::io_context& io, std::vector<ibClusterWorker*> workers,
	std::shared_ptr<ibWebSocket> socket) :
	m_io(io), m_workers(std::move(workers)), m_socket(std::move(socket))
{
}

void ibClusterRoute::OnMessage(const ibString& text)
{
	if (m_relay == nullptr) {
		m_worker = Choose(RouteOf(text));
		if (m_worker == nullptr) {
			ibAppClusterSay(ibJournalMark::Warning, wxT("base '%s': none of its servers serves - a client told so"),
				m_workers.front()->GetServer().m_base);
			m_socket->Close(ibWebSocketClose::BadGateway, "the server is not reached");
			return;
		}
		const ibString name = m_worker->Name();
		m_worker->AddSocket();
		m_relay = std::make_shared<ibWebSocketRelay>(m_io, "127.0.0.1", m_worker->GetPort(), nullptr,
			m_socket->GetRequest().GetTarget(), m_socket, [name](const ibString& reason) {
				ibAppClusterSay(ibJournalMark::Warning, wxT("base %s: %s"), name, reason);
			});
	}
	m_relay->OnMessage(text);
}

void ibClusterRoute::OnBinary(std::string)
{
	m_socket->Close(ibWebSocketClose::Unsupported, "the protocol speaks text");
}

void ibClusterRoute::OnClose()
{
	if (m_relay == nullptr)
		return;
	m_relay->OnClose();
	m_worker->RemoveSocket();
}

ibClusterWorker* ibClusterRoute::Choose(unsigned route) const
{
	ibClusterWorker* fewest = nullptr;
	for (ibClusterWorker* const worker : m_workers) {
		if (worker->GetPort() == 0)
			continue;
		if (worker->GetServer().m_route == route)
			return worker;
		if (fewest == nullptr || worker->GetSockets() < fewest->GetSockets())
			fewest = worker;
	}
	return fewest;
}
