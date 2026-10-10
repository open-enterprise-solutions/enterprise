/////////////////////////////////////////////////////////////////////////////
// Password hashing for OES user authentication.
//
// New passwords are stored as PBKDF2-HMAC-SHA256 with a per-user salt and
// a modest iteration count (OWASP-recommended class of primitive).
// Legacy 32-hex MD5 hashes are still recognised on login so existing
// databases keep working; the caller should transparently re-hash to the
// new format after a successful legacy verify (lazy upgrade).
//
// Output format (self-describing, single column):
//   $pbkdf2-sha256$<iter>$<base64-salt>$<base64-hash>
// Length: ~120 chars. Fits in the existing password column.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_PASSWORD_HASH_H_
#define _IB_PASSWORD_HASH_H_

#include "backend/backend.h"

class BACKEND_API ibPasswordHash {
public:
	// Hash a plain-text password for storage. Returns the PHC-style string.
	static wxString Hash(const wxString& password);

	// Verify a plain-text password against a stored hash.
	// Accepts both the new PBKDF2 format and a legacy 32-hex MD5 string.
	// If the stored value looks like MD5, compares via ibMD5 — the caller
	// should then call IsLegacy() and re-store with Hash() to upgrade.
	static bool Verify(const wxString& password, const wxString& storedHash);

	// True if storedHash is a legacy MD5 (32 hex chars) rather than PBKDF2.
	static bool IsLegacy(const wxString& storedHash);

	// True if storedHash should be replaced after a successful verify:
	// legacy MD5, or PBKDF2 with a below-policy iteration count. Callers
	// use this to drive lazy upgrade — re-hash with Hash() and re-store.
	static bool NeedsRehash(const wxString& storedHash);

	// What the user dialog shows instead of a stored hash, and what it
	// must never write back as a password.
	static const wxString Placeholder();

	// The hash the dialog stores. Untouched, or still the placeholder,
	// keeps `stored`. A box the person cleared stores no password. Any
	// other text is hashed once.
	static wxString PasswordToStore(const wxString& stored, bool touched, const wxString& text);
};

#endif // _IB_PASSWORD_HASH_H_
