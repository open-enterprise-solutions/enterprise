/////////////////////////////////////////////////////////////////////////////
// jsonrpc — minimal JSON-RPC 2.0 framing over stdio for the MCP server.
//
// Per the MCP spec the transport is line-delimited JSON: one request per
// stdin line, one response per stdout line. We deliberately avoid the
// Content-Length framing used by LSP because Claude Code, Cursor, and
// JetBrains all default to the simpler line transport for stdio servers.
//
// Error codes match the JSON-RPC 2.0 reserved range:
//   -32700 parse error      -32600 invalid request    -32601 method not found
//   -32602 invalid params   -32603 internal error
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_MCP_JSONRPC_H_
#define _IB_MCP_JSONRPC_H_

#include "3rdparty/nlohmann/json.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace mcpServer {

// JSON-RPC 2.0 error codes — see the spec.
namespace errc {
constexpr int kParseError      = -32700;
constexpr int kInvalidRequest  = -32600;
constexpr int kMethodNotFound  = -32601;
constexpr int kInvalidParams   = -32602;
constexpr int kInternalError   = -32603;
} // namespace errc

// Handler signature: takes the `params` value (may be null) and returns
// the `result` object that goes on the wire. Throw nlohmann::json on
// protocol failure with shape {"code":..., "message":...} so the
// dispatcher can re-emit it as an error envelope.
using MethodHandler = std::function<nlohmann::json(const nlohmann::json& params)>;

class Dispatcher {
public:
	// Register a method. Replaces any prior handler with the same name.
	void Register(const std::string& method, MethodHandler handler);

	// Process one inbound line. Returns the JSON-encoded response, or an
	// empty string when the request is a notification (no `id` field) —
	// per spec, notifications never receive a response.
	std::string Handle(const std::string& line);

private:
	std::unordered_map<std::string, MethodHandler> m_handlers;
};

// MCP: helper that builds a JSON-RPC 2.0 error envelope without throwing.
nlohmann::json MakeError(const nlohmann::json& id, int code,
                          const std::string& message,
                          const nlohmann::json& data = nullptr);

// MCP: stdio loop driver. Reads one JSON-RPC request per line from stdin,
// dispatches via `disp`, writes the response line to stdout. Returns when
// stdin closes or the supplied stopFn returns true between iterations
// (used by SIGINT handler).
//
// stopFn is polled after each request so a quiescent server still exits
// promptly on Ctrl+C.
void RunStdioLoop(Dispatcher& disp, std::function<bool()> stopFn);

} // namespace mcpServer

#endif // _IB_MCP_JSONRPC_H_
