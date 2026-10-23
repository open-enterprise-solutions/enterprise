// webserver — the bases' transport for browsers: a site's files and the protocol of a base's clients, over HTTP and
// HTTPS (webServer.h — what it serves and web.conf; core/net/httpServer.h — the port). Started by hand it narrates in its
// console — the technological journal mirrored onto standard error — and stays until Ctrl+C; later it becomes a
// service. What it publishes is web.conf's, and the command line's over it:
//
//   webserver --file=bases/trade                     the file base of that folder, at /trade/client
//   webserver --server=oes://appserver/trade         a base of an application server, at /trade/client
//   webserver --file=bases/trade --name=shop --port=0

#include <wx/app.h>
#include <wx/cmdline.h>
#include <wx/filename.h>
#include "core/fileSystem.h"

#ifdef __WXMSW__
#include <windows.h>
#endif

#include <cstdio>

#include "webServer.h"
#include "frontend/diagnostics/oesConsole.h"
#include "protocol/connectionServer.h"   // the base an address names — the publication's name left out

static const wxCmdLineEntryDesc s_cmdLineDesc[] = {
	{ wxCMD_LINE_OPTION, "d",  "dir",        "Web folder: web.conf and the site's files in www (default: web beside the executable)", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "H",  "host",       "Where browsers reach it (default: web.conf's Host, else 127.0.0.1; empty - every interface)", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "p",  "port",       "The HTTP port (default: web.conf's HttpPort, else 8080; 0 - the system chooses)", wxCMD_LINE_VAL_NUMBER, 0 },
	{ wxCMD_LINE_OPTION, "sp", "https-port", "The HTTPS port, over web.conf's Certificate and PrivateKey", wxCMD_LINE_VAL_NUMBER, 0 },
	{ wxCMD_LINE_OPTION, "f",  "file",       "Publish the file base of this folder", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "s",  "server",     "Publish this base of an application server: oes://server[:port]/<base>, oess:// over TLS", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "t",  "trusted",    "--server=oess://: the authorities trusted, a PEM file (default: the system's store)", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "n",  "name",       "The publication's name in the address (default: the base's folder, or its name on the server)", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_OPTION, "lc", "locale",     "The platform's locale for the file base (default: as the base says)", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_SWITCH, "g",  "debug",      "The engine's debug server for the file base, as enterprise's --debug", wxCMD_LINE_VAL_NONE, 0 },
	{ wxCMD_LINE_OPTION, "m",  "manifest",   "Write the publication's page address to this file once the port is open (url=...)", wxCMD_LINE_VAL_STRING, 0 },
	{ wxCMD_LINE_SWITCH, "h",  "help",       "Show this help message.", wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP },
	{ wxCMD_LINE_NONE,   nullptr, nullptr,   nullptr,                   wxCMD_LINE_VAL_NONE, 0 }
};

#include "core/diagnostics/leakTracker.h"

IB_LEAK_TRACKER_ARM();

namespace {

// Where a web server started without --dir keeps its web.conf and its site — `web` in the program folder.
wxString DefaultWebDir()
{
	wxFileName fn = wxFileName::DirName(ibProgramFolder());
	fn.AppendDir(wxT("web"));
	return fn.GetPath();
}

// A port from the command line — false, said, when it is not one.
bool ReadPort(const wxCmdLineParser& parser, const wxString& option, std::optional<std::uint16_t>& port)
{
	long number = 0;
	if (!parser.Found(option, &number))
		return true;
	if (number < 0 || number > 65535) {
		ibWebServerSay(ibJournalMark::Error, wxT("--%s=%ld is not a port"), option, number);
		return false;
	}
	port = static_cast<std::uint16_t>(number);
	return true;
}

// The publication the command line names — a file base's folder or a server's base; false, said, when it names both.
bool ReadPublication(const wxCmdLineParser& parser, ibWebConfig& config)
{
	ibWebPublication publication;
	wxString file, server;
	const bool isFile = parser.Found(wxT("file"), &file);
	const bool isServer = parser.Found(wxT("server"), &server);
	if (isFile && isServer) {
		ibWebServerSay(ibJournalMark::Error, wxT("--file and --server name a publication each - web.conf holds more than one"));
		return false;
	}
	if (!isFile && !isServer)
		return true;

	if (isFile) {
		wxFileName folder = wxFileName::DirName(file);
		folder.MakeAbsolute();
		publication.m_kind = wxT("firebird");
		publication.m_directory = folder.GetPath();
		publication.m_name = folder.GetDirCount() > 0 ? folder.GetDirs().Last() : wxString();
		parser.Found(wxT("locale"), &publication.m_locale);
		publication.m_debug = parser.Found(wxT("debug"));
	}
	else {
		wxString host, base;
		int port = 0;
		bool secure = false;
		publication.m_kind = wxT("server");
		publication.m_address = server;
		if (ibProtocolConnectionServer::ParseAddress(server, host, port, base, secure))
			publication.m_name = base;
		wxString trusted;
		if (parser.Found(wxT("trusted"), &trusted)) {
			wxFileName file(trusted);
			file.MakeAbsolute();
			publication.m_trusted = file.GetFullPath();
		}
	}
	parser.Found(wxT("name"), &publication.m_name);
	config.m_publications.push_back(publication);
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	wxApp::CheckBuildOptions(WX_BUILD_OPTIONS_SIGNATURE, "webserver");

	// wxInitializer + ibCrashGuard::Install, in one line — a file base's engine runs in this process.
	ibOesConsoleBoot boot(wxT("webserver"), argc, argv);
	if (!boot.IsOk()) {
		std::fprintf(stderr, "Failed to initialize the wxWidgets library, aborting.");
		return -1;
	}

	// What the server says, and every warning and error, is what its console shows; the running commentary is the
	// journal file's.
	ibTechJournal::EchoToStderr();

	wxCmdLineParser parser(s_cmdLineDesc, argc, argv);
	if (parser.Parse() != 0)
		return 1;

	wxString folder = DefaultWebDir();
	parser.Found(wxT("dir"), &folder);
	ibWebConfig config;
	wxString error;
	if (!config.Read(folder, error)) {
		ibWebServerSay(ibJournalMark::Error, wxT("%s"), error);
		return 1;
	}
	parser.Found(wxT("host"), &config.m_host);
	parser.Found(wxT("manifest"), &config.m_manifest);
	if (!ReadPort(parser, wxT("port"), config.m_httpPort) || !ReadPort(parser, wxT("https-port"), config.m_httpsPort)
		|| !ReadPublication(parser, config))
		return 1;

#ifdef __WXMSW__
	::DisableProcessWindowsGhosting();
#endif

	return ibWebServer(config).Run();
}
