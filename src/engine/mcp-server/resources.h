/////////////////////////////////////////////////////////////////////////////
// resources — MCP `resources/list`, `resources/templates/list`,
// `resources/read`, `resources/subscribe`, `resources/unsubscribe`,
// and the `notifications/resources/updated` emit path.
//
// Per MCP spec 2025-06-18:
//   * Resources are user/app-controlled context (vs tools which are
//     model-controlled). An MCP host typically lets the user pick which
//     resources to attach to a chat — the server's job is to advertise
//     URIs the host can fetch on demand.
//   * Concrete resources (exact URI, e.g. `oes://config/current`) live in
//     `resources/list`.
//   * Templated resources (URI template per RFC 6570, e.g.
//     `oes://catalog/{name}`) live in `resources/templates/list`.
//   * Reads return `{contents:[{uri, mimeType, text}]}`. Multiple content
//     entries are allowed for composite resources; oes-mcp's resources
//     all emit a single entry.
//   * Subscribe/unsubscribe are optional capabilities. v1 keeps the
//     subscription set IN-MEMORY only — restarts forget. Persistent
//     subscriptions across server restarts is a v2 concern.
//
// The registry is initialised lazily on first AllResources() call so
// templates that capture lambdas don't fight static-init order.
//
// MCP: this header has no wxWidgets includes. The Two-DLL boundary keeps
// the entire MCP server backend-only — we use std::string at the public
// surface and convert to wxString at the metaBridge / activeMetaData edge
// inside .cpp.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_MCP_RESOURCES_H_
#define _IB_MCP_RESOURCES_H_

#include "3rdparty/nlohmann/json.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mcpServer {

// MCP: A single resource entry — either a concrete URI or a URI template.
// `read` populates the `contents[0]` text. Errors are signalled by throwing
// a nlohmann::json error envelope (same shape as Dispatcher handlers) so
// the resources/read JSON-RPC method can re-emit them with proper codes.
struct ResourceDescriptor {
	std::string uri;          // exact URI OR template with {placeholders}
	std::string name;         // human label for resources/list
	std::string description;  // short blurb for the host UI
	std::string mimeType;     // application/json, text/markdown, etc.
	bool        isTemplate = false;

	// Reader function. `params` carries the parsed placeholder values for
	// templated URIs (empty map for concrete resources). Returns the JSON
	// payload for `contents[0].text` — strings are emitted as-is, objects/
	// arrays are stringified by the caller per the mimeType.
	using Reader = std::function<std::string(const std::map<std::string, std::string>& params)>;
	Reader read;
};

// MCP: notify-sink wired by `main.cpp`. tools.cpp invokes
// EmitResourceUpdated() after a successful save_config / meta mutation;
// the sink writes a notifications/resources/updated frame to stdout. Kept
// behind an indirection so tools.cpp doesn't need to know how the stdio
// loop encodes notifications.
using NotifySink = std::function<void(const std::string& uri)>;
void SetNotifySink(NotifySink sink);

// Emit a resources-updated notification for one URI. No-op when no sink
// is installed (e.g. unit tests). Idempotent — caller may invoke for
// every mutation; subscription filtering happens inside.
void EmitResourceUpdated(const std::string& uri);

// Registry — built lazily on first use, then static.
class ResourceRegistry {
public:
	// Register a descriptor. Idempotent on (uri, isTemplate); the last
	// caller wins so init order doesn't matter.
	void Register(ResourceDescriptor desc);

	// `resources/list` payload — concrete entries only (no templates).
	std::vector<ResourceDescriptor> ListConcrete() const;

	// `resources/templates/list` payload — templated entries only.
	std::vector<ResourceDescriptor> ListTemplates() const;

	// `resources/read` dispatch. Tries exact match first, then walks
	// templates and matches placeholder segments. Returns the contents
	// array for the JSON-RPC `result`. Throws nlohmann::json error
	// envelope when no resource matches.
	nlohmann::json Read(const std::string& uri) const;

	// Find the descriptor (concrete or template) whose URI/pattern best
	// matches the given concrete URI. Returns nullptr when no match.
	// Used by subscribe to validate the URI exists before tracking.
	const ResourceDescriptor* FindMatching(const std::string& uri) const;

private:
	std::vector<ResourceDescriptor> m_entries;
};

// Accessor — initialises the registry on first call.
ResourceRegistry& AllResources();

// MCP: subscription tracking. v1 is per-process, in-memory only. The
// notify sink filters EmitResourceUpdated calls so we only push frames
// for URIs the client actually subscribed to. Subscribing to a templated
// URI requires the client to expand it first (e.g.
// `oes://catalog/Контрагенты`, not `oes://catalog/{name}`).
void Subscribe(const std::string& uri);
void Unsubscribe(const std::string& uri);
bool IsSubscribed(const std::string& uri);

// Test hook — clears the subscription set. Used by mcp-smoke to keep
// successive runs idempotent.
void ClearSubscriptions();

} // namespace mcpServer

#endif // _IB_MCP_RESOURCES_H_
