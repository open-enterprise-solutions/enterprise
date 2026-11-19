/////////////////////////////////////////////////////////////////////////////
// Password hashing for OES user authentication.
//
// Passwords are stored as PBKDF2-HMAC-SHA256 with a per-user salt and
// a modest iteration count (OWASP-recommended class of primitive).
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
	static ibString Hash(const ibString& password);

	// Verify a plain-text password against a stored hash.
	static bool Verify(const ibString& password, const ibString& storedHash);

	// True if storedHash should be replaced after a successful verify: a
	// below-policy iteration count. Callers re-hash with Hash() and re-store.
	static bool NeedsRehash(const ibString& storedHash);

	// The hash the user dialog stores. Untouched keeps `stored`. A box the
	// person cleared stores no password. Any other text is hashed once.
	static ibString PasswordToStore(const ibString& stored, bool touched, const ibString& text);
};

#endif // _IB_PASSWORD_HASH_H_
