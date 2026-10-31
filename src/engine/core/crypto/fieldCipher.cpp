#include "fieldCipher.h"

#include <algorithm>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>
#include <mbedtls/platform_util.h>

namespace {

constexpr std::size_t kNonceSize = 12;
constexpr std::size_t kTagSize   = 16;

const unsigned char* FieldBytes(const ibCipherContext& context)
{
	return reinterpret_cast<const unsigned char*>(context.m_field.data());
}

} // namespace

std::vector<unsigned char> ibFieldCipher::Random(std::size_t count)
{
	std::vector<unsigned char> out(count);

	mbedtls_entropy_context  entropy;
	mbedtls_ctr_drbg_context drbg;
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);

	static const unsigned char personal[] = "oes.fieldCipher";
	int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, personal, sizeof(personal) - 1);
	if (rc == 0 && count > 0)
		rc = mbedtls_ctr_drbg_random(&drbg, out.data(), out.size());

	mbedtls_ctr_drbg_free(&drbg);
	mbedtls_entropy_free(&entropy);

	if (rc != 0)
		out.clear();
	return out;
}

std::vector<unsigned char> ibFieldCipher::Encrypt(const std::vector<unsigned char>& plain, const ibCipherContext& context)
{
	if (context.m_key.size() != kKeySize)
		return {};

	const std::vector<unsigned char> nonce = Random(kNonceSize);
	if (nonce.size() != kNonceSize)
		return {};

	std::vector<unsigned char> sealed(kNonceSize + plain.size() + kTagSize);
	std::copy(nonce.begin(), nonce.end(), sealed.begin());

	mbedtls_gcm_context gcm;
	mbedtls_gcm_init(&gcm);
	int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, context.m_key.data(), static_cast<unsigned int>(kKeySize * 8));
	if (rc == 0)
		rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain.size(),
			nonce.data(), kNonceSize,
			FieldBytes(context), context.m_field.size(),
			plain.data(), sealed.data() + kNonceSize,
			kTagSize, sealed.data() + kNonceSize + plain.size());
	mbedtls_gcm_free(&gcm);

	if (rc != 0)
		return {};
	return sealed;
}

bool ibFieldCipher::Decrypt(const std::vector<unsigned char>& sealed, const ibCipherContext& context,
	std::vector<unsigned char>& plain)
{
	plain.clear();
	if (context.m_key.size() != kKeySize || sealed.size() < kNonceSize + kTagSize)
		return false;

	const std::size_t length = sealed.size() - kNonceSize - kTagSize;
	std::vector<unsigned char> opened(length);

	mbedtls_gcm_context gcm;
	mbedtls_gcm_init(&gcm);
	int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, context.m_key.data(), static_cast<unsigned int>(kKeySize * 8));
	if (rc == 0)
		rc = mbedtls_gcm_auth_decrypt(&gcm, length,
			sealed.data(), kNonceSize,
			FieldBytes(context), context.m_field.size(),
			sealed.data() + kNonceSize + length, kTagSize,
			sealed.data() + kNonceSize, opened.data());
	mbedtls_gcm_free(&gcm);

	if (rc != 0) {
		// A value that did not open leaves nothing of itself behind.
		if (!opened.empty())
			mbedtls_platform_zeroize(opened.data(), opened.size());
		return false;
	}
	plain.swap(opened);
	return true;
}
