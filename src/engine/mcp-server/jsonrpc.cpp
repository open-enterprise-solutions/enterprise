/////////////////////////////////////////////////////////////////////////////
// jsonrpc — see header. The dispatcher is single-threaded; stdio loop too.
// MCP clients (Claude Code, Cursor) send one request at a time per server
// process, and metaBridge requires single-thread access anyway — there is
// no benefit to a worker pool here.
/////////////////////////////////////////////////////////////////////////////

#include "jsonrpc.h"

#include <atomic>
#include <cstdio>
#include <iostream>
#include <string>

namespace mcpServer {

void Dispatcher::Register(const std::string& method, MethodHandler handler)
{
	m_handlers[method] = std::move(handler);
}

nlohmann::json MakeError(const nlohmann::json& id, int code,
                          const std::string& message,
                          const nlohmann::json& data)
{
	nlohmann::json env;
	env["jsonrpc"] = "2.0";
	env["id"]      = id.is_null() ? nlohmann::json(nullptr) : id;
	env["error"]   = nlohmann::json::object();
	env["error"]["code"]    = code;
	env["error"]["message"] = message;
	if (!data.is_null()) env["error"]["data"] = data;
	return env;
}

std::string Dispatcher::Handle(const std::string& line)
{
	nlohmann::json req = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
	if (req.is_discarded() || !req.is_object()) {
		return MakeError(nullptr, errc::kParseError, "Parse error").dump();
	}

	// MCP: id may be missing (notification), null, string, or number.
	const nlohmann::json id = req.contains("id") ? req["id"] : nlohmann::json(nullptr);
	const bool isNotification = !req.contains("id");

	if (!req.contains("jsonrpc") || req["jsonrpc"] != "2.0") {
		if (isNotification) return std::string();
		return MakeError(id, errc::kInvalidRequest,
		                  "Invalid Request — jsonrpc field must be \"2.0\"").dump();
	}
	if (!req.contains("method") || !req["method"].is_string()) {
		if (isNotification) return std::string();
		return MakeError(id, errc::kInvalidRequest,
		                  "Invalid Request — method field is required").dump();
	}

	const std::string method = req["method"].get<std::string>();
	const nlohmann::json params = req.contains("params") ? req["params"] : nlohmann::json(nullptr);

	auto it = m_handlers.find(method);
	if (it == m_handlers.end()) {
		if (isNotification) return std::string();
		return MakeError(id, errc::kMethodNotFound,
		                  "Method not found: " + method).dump();
	}

	try {
		nlohmann::json result = it->second(params);
		if (isNotification) return std::string();
		nlohmann::json env;
		env["jsonrpc"] = "2.0";
		env["id"]      = id;
		env["result"]  = std::move(result);
		return env.dump();
	} catch (const nlohmann::json& jsErr) {
		// MCP: structured error thrown by handler — propagate verbatim.
		const int code = jsErr.value("code", errc::kInternalError);
		const std::string msg = jsErr.value("message",
			std::string("Internal error"));
		const nlohmann::json data = jsErr.contains("data") ? jsErr["data"] : nlohmann::json(nullptr);
		if (isNotification) return std::string();
		return MakeError(id, code, msg, data).dump();
	} catch (const std::exception& e) {
		if (isNotification) return std::string();
		return MakeError(id, errc::kInternalError,
		                  std::string("Internal error: ") + e.what()).dump();
	} catch (...) {
		if (isNotification) return std::string();
		return MakeError(id, errc::kInternalError,
		                  "Internal error: unknown exception").dump();
	}
}

void RunStdioLoop(Dispatcher& disp, std::function<bool()> stopFn)
{
	// MCP: stdin is line-buffered already, but ensure stdout is too so
	// every response flushes before we accept the next request.
	std::ios::sync_with_stdio(false);
	std::cin.tie(nullptr);

	std::string line;
	while (std::getline(std::cin, line)) {
		if (stopFn && stopFn()) break;
		if (line.empty()) continue;

		const std::string resp = disp.Handle(line);
		if (!resp.empty()) {
			// MCP: stdout is the wire — every response must terminate
			// with exactly one newline. std::endl flushes too.
			std::cout << resp << '\n' << std::flush;
		}

		if (stopFn && stopFn()) break;
	}
}

} // namespace mcpServer
