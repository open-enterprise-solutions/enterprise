/////////////////////////////////////////////////////////////////////////////
// prompts — MCP `prompts/list` + `prompts/get` surface area.
//
// Per MCP spec 2025-06-18 (https://spec.modelcontextprotocol.io/specification/
// 2025-06-18/server/prompts):
//   * Prompts are USER-controlled (vs tools = model-controlled, resources =
//     app/user-controlled). The host typically surfaces them as slash
//     commands the user explicitly invokes (e.g. `/oes:new-catalog` in
//     Claude Code, `/mcp.oes.new-catalog` in Cursor).
//   * `prompts/list` returns an array of `PromptDescriptor` entries —
//     name, description, arguments[].
//   * `prompts/get` takes `{name, arguments}` and returns
//     `{description, messages:[{role, content:{type:"text", text}}]}`.
//     The host then feeds those messages to the LLM as the next-turn
//     conversation prefix.
//   * Server advertises the capability in `initialize` via
//     `{"prompts":{"listChanged":true}}`. listChanged is wired as a hint —
//     v1's table is static for the process lifetime but the bit lets us
//     evolve without a protocol bump.
//
// MCP: this header has no wxWidgets includes. Two-DLL boundary stays
// intact — std::string at the public surface, no backend dependency.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_MCP_PROMPTS_H_
#define _IB_MCP_PROMPTS_H_

#include "3rdparty/nlohmann/json.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mcpServer {

// MCP: a single argument the prompt template substitutes. The host UI
// surfaces these as form fields when the user invokes the prompt.
struct PromptArgument {
	std::string name;          // identifier (e.g. "synonym")
	std::string description;   // tooltip / placeholder text
	bool        required = false;
};

// MCP: prompt descriptor returned by `prompts/list`. The `render` lambda
// receives the user-supplied argument map and produces the messages array
// for `prompts/get`. Missing required args throw a JSON error envelope so
// the dispatcher re-emits it as a JSON-RPC error.
struct PromptDescriptor {
	std::string                 name;
	std::string                 description;
	std::vector<PromptArgument> arguments;

	// Render the prompt. `args` is the user-supplied argument map (string
	// values only — per spec, prompt arguments are scalar strings). Returns
	// the `prompts/get` result shape:
	//   { "description": "...",
	//     "messages": [ { "role": "user|assistant",
	//                     "content": { "type": "text", "text": "..." } } ] }
	using Renderer = std::function<nlohmann::json(
		const std::map<std::string, std::string>& args)>;
	Renderer render;
};

// Registry — built lazily on first use, then static. Keeps the same
// shape as ResourceRegistry / tool registry for consistency.
class PromptRegistry {
public:
	// Register a descriptor. Idempotent on name — the last caller wins
	// so static-init order doesn't matter.
	void Register(PromptDescriptor desc);

	// `prompts/list` payload — all descriptors, sans the render fn.
	std::vector<PromptDescriptor> List() const;

	// `prompts/get` dispatch. Throws nlohmann::json error envelope for:
	//   * unknown prompt name (code -32602)
	//   * missing required argument (code -32602)
	// Render-time errors are routed by the prompt's own renderer; the
	// registry just shovels them through.
	nlohmann::json Get(const std::string& name,
	                   const std::map<std::string, std::string>& args) const;

	// Used internally by Get(); exposed so tests can introspect.
	const PromptDescriptor* Find(const std::string& name) const;

private:
	std::vector<PromptDescriptor> m_entries;
};

// Accessor — initialises the 7 OES prompts on first call.
PromptRegistry& AllPrompts();

} // namespace mcpServer

#endif // _IB_MCP_PROMPTS_H_
