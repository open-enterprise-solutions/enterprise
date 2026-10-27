#ifndef __SESSION_TOKEN_H__
#define __SESSION_TOKEN_H__

// THE BEARER TOKEN of a thin client's session (docs/public/client-protocol.md). The database and the
// process keep the SHA-256 of it, never the token, and a presented token is compared as that digest,
// in constant time. The bytes come from Mbed TLS's CTR-DRBG — at least 128 bits; this draws 32 bytes.

#include "core/core.h"

#include <cstddef>
#include <string>
#include <vector>


namespace ibSessionToken {

constexpr std::size_t kBytes  = 32;
constexpr std::size_t kDigest = 32;

// 32 bytes from Mbed TLS CTR-DRBG. Empty when the platform gave no randomness.
CORE_API std::vector<unsigned char> Generate();

// Lowercase hex of `bytes`. Empty when `bytes` is null.
CORE_API ibString Hex(const unsigned char* bytes, std::size_t length);

// SHA-256 of the raw token bytes, lowercase hex (64 characters). Empty when `bytes` is null.
CORE_API ibString HashHex(const unsigned char* bytes, std::size_t length);

// The digest itself, for the constant-time compare. False when `bytes` is null.
CORE_API bool Hash(const unsigned char* bytes, std::size_t length, unsigned char out[kDigest]);

// 64 hex characters back into 32 bytes. False when it is not that.
CORE_API bool ParseHex(const ibString& hex, unsigned char out[kBytes]);

// THE ROUTE — which of a base's servers holds the session (server.conf's Processes, appserver --route): the token
// handed out ends in it, `<hex>.<route>`, and the cluster carries a client that comes back with it to that server.
// No secret: the hex is the bearer, and only the hex is hashed. 0 — a server alone, or none: the hex as it is.
CORE_API ibString WithRoute(const ibString& hex, unsigned route);
// …and taken apart: the hex, and the route it ends in — 0 when it ends in none.
CORE_API void SplitRoute(const ibString& token, ibString& hex, unsigned& route);

// Constant-time compare of two digests. False when either pointer is null.
CORE_API bool DigestEqual(const unsigned char* left, const unsigned char* right);

// A presented text against the expected one, in constant time over the expected one's length - the length
// itself is compared first and is no secret (a token's is fixed). False for an empty expected text: nothing
// presented matches a token that was never minted.
CORE_API bool TextEqual(const std::string& presented, const std::string& expected);

} // namespace ibSessionToken

#endif
