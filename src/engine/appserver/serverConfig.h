#ifndef __APP_SERVER_CONFIG_H__
#define __APP_SERVER_CONFIG_H__

// THE SERVER FOLDER — what the application server serves, and the key its secrets are sealed with
// (docs/private/multi-base-process.md § 5).
//
//   <server folder>/
//     server.conf             the bases — one group each
//     server.key              this installation's key: made at the first start, never part of a release
//     <Id>/                   one folder per base, named by its Id: a Firebird base's sys.fdb, the base's
//                             infobase.conf and its journal (a PostgreSQL base: infobase.conf and journal)
//
//   ; server.conf
//   [trade]                   the group is the base's name in the journal
//   Kind=firebird             firebird — a file base, in <server folder>/<Id> unless Path names another;
//                             postgresql — a server one: Server, Port, Database, User, Password
//   Id=…                      written at the first start — the folder keeps it, not the name
//   IbUser=Admin              the base's user the server logs in as
//   IbPassword=               empty, or enc:… — sealed by `appserver --set-password=trade/IbPassword`
//
// A secret is empty — a DBMS that trusts the machine, a base user without one — or sealed with the key. A
// copy of the config alone opens nowhere; a copy of the whole folder carries the key with it.

#include <vector>

#include <wx/string.h>

#include "backend/appData.h"                 // ibDatabaseMode
#include "backend/diagnostics/journal.h"

// What the application server says goes through Print, not the journal macros: those compile to nothing in a
// release build, and this is what a person watching the console must always see.
template <typename... Args>
void ibAppServerSay(ibJournalMark mark, const wxFormatString& format, const Args&... args)
{
	ibTechJournal::Print(mark, wxT("appserver"), format, args...);
}

// One base as server.conf names it.
struct ibConfiguredInstance {
	wxString       m_name;            // the group — the base's name in the journal
	wxString       m_id;              // its folder's name, given at the first start
	wxString       m_kind;            // as written, for the refusal that names it
	ibDatabaseMode m_mode = eNONE;    // eFILE — firebird, eSERVER — postgresql, eNONE — neither
	wxString       m_path;            // its folder: <server folder>/<Id> unless Path names another
	wxString       m_server;
	wxString       m_port;
	wxString       m_database;
	wxString       m_user;            // the DBMS's
	wxString       m_password;        // as stored: empty or sealed
	wxString       m_ibUser;          // the base's user the server logs in as
	wxString       m_ibPassword;      // as stored: empty or sealed
};

class ibServerConfig {
public:
	explicit ibServerConfig(const wxString& folder);

	const wxString& GetConfPath() const { return m_confPath; }
	bool            HasConf() const;

	// The installation's key, read — or made, at the first start. False, with the reason, when it cannot be.
	bool LoadKey(wxString& error);

	// Every base the config lists, as written.
	std::vector<ibConfiguredInstance> ReadInstances() const;

	// A base seen for the first time is assigned its Id, written back into the config — its folder is named by
	// it, so renaming the group does not lose the folder — and every base learns its folder.
	void AssignIds(std::vector<ibConfiguredInstance>& instances) const;

	// A secret as stored — empty, or sealed with the key. Plain text is refused: it is what the key keeps out.
	bool OpenSecret(const wxString& base, const wxString& name, const wxString& stored, wxString& secret,
		wxString& error) const;

	// A secret sealed with the key and written into the config.
	bool SealSecret(const wxString& base, const wxString& name, const wxString& secret, wxString& error) const;

private:
	wxString                   m_folder;
	wxString                   m_confPath;
	std::vector<unsigned char> m_key;
};

#endif
