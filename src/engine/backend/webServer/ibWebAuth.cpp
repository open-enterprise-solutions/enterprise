////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : JWT HS256 implementation — no OpenSSL, no external libs
//	              SHA-256 per FIPS 180-4; HMAC per RFC 2104
////////////////////////////////////////////////////////////////////////////

#include "ibWebAuth.h"

#include <json.hpp>

#include <array>
#include <cstring>
#include <ctime>
#include <mutex>
#include <random>

using json = nlohmann::json;

//***********************************************************************
//*                         ibWebAuthClaims                            *
//***********************************************************************

bool ibWebAuthClaims::IsExpired() const
{
	return std::time(nullptr) >= exp;
}

//***********************************************************************
//*                      SHA-256 implementation                        *
//***********************************************************************

namespace {

// SHA-256 constants (first 32 bits of cube roots of first 64 primes)
static constexpr uint32_t SHA256_K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t RotR32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline uint32_t Sigma0(uint32_t x) { return RotR32(x, 2)  ^ RotR32(x, 13) ^ RotR32(x, 22); }
inline uint32_t Sigma1(uint32_t x) { return RotR32(x, 6)  ^ RotR32(x, 11) ^ RotR32(x, 25); }
inline uint32_t sigma0(uint32_t x) { return RotR32(x, 7)  ^ RotR32(x, 18) ^ (x >> 3); }
inline uint32_t sigma1(uint32_t x) { return RotR32(x, 17) ^ RotR32(x, 19) ^ (x >> 10); }
inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)  { return (x & y) ^ (~x & z); }
inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }

// Big-endian read/write helpers
inline uint32_t ReadBE32(const uint8_t* p)
{
	return (static_cast<uint32_t>(p[0]) << 24) |
		(static_cast<uint32_t>(p[1]) << 16) |
		(static_cast<uint32_t>(p[2]) << 8)  |
		static_cast<uint32_t>(p[3]);
}

inline void WriteBE32(uint8_t* p, uint32_t v)
{
	p[0] = static_cast<uint8_t>(v >> 24);
	p[1] = static_cast<uint8_t>(v >> 16);
	p[2] = static_cast<uint8_t>(v >> 8);
	p[3] = static_cast<uint8_t>(v);
}

} // anonymous namespace

//***********************************************************************
//*                           ibWebAuth                                *
//***********************************************************************

// --- Secret storage (thread-safe) ---

std::string& ibWebAuth::Secret()
{
	static std::string s_secret;
	static std::once_flag s_initFlag;

	std::call_once(s_initFlag, []() {
		// Generate a random 32-byte secret on first use
		std::random_device rd;
		std::mt19937_64 rng(rd());
		std::uniform_int_distribution<uint8_t> dist(0, 255);
		s_secret.resize(32);
		for (auto& c : s_secret)
			c = static_cast<char>(dist(rng));
	});

	return s_secret;
}

void ibWebAuth::SetSecret(const std::string& secret)
{
	Secret() = secret;
}

// --- SHA-256 ---

std::vector<uint8_t> ibWebAuth::Sha256(const uint8_t* data, size_t len)
{
	// Initial hash values (first 32 bits of square roots of first 8 primes)
	uint32_t H[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
	};

	// Pre-processing: pad message
	uint64_t bitLen = static_cast<uint64_t>(len) * 8;
	size_t padLen = ((len + 8) / 64 + 1) * 64;

	std::vector<uint8_t> msg(padLen, 0);
	std::memcpy(msg.data(), data, len);
	msg[len] = 0x80;

	// Append original length as 64-bit big-endian
	for (int i = 0; i < 8; ++i)
		msg[padLen - 1 - i] = static_cast<uint8_t>(bitLen >> (i * 8));

	// Process each 512-bit (64-byte) chunk
	for (size_t chunk = 0; chunk < padLen; chunk += 64) {
		uint32_t W[64];
		for (int t = 0; t < 16; ++t)
			W[t] = ReadBE32(msg.data() + chunk + t * 4);
		for (int t = 16; t < 64; ++t)
			W[t] = sigma1(W[t - 2]) + W[t - 7] + sigma0(W[t - 15]) + W[t - 16];

		uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
		uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

		for (int t = 0; t < 64; ++t) {
			uint32_t T1 = h + Sigma1(e) + Ch(e, f, g) + SHA256_K[t] + W[t];
			uint32_t T2 = Sigma0(a) + Maj(a, b, c);
			h = g; g = f; f = e; e = d + T1;
			d = c; c = b; b = a; a = T1 + T2;
		}

		H[0] += a; H[1] += b; H[2] += c; H[3] += d;
		H[4] += e; H[5] += f; H[6] += g; H[7] += h;
	}

	std::vector<uint8_t> digest(32);
	for (int i = 0; i < 8; ++i)
		WriteBE32(digest.data() + i * 4, H[i]);

	return digest;
}

// --- HMAC-SHA256 (RFC 2104) ---

std::vector<uint8_t> ibWebAuth::HmacSha256(const std::string& key, const std::string& msg)
{
	constexpr size_t BLOCK = 64;

	// Derive key block
	uint8_t kPad[BLOCK] = {};

	if (key.size() > BLOCK) {
		// Hash the key if it's too long
		auto hk = Sha256(reinterpret_cast<const uint8_t*>(key.data()), key.size());
		std::memcpy(kPad, hk.data(), hk.size());
	}
	else {
		std::memcpy(kPad, key.data(), key.size());
	}

	// Inner and outer padded keys
	uint8_t iPad[BLOCK], oPad[BLOCK];
	for (size_t i = 0; i < BLOCK; ++i) {
		iPad[i] = kPad[i] ^ 0x36;
		oPad[i] = kPad[i] ^ 0x5c;
	}

	// Inner hash: H(iPad || msg)
	std::vector<uint8_t> inner;
	inner.resize(BLOCK + msg.size());
	std::memcpy(inner.data(), iPad, BLOCK);
	std::memcpy(inner.data() + BLOCK, msg.data(), msg.size());
	auto innerHash = Sha256(inner.data(), inner.size());

	// Outer hash: H(oPad || innerHash)
	std::vector<uint8_t> outer;
	outer.resize(BLOCK + 32);
	std::memcpy(outer.data(), oPad, BLOCK);
	std::memcpy(outer.data() + BLOCK, innerHash.data(), 32);

	return Sha256(outer.data(), outer.size());
}

// --- Base64URL (RFC 4648 §5, no padding) ---

std::string ibWebAuth::Base64UrlEncode(const uint8_t* data, size_t len)
{
	static const char kAlpha[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

	std::string out;
	out.reserve((len + 2) / 3 * 4);

	for (size_t i = 0; i < len; i += 3) {
		uint32_t b = static_cast<uint32_t>(data[i]) << 16;
		if (i + 1 < len) b |= static_cast<uint32_t>(data[i + 1]) << 8;
		if (i + 2 < len) b |= static_cast<uint32_t>(data[i + 2]);

		out.push_back(kAlpha[(b >> 18) & 0x3f]);
		out.push_back(kAlpha[(b >> 12) & 0x3f]);
		if (i + 1 < len) out.push_back(kAlpha[(b >> 6) & 0x3f]);
		if (i + 2 < len) out.push_back(kAlpha[b & 0x3f]);
	}

	return out;
}

std::string ibWebAuth::Base64UrlEncode(const std::string& s)
{
	return Base64UrlEncode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::vector<uint8_t> ibWebAuth::Base64UrlDecode(const std::string& s)
{
	// Build decode table
	static const int8_t kTable[256] = [] {
		std::array<int8_t, 256> t;
		t.fill(-1);
		const char* alpha =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		for (int i = 0; i < 64; ++i)
			t[static_cast<uint8_t>(alpha[i])] = static_cast<int8_t>(i);
		return t;
	}();

	std::vector<uint8_t> out;
	out.reserve(s.size() * 3 / 4 + 2);

	uint32_t acc = 0;
	int bits = 0;

	for (char c : s) {
		int8_t v = kTable[static_cast<uint8_t>(c)];
		if (v < 0) continue; // skip padding / unknown chars
		acc = (acc << 6) | static_cast<uint32_t>(v);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<uint8_t>(acc >> bits));
		}
	}

	return out;
}

// --- JSON helpers ---

std::string ibWebAuth::BuildHeader()
{
	// Fixed JWT header: {"alg":"HS256","typ":"JWT"}
	return Base64UrlEncode(R"({"alg":"HS256","typ":"JWT"})");
}

std::string ibWebAuth::BuildPayload(const ibWebAuthClaims& claims)
{
	json j;
	j["sub"]  = claims.sub;
	j["name"] = claims.name;

	json jRoles = json::array();
	for (const auto& r : claims.roles)
		jRoles.push_back(r);
	j["roles"] = jRoles;

	j["iat"] = claims.iat;
	j["exp"] = claims.exp;

	return Base64UrlEncode(j.dump());
}

bool ibWebAuth::ParsePayload(const std::string& encoded, ibWebAuthClaims& out)
{
	auto raw = Base64UrlDecode(encoded);
	if (raw.empty())
		return false;

	try {
		auto j = json::parse(raw.begin(), raw.end());

		out.sub  = j.value("sub",  std::string());
		out.name = j.value("name", std::string());
		out.iat  = j.value("iat",  int64_t(0));
		out.exp  = j.value("exp",  int64_t(0));

		out.roles.clear();
		if (j.contains("roles") && j["roles"].is_array()) {
			for (const auto& r : j["roles"])
				out.roles.push_back(r.get<std::string>());
		}
	}
	catch (...) {
		return false;
	}

	return !out.sub.empty();
}

// --- Public API ---

std::string ibWebAuth::CreateToken(const std::string& userGuid,
	const std::string& userName,
	const std::vector<std::string>& roles)
{
	ibWebAuthClaims claims;
	claims.sub   = userGuid;
	claims.name  = userName;
	claims.roles = roles;
	claims.iat   = static_cast<int64_t>(std::time(nullptr));
	claims.exp   = claims.iat + ms_tokenLifetimeSec;

	std::string header  = BuildHeader();
	std::string payload = BuildPayload(claims);
	std::string signingInput = header + '.' + payload;

	auto mac = HmacSha256(Secret(), signingInput);
	std::string sig = Base64UrlEncode(mac.data(), mac.size());

	return signingInput + '.' + sig;
}

bool ibWebAuth::ValidateToken(const std::string& token, ibWebAuthClaims& outClaims)
{
	// Split into header.payload.signature
	size_t dot1 = token.find('.');
	if (dot1 == std::string::npos)
		return false;

	size_t dot2 = token.find('.', dot1 + 1);
	if (dot2 == std::string::npos)
		return false;

	std::string headerPart  = token.substr(0, dot1);
	std::string payloadPart = token.substr(dot1 + 1, dot2 - dot1 - 1);
	std::string sigPart     = token.substr(dot2 + 1);

	// Re-compute expected signature
	std::string signingInput = headerPart + '.' + payloadPart;
	auto expectedMac  = HmacSha256(Secret(), signingInput);
	std::string expectedSig = Base64UrlEncode(expectedMac.data(), expectedMac.size());

	// Constant-time comparison to prevent timing attacks
	if (expectedSig.size() != sigPart.size())
		return false;

	uint8_t diff = 0;
	for (size_t i = 0; i < expectedSig.size(); ++i)
		diff |= static_cast<uint8_t>(expectedSig[i]) ^ static_cast<uint8_t>(sigPart[i]);

	if (diff != 0)
		return false;

	// Parse payload
	if (!ParsePayload(payloadPart, outClaims))
		return false;

	// Check expiry
	if (outClaims.IsExpired())
		return false;

	return true;
}
