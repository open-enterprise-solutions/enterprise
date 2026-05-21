/////////////////////////////////////////////////////////////////////////////
// tools — MCP `tools/list` + `tools/call` surface area. Each tool wraps an
// existing backend / metaBridge entry point. The registry is statically
// initialised at server boot so `tools/list` can return the full set with
// inputSchema definitions in a single pass.
//
// MCP: every tool function returns a JSON object shaped like the MCP
// `tools/call` result, i.e. {"content":[{"type":"text","text":"..."}]}.
// Errors INSIDE a tool (validation, missing config, etc.) set
// "isError":true on the result instead of throwing JSON-RPC errors —
// that maps cleanly to how Claude Code surfaces tool failures.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_MCP_TOOLS_H_
#define _IB_MCP_TOOLS_H_

#include "3rdparty/nlohmann/json.hpp"

#include <functional>
#include <string>
#include <vector>

namespace mcpServer {

// MCP: tool behaviour hints (spec 2025-06-18). All four must be set so the
// client never has to guess defaults; downstream agents key off these to
// route calls (e.g. allow-list read-only tools in restricted modes).
struct ToolAnnotations {
	bool readOnlyHint    = false;   // does NOT modify environment
	bool destructiveHint = false;   // may perform destructive updates
	bool idempotentHint  = false;   // repeated calls with same args = same effect
	bool openWorldHint   = false;   // interacts with external world
};

// Static descriptor — what `tools/list` ships back to the client.
struct ToolDescriptor {
	std::string     name;
	std::string     description;
	nlohmann::json  inputSchema;   // JSON Schema object
	ToolAnnotations annotations;   // MCP 2025-06-18 hint block
};

// Concrete tool entry: a descriptor + the dispatch fn that takes the
// `arguments` object and produces an MCP content envelope.
using ToolFn = std::function<nlohmann::json(const nlohmann::json& args)>;

struct ToolEntry {
	ToolDescriptor desc;
	ToolFn         fn;
};

// Build the full tool table. Idempotent — second call returns the same
// vector (statics live in the .cpp).
const std::vector<ToolEntry>& AllTools();

// Convenience: dispatch by name. Returns the MCP content envelope (with
// isError:true on failure) or a JSON-RPC-style error envelope when the
// tool name is unknown.
nlohmann::json DispatchTool(const std::string& name, const nlohmann::json& args);

} // namespace mcpServer

#endif // _IB_MCP_TOOLS_H_
