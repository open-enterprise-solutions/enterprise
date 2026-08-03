/////////////////////////////////////////////////////////////////////////////
// PBKDF2-HMAC-SHA256 password hashing + legacy MD5 compatibility.
/////////////////////////////////////////////////////////////////////////////

#include "passwordHash.hpp"
#include "sha256.hpp"
#include "md5.hpp"

#include <wx/base64.h>
#include <wx/string.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

// THREE platforms, not two. The old #else covered "everything that is not Windows"
// and reached for getrandom(), which is a LINUX (glibc) call: macOS ships
// <sys/random.h> too, but it declares getentropy / arc4random, never getrandom —
// so this did not compile there at all. A platform API needs a branch per platform
// that actually has it (portability.md § 1.4).
#if defined(__WXMSW__)
  #include <windows.h>
  #include <bcrypt.h>
  #pragma comment(lib, "bcrypt.lib")
#elif defined(__APPLE__)
  #include <cstdlib>          // arc4random_buf
#else
  #include <sys/random.h>     // getrandom
#endif

namespace {

// Iteration count per OWASP 2023 recommendation for PBKDF2-HMAC-SHA256.
// Must be re-evaluated every couple of years against current hardware.
// 600k iterations ≈ 300 ms on a mid-2020s desktop CPU — acceptable for
// interactive login, expensive enough that offline brute-force of a stolen
// hash column is costly. Argon2id would be the stronger choice (memory-
// hard) but requires vendoring a third-party library; PBKDF2 stays as it
// leans on primitives we already have (HMAC-SHA256 + system RNG).
constexpr uint32_t kIterations = 600000;
constexpr size_t   kSaltLen    = 16;
constexpr size_t   kKeyLen     = 32; // SHA-256 output size

const char* kPrefix = "$pbkdf2-sha256$";

void FillRandom(uint8_t* buf, size_t len)
{
#if defined(__WXMSW__)
	if (BCryptGenRandom(nullptr, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
		return;
#elif defined(__APPLE__)
	// arc4random_buf is the system CSPRNG on Apple platforms and cannot fail —
	// it returns void and never reports an error, so there is no fallback path
	// to take here.
	arc4random_buf(buf, len);
	return;
#else
	if (getrandom(buf, len, 0) == (ssize_t)len)
		return;
#endif
	// Fallback: portable but weaker — only reached if the system RNG syscall fails.
	std::random_device rd;
	for (size_t i = 0; i < len; ++i)
		buf[i] = uint8_t(rd());
}

void HmacSha256(const uint8_t* key, size_t keyLen,
                const uint8_t* msg, size_t msgLen,
                uint8_t out[ibSHA256::DIGEST_SIZE])
{
	uint8_t k[ibSHA256::BLOCK_SIZE] = {};
	if (keyLen > ibSHA256::BLOCK_SIZE) {
		ibSHA256::Hash(key, keyLen, k);
	} else {
		memcpy(k, key, keyLen);
	}

	uint8_t ipad[ibSHA256::BLOCK_SIZE];
	uint8_t opad[ibSHA256::BLOCK_SIZE];
	for (size_t i = 0; i < ibSHA256::BLOCK_SIZE; ++i) {
		ipad[i] = k[i] ^ 0x36;
		opad[i] = k[i] ^ 0x5c;
	}

	uint8_t inner[ibSHA256::DIGEST_SIZE];
	{
		ibSHA256 h;
		h.Update(ipad, ibSHA256::BLOCK_SIZE);
		h.Update(msg, msgLen);
		h.Final(inner);
	}
	{
		ibSHA256 h;
		h.Update(opad, ibSHA256::BLOCK_SIZE);
		h.Update(inner, ibSHA256::DIGEST_SIZE);
		h.Final(out);
	}
}

void Pbkdf2HmacSha256(const uint8_t* password, size_t passwordLen,
                     const uint8_t* salt, size_t saltLen,
                     uint32_t iterations,
                     uint8_t* outKey, size_t keyLen)
{
	const size_t hlen = ibSHA256::DIGEST_SIZE;
	const uint32_t blockCount = uint32_t((keyLen + hlen - 1) / hlen);

	std::vector<uint8_t> block(saltLen + 4);
	memcpy(block.data(), salt, saltLen);

	uint8_t U[ibSHA256::DIGEST_SIZE];
	uint8_t T[ibSHA256::DIGEST_SIZE];

	for (uint32_t i = 1; i <= blockCount; ++i) {
		block[saltLen]     = uint8_t(i >> 24);
		block[saltLen + 1] = uint8_t(i >> 16);
		block[saltLen + 2] = uint8_t(i >> 8);
		block[saltLen + 3] = uint8_t(i);

		HmacSha256(password, passwordLen, block.data(), block.size(), U);
		memcpy(T, U, hlen);

		for (uint32_t j = 1; j < iterations; ++j) {
			HmacSha256(password, passwordLen, U, hlen, U);
			for (size_t k = 0; k < hlen; ++k)
				T[k] ^= U[k];
		}

		const size_t copyLen = (i == blockCount) ? keyLen - (i - 1) * hlen : hlen;
		memcpy(outKey + (i - 1) * hlen, T, copyLen);
	}
}

// Constant-time byte compare — avoids a timing leak of the hash prefix match.
bool ConstTimeEqual(const uint8_t* a, const uint8_t* b, size_t len)
{
	uint8_t diff = 0;
	for (size_t i = 0; i < len; ++i) diff |= uint8_t(a[i] ^ b[i]);
	return diff == 0;
}

} // namespace

bool ibPasswordHash::IsLegacy(const wxString& storedHash)
{
	if (storedHash.length() != 32)
		return false;
	for (size_t i = 0; i < 32; ++i) {
		const wxChar c = storedHash[i];
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!hex) return false;
	}
	return true;
}

bool ibPasswordHash::NeedsRehash(const wxString& storedHash)
{
	if (IsLegacy(storedHash))
		return true;

	if (!storedHash.StartsWith(kPrefix))
		return false;

	// Parse the iteration count out of $pbkdf2-sha256$<iter>$<salt>$<hash>.
	// Treat a malformed hash as "no rehash" — verification will fail on its
	// own and we'd rather not overwrite something we don't understand.
	const wxString rest = storedHash.Mid(wxString(kPrefix).length());
	const int p1 = rest.Find('$');
	if (p1 == wxNOT_FOUND) return false;
	unsigned long iterations = 0;
	if (!rest.Left(p1).ToULong(&iterations)) return false;
	return iterations < kIterations;
}

wxString ibPasswordHash::Hash(const wxString& password)
{
	const wxScopedCharBuffer pwd_utf8 = password.utf8_str();

	uint8_t salt[kSaltLen];
	FillRandom(salt, kSaltLen);

	uint8_t derived[kKeyLen];
	Pbkdf2HmacSha256(reinterpret_cast<const uint8_t*>(pwd_utf8.data()), pwd_utf8.length(),
	                 salt, kSaltLen, kIterations, derived, kKeyLen);

	const wxString saltB64 = wxBase64Encode(salt, kSaltLen);
	const wxString hashB64 = wxBase64Encode(derived, kKeyLen);
	return wxString::Format("%s%u$%s$%s", kPrefix, kIterations, saltB64, hashB64);
}

bool ibPasswordHash::Verify(const wxString& password, const wxString& storedHash)
{
	if (storedHash.IsEmpty())
		return password.IsEmpty();

	// Legacy fast path: 32-hex MD5.
	if (IsLegacy(storedHash))
		return storedHash.IsSameAs(ibMD5::ComputeMd5(password), false);

	if (!storedHash.StartsWith(kPrefix))
		return false;

	// Parse $pbkdf2-sha256$<iter>$<saltB64>$<hashB64>
	const wxString rest = storedHash.Mid(wxString(kPrefix).length());
	const int p1 = rest.Find('$');
	if (p1 == wxNOT_FOUND) return false;
	const wxString iterStr = rest.Left(p1);
	const wxString afterIter = rest.Mid(p1 + 1);
	const int p2 = afterIter.Find('$');
	if (p2 == wxNOT_FOUND) return false;
	const wxString saltB64 = afterIter.Left(p2);
	const wxString hashB64 = afterIter.Mid(p2 + 1);

	unsigned long iterations = 0;
	if (!iterStr.ToULong(&iterations) || iterations == 0 || iterations > 10000000u)
		return false;

	const wxMemoryBuffer salt = wxBase64Decode(saltB64);
	const wxMemoryBuffer stored = wxBase64Decode(hashB64);
	if (salt.GetDataLen() == 0 || stored.GetDataLen() == 0)
		return false;

	const wxScopedCharBuffer pwd_utf8 = password.utf8_str();
	std::vector<uint8_t> derived(stored.GetDataLen());
	Pbkdf2HmacSha256(reinterpret_cast<const uint8_t*>(pwd_utf8.data()), pwd_utf8.length(),
	                 static_cast<const uint8_t*>(salt.GetData()), salt.GetDataLen(),
	                 uint32_t(iterations),
	                 derived.data(), derived.size());

	return ConstTimeEqual(derived.data(), static_cast<const uint8_t*>(stored.GetData()), derived.size());
}
