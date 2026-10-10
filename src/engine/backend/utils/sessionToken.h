#ifndef __SESSION_TOKEN_H__
#define __SESSION_TOKEN_H__

// THE BEARER TOKEN of a thin client's session (docs/public/session-failover.md). The database and the
// process keep the SHA-256 of it, never the token, and a presented token is compared as that digest,
// in constant time. The bytes come from Mbed TLS's CTR-DRBG — at least 128 bits; this draws 32 bytes.

#include "backend/backend.h"

#include <cstddef>
#include <vector>

#include <wx/string.h>

namespace ibSessionToken {

constexpr std::size_t kBytes  = 32;
constexpr std::size_t kDigest = 32;

// 32 bytes from Mbed TLS CTR-DRBG. Empty when the platform gave no randomness.
BACKEND_API std::vector<unsigned char> Generate();

// Lowercase hex of `bytes`. Empty when `bytes` is null.
BACKEND_API wxString Hex(const unsigned char* bytes, std::size_t length);

// SHA-256 of the raw token bytes, lowercase hex (64 characters). Empty when `bytes` is null.
BACKEND_API wxString HashHex(const unsigned char* bytes, std::size_t length);

// The digest itself, for the constant-time compare. False when `bytes` is null.
BACKEND_API bool Hash(const unsigned char* bytes, std::size_t length, unsigned char out[kDigest]);

// 64 hex characters back into 32 bytes. False when it is not that.
BACKEND_API bool ParseHex(const wxString& hex, unsigned char out[kBytes]);

// Constant-time compare of two digests. False when either pointer is null.
BACKEND_API bool DigestEqual(const unsigned char* left, const unsigned char* right);

} // namespace ibSessionToken

#endif
