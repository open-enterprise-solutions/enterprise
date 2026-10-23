#ifndef __CLUSTER_ROUTE_H__
#define __CLUSTER_ROUTE_H__

// ONE CLIENT'S SOCKET, CARRIED TO A SERVER OF ITS BASE — chosen by the first message it sends. A client that comes back
// (a login by its Token, a debugger's Listen by its Channel) names the server its session lives in by the route its
// token ends in (ibSessionToken::WithRoute), and is carried to that one while it serves; a new one — or one whose server
// has gone, and its session with it — to the serving one carrying the fewest sockets. From then on the messages go both
// ways as they are (ibWebSocketRelay). None of the base's servers serving, the client is told so and comes again.

#include <memory>
#include <string>
#include <vector>

#include "core/net/webSocketRelay.h"

class ibClusterWorker;

class ibClusterRoute : public ibWebSocketHandler {
public:

	// `workers` — every server of the base: the cluster's, alive longer than its port.
	ibClusterRoute(asio::io_context& io, std::vector<ibClusterWorker*> workers, std::shared_ptr<ibWebSocket> socket);

	virtual void OnMessage(const ibString& text) override;
	virtual void OnBinary(std::string data) override;
	virtual void OnClose() override;

private:

	// The server of `route`, serving — else the serving one carrying the fewest sockets; null: none serves.
	ibClusterWorker* Choose(unsigned route) const;

	asio::io_context&                 m_io;
	std::vector<ibClusterWorker*>     m_workers;
	std::shared_ptr<ibWebSocket>      m_socket;
	ibClusterWorker*                  m_worker = nullptr;   // the one it is carried to
	std::shared_ptr<ibWebSocketRelay> m_relay;
};

#endif
