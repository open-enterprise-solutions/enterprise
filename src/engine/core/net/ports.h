#ifndef __CORE_NET_PORTS_H__
#define __CORE_NET_PORTS_H__

// THE PLATFORM'S PORTS — each role a range of its own, and no two ranges meet: whatever runs side by side on one
// machine, nothing takes another's port by default. A role that holds one port holds the first of its range; one that
// looks for a free one looks inside its own alone; one with a port per base gives each base its own from it, kept in
// server.conf. A port a person writes (server.conf, web.conf, the assistant's settings) is taken as written.

#include <cstdint>

struct ibPortRange {
	std::uint16_t m_first;
	std::uint16_t m_count;

	constexpr std::uint16_t Last() const { return static_cast<std::uint16_t>(m_first + m_count - 1); }
};

namespace ibPort {
	// THE COORDINATOR'S — the application cluster's, kept in server.conf (Port): every client comes in by it, the tray
	// too, and the cluster hands each client to the server of its base inside.
	inline constexpr ibPortRange Clients       { 7373, 1 };
	// The designer's assistant access (MCP).
	inline constexpr ibPortRange Assistant     { 7374, 1 };

	// THE SERVERS' — the cluster's, one process a base (appserver --worker), on this machine alone: each the first free
	// one, said to the cluster as it starts serving.
	inline constexpr ibPortRange Workers       { 7500, 100 };
	// …and the assistant access of their bases: one each, kept in server.conf (McpPort).
	inline constexpr ibPortRange BaseAssistant { 7600, 100 };

	// THE DEBUGGER'S — every process that can be debugged (a client, a server's base, the web) listens on the first
	// free one; the designer looks in all of them.
	inline constexpr ibPortRange Debugger      { 7700, 10 };

	// The web server's (web.conf: HttpPort, HttpsPort — HTTPS when it names a certificate).
	inline constexpr ibPortRange Web           { 8080, 1 };
	inline constexpr ibPortRange WebSecure     { 443, 1 };
}

#endif
