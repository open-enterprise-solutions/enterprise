#ifndef __FIREBIRD_REPLICATION_CONFIG_H__
#define __FIREBIRD_REPLICATION_CONFIG_H__

// `replication.conf` writer — generates Firebird 4+ replication
// configuration sidecar next to `firebird.conf` so the engine's
// built-in replication framework knows how to ship logs to peers
// and consume incoming streams.
//
// The on-disk format is a simple text file with `database = …` /
// `journal_directory = …` / `sync_replica = …` lines per database.
// We generate it programmatically from `ibFirebirdReplicationConfig`
// values rather than expose the conf file to users — they describe
// peers in `mesh.conf` (OES-level config) and the driver derives the
// FB-level `replication.conf` on startup.
//
// Reference: Firebird 4 docs → `doc/README.replication`.

#include "backend/backend.h"

#include <wx/string.h>

#include <vector>

class BACKEND_API ibFirebirdReplicationConfig {
public:
	// One peer to ship replicated transactions to. `address` is
	// `host:port` of the peer's incoming FB listener. `username` /
	// `password` are the credentials FB uses to authenticate as it
	// connects out to that peer.
	struct Peer {
		wxString address;     // "192.168.1.5:3050"
		wxString username;    // peer's auth user (default "SYSDBA")
		wxString password;    // peer's auth password
	};

	// Build a `replication.conf` for `databasePath`. `journalDir` is
	// where FB writes the local replication log segments — typically
	// `<dbDir>/repl_journal/`. `peers` is the list of remote peers
	// this database ships to.
	//
	// Returns the text content suitable for writing to disk.
	static wxString Build(
		const wxString& databasePath,
		const wxString& journalDir,
		const std::vector<Peer>& peers);

	// Write the generated content to `_fb/replication.conf`.
	// Returns true on success.
	static bool WriteToRuntimeDir(
		const wxString& fbRuntimeDir,
		const wxString& content);

	// Default ports / paths used when the user hasn't specified
	// otherwise.
	static constexpr uint16_t kDefaultPeerPort = 3050;
};

#endif // __FIREBIRD_REPLICATION_CONFIG_H__
