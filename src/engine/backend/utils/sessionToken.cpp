#include "sessionToken.h"

#include <cstdint>
#include <cstring>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>

#include "sha256.hpp"

namespace ibSessionToken {

std::vector<unsigned char> Generate()
{
	std::vector<unsigned char> out(kBytes);

	mbedtls_entropy_context  entropy;
	mbedtls_ctr_drbg_context drbg;
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);

	// Its own personalization — not the field cipher's, so the two draws are not one stream.
	static const unsigned char personal[] = "oes.sessionToken";
	int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, personal, sizeof(personal) - 1);
	if (rc == 0)
		rc = mbedtls_ctr_drbg_random(&drbg, out.data(), out.size());

	mbedtls_ctr_drbg_free(&drbg);
	mbedtls_entropy_free(&entropy);

	if (rc != 0)
		out.clear();
	return out;
}

wxString Hex(const unsigned char* bytes, std::size_t length)
{
	static const char kDigits[] = "0123456789abcdef";
	if (bytes == nullptr)
		return wxString();
	wxString hex;
	hex.reserve(length * 2);
	for (std::size_t i = 0; i < length; ++i) {
		hex += wxChar(kDigits[bytes[i] >> 4]);
		hex += wxChar(kDigits[bytes[i] & 0x0F]);
	}
	return hex;
}

bool Hash(const unsigned char* bytes, std::size_t length, unsigned char out[kDigest])
{
	if (bytes == nullptr || out == nullptr)
		return false;
	ibSHA256::Hash(bytes, length, out);
	return true;
}

wxString HashHex(const unsigned char* bytes, std::size_t length)
{
	unsigned char digest[kDigest];
	if (!Hash(bytes, length, digest))
		return wxString();
	return Hex(digest, kDigest);
}

bool ParseHex(const wxString& hex, unsigned char out[kBytes])
{
	if (out == nullptr || hex.length() != kBytes * 2)
		return false;
	auto nibble = [](wxChar c) -> int {
		if (c >= wxT('0') && c <= wxT('9'))
			return c - wxT('0');
		if (c >= wxT('a') && c <= wxT('f'))
			return c - wxT('a') + 10;
		if (c >= wxT('A') && c <= wxT('F'))
			return c - wxT('A') + 10;
		return -1;
	};
	for (std::size_t i = 0; i < kBytes; ++i) {
		const int hi = nibble(hex[i * 2]);
		const int lo = nibble(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = static_cast<unsigned char>((hi << 4) | lo);
	}
	return true;
}

bool DigestEqual(const unsigned char* left, const unsigned char* right)
{
	if (left == nullptr || right == nullptr)
		return false;
	return mbedtls_ct_memcmp(left, right, kDigest) == 0;
}

} // namespace ibSessionToken
