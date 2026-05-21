/////////////////////////////////////////////////////////////////////////////
// oes-mcp — Model Context Protocol server for the OES Designer metadata
// API. Wraps metaBridge over a JSON-RPC 2.0 / stdio transport so MCP
// clients (Claude Code CLI, Cursor, JetBrains AI, ...) can edit OES
// configurations programmatically.
//
// Spec: https://spec.modelcontextprotocol.io/specification/
//
// Setup (Claude Code CLI):
//   claude mcp add oes-local \
//     --transport=stdio \
//     --command="<build>/bin/Debug/oes-mcp" \
//     --args="<path-to-OES-config-dir>"
/////////////////////////////////////////////////////////////////////////////

#include "headless_app.h"
#include "jsonrpc.h"
#include "tools.h"

#include "3rdparty/nlohmann/json.hpp"

#include <wx/init.h>
#include <wx/app.h>
#include <wx/log.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::atomic<bool> g_stop{false};

void StopRequested(int /*sig*/)
{
	// MCP: SIGINT handler. We can't call non-async-safe functions here
	// (no std::cerr, no shutdown). Just flip the flag; the stdio loop
	// polls between requests and unblocks std::getline by closing stdin
	// the next time main loops.
	g_stop.store(true);
}

// MCP: every diagnostic line lands on stderr. Stdout is the JSON-RPC
// channel — any stray byte there breaks the protocol.
void DiagToStderr(const char* line)
{
	std::fprintf(stderr, "%s\n", line);
	std::fflush(stderr);
}

void PrintHelp()
{
	std::fprintf(stderr,
		"oes-mcp — Model Context Protocol server for OES Designer\n"
		"\n"
		"Usage: oes-mcp [<config-path>]\n"
		"\n"
		"If <config-path> is omitted, the OES_CONFIG_PATH environment\n"
		"variable is used. The configuration path is a directory containing\n"
		"sys.fdb (an OES file-mode configuration).\n"
		"\n"
		"Environment:\n"
		"  OES_CONFIG_PATH  fallback for the configuration directory\n"
		"  OES_MCP_USER     IB user for the system session (default: empty)\n"
		"  OES_MCP_PWD      IB password for the system session\n"
		"  OES_MCP_LOCALE   BCP-47 locale (default: empty / appData default)\n"
		"\n"
		"Stdio transport. Wire into Claude Code via:\n"
		"  claude mcp add oes-local --transport=stdio --command=oes-mcp \\\n"
		"      --args=<config-path>\n");
}

} // namespace

int main(int argc, char** argv)
{
	// MCP: route --help / -h before wx init so users without a config
	// can still discover the binary.
	bool noConfig = false;
	std::string configPath;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--help" || a == "-h") {
			PrintHelp();
			return 0;
		}
		// MCP: --no-config keeps the server running without loading a
		// configuration. Used by the smoke test + CI to validate the
		// JSON-RPC handshake / tools/list shape without a sys.fdb.
		// Config-bound tools will return isError:true envelopes.
		if (a == "--no-config") { noConfig = true; continue; }
		// First positional arg = config path.
		if (configPath.empty() && a.size() > 0 && a[0] != '-') configPath = a;
	}

	if (configPath.empty() && !noConfig) {
		if (const char* env = std::getenv("OES_CONFIG_PATH")) {
			configPath = env;
		}
	}
	if (configPath.empty() && !noConfig) {
		std::fprintf(stderr,
			"oes-mcp: configuration path required (argv[1], OES_CONFIG_PATH, "
			"or --no-config for protocol-only mode)\n");
		PrintHelp();
		return 1;
	}

	// MCP: wxWidgets bootstrap. metaBridge guards every entry with
	// wxIsMainThread(), which returns false if wxApp isn't running. The
	// console wxInitializer brings up a minimal wxAppConsole — enough for
	// the main-thread check and the backend's wxString / wxLog usage.
	wxInitializer wxInit(argc, argv);
	if (!wxInit.IsOk()) {
		std::fprintf(stderr, "oes-mcp: wxWidgets failed to initialise\n");
		return 1;
	}
	// MCP: route every wx log line to stderr so it doesn't pollute the
	// JSON-RPC stdout channel. The default target on console wx is
	// already wxLogStderr but we override explicitly so debug builds
	// don't surprise us.
	delete wxLog::SetActiveTarget(new wxLogStderr);

	// MCP: SIGINT / SIGTERM = graceful shutdown.
	std::signal(SIGINT,  StopRequested);
	std::signal(SIGTERM, StopRequested);

	mcpServer::HeadlessConfig cfg;
	cfg.configPath = configPath;
	if (const char* u = std::getenv("OES_MCP_USER"))   cfg.ibUser     = u;
	if (const char* p = std::getenv("OES_MCP_PWD"))    cfg.ibPassword = p;
	if (const char* l = std::getenv("OES_MCP_LOCALE")) cfg.locale     = l;

	if (!noConfig) {
		if (!mcpServer::Init(cfg, &DiagToStderr)) {
			std::fprintf(stderr, "oes-mcp: configuration load failed — exiting\n");
			return 1;
		}
	} else {
		std::fprintf(stderr,
			"oes-mcp: --no-config mode — config-bound tools will report isError\n");
	}

	// =========================================================================
	// JSON-RPC method registration
	// =========================================================================
	mcpServer::Dispatcher disp;

	// MCP: `initialize` — first call, returns capabilities + serverInfo.
	disp.Register("initialize", [](const nlohmann::json& /*params*/) {
		nlohmann::json out;
		out["protocolVersion"] = "2024-11-05";
		out["capabilities"]    = nlohmann::json::object();
		out["capabilities"]["tools"] = nlohmann::json::object();
		out["serverInfo"]      = nlohmann::json::object();
		out["serverInfo"]["name"]    = "oes-mcp";
		out["serverInfo"]["version"] = "1.0";
		return out;
	});

	// MCP: `notifications/initialized` — client signals it's done with
	// the initialize handshake. We don't act on it but acknowledge by
	// not throwing.
	disp.Register("notifications/initialized",
		[](const nlohmann::json& /*params*/) {
			return nlohmann::json(nullptr);
		});

	// MCP: also accept the older spelling `initialized` that some clients
	// (pre-2024-11 drafts) still send.
	disp.Register("initialized", [](const nlohmann::json& /*params*/) {
		return nlohmann::json(nullptr);
	});

	// MCP: `tools/list` — returns the tool table.
	disp.Register("tools/list", [](const nlohmann::json& /*params*/) {
		nlohmann::json arr = nlohmann::json::array();
		for (const auto& t : mcpServer::AllTools()) {
			nlohmann::json item;
			item["name"]        = t.desc.name;
			item["description"] = t.desc.description;
			item["inputSchema"] = t.desc.inputSchema;
			// MCP spec 2025-06-18: behaviour hints. All four always emitted
			// so clients never have to guess defaults.
			nlohmann::json ann;
			ann["readOnlyHint"]    = t.desc.annotations.readOnlyHint;
			ann["destructiveHint"] = t.desc.annotations.destructiveHint;
			ann["idempotentHint"]  = t.desc.annotations.idempotentHint;
			ann["openWorldHint"]   = t.desc.annotations.openWorldHint;
			item["annotations"]    = std::move(ann);
			arr.push_back(item);
		}
		nlohmann::json out;
		out["tools"] = std::move(arr);
		return out;
	});

	// MCP: `tools/call` — dispatch to the named tool. Failures inside the
	// tool come back through DispatchTool as content envelopes with
	// isError:true; only structural failures (missing name) raise here.
	disp.Register("tools/call", [](const nlohmann::json& params) {
		if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
			nlohmann::json err;
			err["code"]    = mcpServer::errc::kInvalidParams;
			err["message"] = "tools/call: 'name' field is required";
			throw err;
		}
		const std::string name = params["name"].get<std::string>();
		const nlohmann::json args = params.contains("arguments")
			? params["arguments"]
			: nlohmann::json::object();
		return mcpServer::DispatchTool(name, args);
	});

	// MCP: optional, but several clients call `ping` to verify the
	// connection is live. Cheap to implement.
	disp.Register("ping", [](const nlohmann::json& /*params*/) {
		return nlohmann::json::object();
	});

	// =========================================================================
	// Stdio loop
	// =========================================================================
	std::fprintf(stderr,
		"oes-mcp: ready on stdio, %zu tools registered\n",
		mcpServer::AllTools().size());
	std::fflush(stderr);

	mcpServer::RunStdioLoop(disp, [] { return g_stop.load(); });

	std::fprintf(stderr, "oes-mcp: shutting down\n");
	std::fflush(stderr);
	mcpServer::Shutdown();
	return 0;
}
