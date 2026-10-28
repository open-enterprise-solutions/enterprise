#include "clusterRoute.h"

#include "core/crypto/sessionToken.h"
#include "core/exception.h"
#include "core/serialize/dataBuilder.h"   // ibBinaryProvider — the first packet read where it lies
#include "protocol/protocol.h"

#include "appCluster.h"
#include "clusterWorker.h"

namespace {

// THE ROUTE A CLIENT'S FIRST MESSAGE NAMES — the one its token ends in: a login's Token, a debugger's Channel. 0 — none
// named, or a message the cluster cannot read: what it is, the server says. The packet is read where it lies.
unsigned RouteOf(const std::string& message)
{
	const ibMemoryBuffer packet(message.data(), message.size());
	ibDataNode read;
	try {
		if (!ibBinaryProvider().Read(packet, read))
			return 0;
	}
	catch (const ibCoreException&) {
		return 0;
	}
	const auto params = read.FindChild(ibProtocol::ibRpc::Params);
	if (!params)
		return 0;
	ibString token = params->GetValue<ibString>(ibProtocol::ibLogin::Token);
	if (token.IsEmpty())
		token = params->GetValue<ibString>(ibProtocol::ibDebug::Channel);
	ibString hex;
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

void ibClusterRoute::OnMessage(const ibString&)
{
	m_socket->Close(ibWebSocketClose::Unsupported, "the protocol speaks packets");
}

void ibClusterRoute::OnBinary(std::string data)
{
	if (m_relay == nullptr) {
		m_worker = Choose(RouteOf(data));
		if (m_worker == nullptr) {
			ibAppClusterSay(ibJournalMark::Warning, ibT("base '%s': none of its servers serves - a client told so"),
				m_workers.front()->GetServer().m_base);
			m_socket->Close(ibWebSocketClose::BadGateway, "the server is not reached");
			return;
		}
		const ibString name = m_worker->Name();
		m_worker->AddSocket();
		m_relay = std::make_shared<ibWebSocketRelay>(m_io, "127.0.0.1", m_worker->GetPort(), nullptr,
			m_socket->GetRequest().GetTarget(), m_socket, [name](const ibString& reason) {
				ibAppClusterSay(ibJournalMark::Warning, ibT("base %s: %s"), name, reason);
			});
	}
	m_relay->OnBinary(std::move(data));
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
