////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : JWT HS256 auth for OES web server (no external libs)
////////////////////////////////////////////////////////////////////////////

#ifndef _IB_WEB_AUTH_H__
#define _IB_WEB_AUTH_H__

#include "backend/backend.h"

#include <string>
#include <vector>
#include <cstdint>

//***********************************************************************
//*                         ibWebAuthClaims                            *
//***********************************************************************

struct BACKEND_API ibWebAuthClaims {
	std::string sub;    // user GUID
	std::string name;   // user login name
	std::vector<std::string> roles;
	int64_t iat = 0;    // issued at (Unix epoch)
	int64_t exp = 0;    // expiry   (Unix epoch)

	bool IsExpired() const;
};

//***********************************************************************
//*                           ibWebAuth                                *
//***********************************************************************

class BACKEND_API ibWebAuth {
public:

	// Call once at startup; if not called a random secret is generated
	static void SetSecret(const std::string& secret);

	// Issue a signed JWT for the given identity
	static std::string CreateToken(const std::string& userGuid,
		const std::string& userName,
		const std::vector<std::string>& roles);

	// Validate a raw JWT string.
	// Returns true and fills outClaims on success.
	// Returns false (invalid signature, malformed, expired) otherwise.
	static bool ValidateToken(const std::string& token,
		ibWebAuthClaims& outClaims);

	// Decode payload without signature verification (for debugging/fallback)
	static bool DecodePayload(const std::string& token,
		ibWebAuthClaims& outClaims);

	// Token lifetime in seconds (default: 15 minutes)
	static constexpr int64_t ms_tokenLifetimeSec = 15 * 60;

private:

	// Base64URL helpers (no padding)
	static std::string Base64UrlEncode(const uint8_t* data, size_t len);
	static std::string Base64UrlEncode(const std::string& s);
	static std::vector<uint8_t> Base64UrlDecode(const std::string& s);

	// HMAC-SHA256 (pure C++17, no OpenSSL dependency)
	static std::vector<uint8_t> HmacSha256(const std::string& key,
		const std::string& msg);

	// SHA-256 block transform (RFC 6234 table-based)
	static std::vector<uint8_t> Sha256(const uint8_t* data, size_t len);

	// Lazy-initialised secret key
	static std::string& Secret();

	// JSON encode / decode helpers (minimal, no full JSON parser needed here)
	static std::string BuildHeader();
	static std::string BuildPayload(const ibWebAuthClaims& claims);
	static bool ParsePayload(const std::string& json, ibWebAuthClaims& out);
};

#endif // _IB_WEB_AUTH_H__
