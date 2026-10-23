#ifndef __CORE_SERVER_CONFIG_H__
#define __CORE_SERVER_CONFIG_H__

// THE SERVER FOLDER — what the application server serves, and the key its secrets are sealed with
// (docs/private/multi-base-process.md § 5). Read by the backend: a server opens its base by the base's name and
// reads where the base lives itself (ibApplicationInstance::CreateAppDataEnv(ibServerInstanceRequest)) — nobody
// hands it a DBMS. The application cluster (appcluster, docs/private/appcluster.md) lists the bases and starts a server
// for each (appserver --worker=<base>), which logs into its own.
//
//   <server folder>/
//     server.conf             the bases — one group each
//     server.key              this installation's key: made at the first start, never part of a release
//     <Id>/                   one folder per base, named by its Id: a Firebird base's sys.fdb, the base's
//                             infobase.conf and its journal (a PostgreSQL base: infobase.conf and journal)
//
//   ; server.conf
//   Host=                     where the clients reach the cluster that runs the bases: left out — 127.0.0.1, this
//                             machine alone; empty — every interface. Off this machine it speaks TLS, over the
//                             certificate backend.conf names (core/server/listener.h): oess://
//   Port=7373                 …and its port: ibPort::Clients (core/net/ports.h) unless another is written; written
//                             at the first start, so it stays the one clients are pointed at. Taken, it is refused,
//                             never moved off
//   [trade]                   the group is the base's name in the journal
//   Kind=firebird             firebird — a file base, in <server folder>/<Id> unless Path names another;
//                             postgresql — a server one: Server, Port, Database, User, Password
//   Id=…                      written at the first start — the folder keeps it, not the name
//   Processes=1               how many servers the cluster runs for the base, side by side: a client goes to the one
//                             its session lives in (ibSessionToken::WithRoute), a new one to the least busy
//   McpPort=7600              the base's assistant access (MCP), when its settings switch it on: written at the
//                             first start — the first of ibPort::BaseAssistant no other base has
//   IbUser=Admin              the base's user the server logs in as
//   IbPassword=               empty, or enc:… — sealed by `appserver --set-password=trade/IbPassword`
//
// A secret is empty — a DBMS that trusts the machine, a base user without one — or sealed with the key. A
// copy of the config alone opens nowhere; a copy of the whole folder carries the key with it.

#include <vector>

#include "core/core.h"
#include "core/fstring.h"
#include "core/diagnostics/journal.h"
#include "core/net/ports.h"

// What the application server says goes through Print, not the journal macros: those compile to nothing in a
// release build, and this is what a person watching the console must always see.
template <typename... Args>
void ibAppServerSay(ibJournalMark mark, const wxFormatString& format, const Args&... args)
{
	ibTechJournal::Print(mark, wxT("appserver"), format, args...);
}

// One base as server.conf names it.
struct ibConfiguredInstance {
	ibString       m_name;            // the group — the base's name in the journal
	ibString       m_id;              // its folder's name, given at the first start
	ibString       m_kind;            // lower-cased: firebird, postgresql — the opening decides, and refuses another
	ibString       m_path;            // its folder: <server folder>/<Id> unless Path names another
	ibString       m_server;
	ibString       m_port;
	ibString       m_database;
	ibString       m_user;            // the DBMS's
	ibString       m_password;        // as stored: empty or sealed
	ibString       m_ibUser;          // the base's user the server logs in as
	ibString       m_ibPassword;      // as stored: empty or sealed
	ibString       m_mcpPort;         // its assistant access's port, as written
	unsigned       m_processes = 1;   // its servers side by side — at least one
};

class CORE_API ibServerConfig {
public:
	explicit ibServerConfig(const ibString& folder);

	const ibString& GetFolder() const { return m_folder; }
	const ibString& GetConfPath() const { return m_confPath; }
	bool            HasConf() const;

	// The installation's key, read — or made, at the first start. False, with the reason, when it cannot be.
	bool LoadKey(ibString& error);

	// THE CLUSTER'S TOKEN (core/server/clusterControl.h) — a new one written into the folder at each start of the cluster,
	// read by its tray, and removed as the cluster goes: a token left behind would be read by a tray as the next
	// cluster's, before that one wrote its own. False, with the reason, when it cannot be.
	bool WriteClusterToken(ibString& token, ibString& error) const;
	bool ReadClusterToken(ibString& token, ibString& error) const;
	bool RemoveClusterToken(ibString& error) const;

	// Every base the config lists, as written.
	std::vector<ibConfiguredInstance> ReadInstances() const;

	// A base seen for the first time is assigned its Id, written back into the config — its folder is named by
	// it, so renaming the group does not lose the folder — and every base learns its folder.
	void AssignIds(std::vector<ibConfiguredInstance>& instances) const;

	// …and its assistant access's port, the first of ibPort::BaseAssistant no other base has, written back too: two
	// bases of one cluster never meet on one port. A port written by hand is kept as written.
	void AssignMcpPorts(std::vector<ibConfiguredInstance>& instances) const;

	// WHERE THE CLIENTS REACH THIS SERVER — the keys above the first base's group (a group IS a base). The host is
	// this machine's own when none is written: a person has to mean it to widen that, and to name a certificate for it.
	// The port is 0 when none is written yet; a port chosen at the first start is written back. False, with the reason,
	// when what is written is not a port — a mistake is said, never chosen over.
	ibString ReadHost() const;
	bool     ReadPort(unsigned short& port, ibString& error) const;
	void     WritePort(unsigned short port) const;

	// Where a server started without --dir keeps its bases — `server` in the program folder (beside the .app in a bundle).
	static ibString DefaultFolder();

	// A secret as stored — empty, or sealed with the key. Plain text is refused: it is what the key keeps out.
	bool OpenSecret(const ibString& base, const ibString& name, const ibString& stored, ibString& secret,
		ibString& error) const;

	// A secret sealed with the key and written into the config.
	bool SealSecret(const ibString& base, const ibString& name, const ibString& secret, ibString& error) const;

private:
	ibString                   m_folder;
	ibString                   m_confPath;
	std::vector<unsigned char> m_key;
};

// THE PIPE BETWEEN THE CLUSTER AND A SERVER IT RUNS (`appserver --worker=<base>`) — a line each way: the server says
// where its clients come in once it serves (`serving <port>`, on its stdout), the cluster tells it to stop (`stop`, on
// its stdin). Its stdin closing — the cluster gone — is a stop too: a server is never left without its cluster.
namespace ibWorkerPipe {
	inline constexpr const char* Serving = "serving";
	inline constexpr const char* Stop    = "stop";
}

#endif
