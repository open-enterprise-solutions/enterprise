/////////////////////////////////////////////////////////////////////////////
// HTTP transport helpers for the template wizard.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizardHttp.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

namespace ibTemplateWizardHttp {
namespace {

std::string ShellQuote(const std::string& value)
{
	std::string out = "'";
	for (char c : value) {
		if (c == '\'') out += "'\\''";
		else out.push_back(c);
	}
	out += "'";
	return out;
}

std::string CurlConfigEscape(const std::string& value)
{
	std::string out;
	out.reserve(value.size());
	for (char c : value) {
		if (c == '\\' || c == '"') out.push_back('\\');
		out.push_back(c);
	}
	return out;
}

std::filesystem::path TempPath(const char* prefix, const char* suffix)
{
	const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
	std::ostringstream name;
	name << prefix << "-" << ticks << "-" << tid << suffix;
	return std::filesystem::temp_directory_path() / name.str();
}

void RestrictOwnerReadWrite(const std::filesystem::path& path)
{
	std::error_code ec;
	std::filesystem::permissions(
		path,
		std::filesystem::perms::owner_read |
		std::filesystem::perms::owner_write,
		std::filesystem::perm_options::replace,
		ec);
}

std::string CurlBinary()
{
	return std::filesystem::exists("/usr/bin/curl") ? "/usr/bin/curl" : "curl";
}

void TrimTrailingSpace(std::string& text)
{
	while (!text.empty() &&
	       (text.back() == '\n' || text.back() == '\r' ||
	        text.back() == ' '  || text.back() == '\t')) {
		text.pop_back();
	}
}

}  // namespace

std::string NormalizeEndpoint(std::string raw, const std::string& fallback)
{
	TrimTrailingSpace(raw);
	while (!raw.empty() && raw.back() == '/') raw.pop_back();
	if (raw.empty()) return fallback;
	const std::string suffix = "/api/oes-mcp/invoke";
	if (raw.size() >= suffix.size() &&
	    raw.compare(raw.size() - suffix.size(), suffix.size(), suffix) == 0) {
		return raw;
	}
	return raw + suffix;
}

Response PostJson(const std::string& endpoint,
                  const std::string& token,
                  const std::string& tenant,
                  const nlohmann::json& body,
                  int timeoutSec)
{
	Response out;
	const std::filesystem::path bodyPath =
	    TempPath("oes-template-body", ".json");
	const std::filesystem::path cfgPath =
	    TempPath("oes-template-curl", ".conf");

	try {
		{
			std::ofstream f(bodyPath, std::ios::binary);
			if (!f.is_open()) {
				out.error = "cannot create request body file";
				return out;
			}
			f << body.dump();
			RestrictOwnerReadWrite(bodyPath);
		}
		{
			std::ofstream f(cfgPath, std::ios::binary);
			if (!f.is_open()) {
				out.error = "cannot create curl config file";
				std::error_code ec;
				std::filesystem::remove(bodyPath, ec);
				return out;
			}
			f << "silent\n";
			f << "show-error\n";
			f << "request = \"POST\"\n";
			f << "url = \"" << CurlConfigEscape(endpoint) << "\"\n";
			f << "max-time = " << timeoutSec << "\n";
			f << "header = \"Authorization: Bearer "
			  << CurlConfigEscape(token) << "\"\n";
			if (!tenant.empty()) {
				f << "header = \"X-Tenant-Id: "
				  << CurlConfigEscape(tenant) << "\"\n";
			}
			f << "header = \"Content-Type: application/json\"\n";
			f << "header = \"Accept: application/json\"\n";
			f << "data-binary = \"@" << CurlConfigEscape(bodyPath.string())
			  << "\"\n";
			f << "write-out = \"\\n%{http_code}\"\n";
			RestrictOwnerReadWrite(cfgPath);
		}

		const std::string cmd = ShellQuote(CurlBinary()) + " --config " +
		                        ShellQuote(cfgPath.string()) + " 2>&1";
		FILE* pipe = popen(cmd.c_str(), "r");
		if (pipe == nullptr) {
			out.error = "cannot start curl";
		} else {
			char buf[4096];
			while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
				out.body += buf;
			}
			(void)pclose(pipe);
		}
	} catch (const std::exception& e) {
		out.error = e.what();
	} catch (...) {
		out.error = "curl transport exception";
	}

	std::error_code ec;
	std::filesystem::remove(bodyPath, ec);
	std::filesystem::remove(cfgPath, ec);

	std::string text = out.body;
	TrimTrailingSpace(text);
	if (text.size() >= 3 &&
	    std::isdigit(static_cast<unsigned char>(text[text.size() - 1])) &&
	    std::isdigit(static_cast<unsigned char>(text[text.size() - 2])) &&
	    std::isdigit(static_cast<unsigned char>(text[text.size() - 3]))) {
		out.status = std::atoi(text.substr(text.size() - 3).c_str());
		text.erase(text.size() - 3);
		TrimTrailingSpace(text);
		out.body = text;
	}
	if (out.status == 0 && out.error.empty()) {
		out.error = out.body.empty() ? "curl returned no HTTP status" : out.body;
	}
	if (out.status >= 300 && out.status < 400) {
		out.error = "server returned redirect; refusing to replay bearer";
	}
	if (out.status >= 400 && out.error.empty()) {
		out.error = "HTTP " + std::to_string(out.status);
	}
	return out;
}

bool GetBytes(const std::string& url, std::string& body, int timeoutSec)
{
	if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
		return false;
	}
	const std::filesystem::path outPath =
	    TempPath("oes-template-thumb", ".bin");
	const std::filesystem::path cfgPath =
	    TempPath("oes-template-thumb-curl", ".conf");
	bool ok = false;

	try {
		{
			std::ofstream f(cfgPath, std::ios::binary);
			if (!f.is_open()) return false;
			f << "silent\n";
			f << "show-error\n";
			f << "location\n";
			f << "max-time = " << timeoutSec << "\n";
			f << "url = \"" << CurlConfigEscape(url) << "\"\n";
			f << "output = \"" << CurlConfigEscape(outPath.string()) << "\"\n";
			f << "write-out = \"%{http_code}\"\n";
			RestrictOwnerReadWrite(cfgPath);
		}

		const std::string cmd = ShellQuote(CurlBinary()) + " --config " +
		                        ShellQuote(cfgPath.string()) + " 2>/dev/null";
		FILE* pipe = popen(cmd.c_str(), "r");
		std::string status;
		if (pipe != nullptr) {
			char buf[64];
			while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
				status += buf;
			}
			(void)pclose(pipe);
		}
		TrimTrailingSpace(status);
		if (status.size() >= 3 && status[status.size() - 3] == '2') {
			std::ifstream in(outPath, std::ios::binary);
			if (in.is_open()) {
				body.assign(std::istreambuf_iterator<char>(in),
				            std::istreambuf_iterator<char>());
				ok = !body.empty();
			}
		}
	} catch (...) {
		ok = false;
	}

	std::error_code ec;
	std::filesystem::remove(outPath, ec);
	std::filesystem::remove(cfgPath, ec);
	return ok;
}

}  // namespace ibTemplateWizardHttp
