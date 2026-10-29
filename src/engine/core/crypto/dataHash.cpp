#include "dataHash.h"

#include "core/exception.h"                 // a digest Mbed TLS could not compute is refused, never handed over short
#include "core/localization/dictionary.h"   // _() — the refusal's words

#include <cstring>

#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>

namespace {

// The widest block of the functions offered: SHA-384 and SHA-512 read 128 bytes at a time, the rest 64.
constexpr size_t kMaxBlock = 128;

mbedtls_md_type_t TypeOf(ibDataHash::Function function)
{
	switch (function) {
	case ibDataHash::Function::MD5:    return MBEDTLS_MD_MD5;
	case ibDataHash::Function::SHA1:   return MBEDTLS_MD_SHA1;
	case ibDataHash::Function::SHA256: return MBEDTLS_MD_SHA256;
	case ibDataHash::Function::SHA384: return MBEDTLS_MD_SHA384;
	case ibDataHash::Function::SHA512: return MBEDTLS_MD_SHA512;
	}
	return MBEDTLS_MD_NONE;
}

// The HMAC pads the key to the function's BLOCK, which Mbed TLS keeps to itself.
size_t BlockOf(ibDataHash::Function function)
{
	return function == ibDataHash::Function::SHA384 || function == ibDataHash::Function::SHA512 ? 128 : 64;
}

[[noreturn]] void Refuse()
{
	ibCoreException::Error(_("The hash sum could not be computed"));
}

const mbedtls_md_info_t* InfoOf(ibDataHash::Function function)
{
	const mbedtls_md_info_t* info = mbedtls_md_info_from_type(TypeOf(function));
	if (info == nullptr)
		Refuse();
	return info;
}

// One Mbed TLS context, let go of whichever way the scope is left.
struct ibMdContext {
	ibMdContext() { mbedtls_md_init(&m_ctx); }
	~ibMdContext() { mbedtls_md_free(&m_ctx); }
	ibMdContext(const ibMdContext&) = delete;
	ibMdContext& operator=(const ibMdContext&) = delete;

	void Start(const mbedtls_md_info_t* info) {
		if (mbedtls_md_setup(&m_ctx, info, 0) != 0 || mbedtls_md_starts(&m_ctx) != 0)
			Refuse();
	}
	void Update(const void* data, size_t length) {
		if (length > 0 && mbedtls_md_update(&m_ctx, static_cast<const unsigned char*>(data), length) != 0)
			Refuse();
	}

	mbedtls_md_context_t m_ctx;
};

} // namespace

struct ibDataHash::Impl {
	explicit Impl(Function function) : m_function(function), m_info(InfoOf(function)) {}
	~Impl() { mbedtls_platform_zeroize(m_outerPad, sizeof(m_outerPad)); }

	Function                 m_function;
	const mbedtls_md_info_t* m_info;
	ibMdContext              m_inner;              // what is appended goes here - for an HMAC, after the inner pad
	bool                     m_keyed = false;
	unsigned char            m_outerPad[kMaxBlock] = {};   // the key XOR 0x5c, kept for the outer pass of an HMAC
};

ibDataHash::ibDataHash(Function function) : m_impl(std::make_unique<Impl>(function))
{
	m_impl->m_inner.Start(m_impl->m_info);
}

// RFC 2104: a key longer than the block is hashed first; the key, padded with zeros to the block, is XORed with
// 0x36 for the inner pass and with 0x5c for the outer one. The inner pad goes in now, the outer one waits for the
// sum. Mbed TLS has an HMAC of its own, but its state cannot be copied, and a sum asked midway needs a copy.
ibDataHash::ibDataHash(Function function, const void* key, size_t keyLength) : ibDataHash(function)
{
	const size_t block = BlockOf(function);

	unsigned char padded[kMaxBlock] = {};
	if (keyLength > block) {
		if (mbedtls_md(m_impl->m_info, static_cast<const unsigned char*>(key), keyLength, padded) != 0)
			Refuse();
	}
	else if (keyLength > 0) {
		std::memcpy(padded, key, keyLength);
	}

	unsigned char innerPad[kMaxBlock];
	for (size_t i = 0; i < block; ++i) {
		innerPad[i] = padded[i] ^ 0x36;
		m_impl->m_outerPad[i] = padded[i] ^ 0x5c;
	}
	m_impl->m_inner.Update(innerPad, block);
	m_impl->m_keyed = true;

	mbedtls_platform_zeroize(padded, sizeof(padded));
	mbedtls_platform_zeroize(innerPad, sizeof(innerPad));
}

ibDataHash::~ibDataHash() = default;

ibDataHash::Function ibDataHash::GetFunction() const
{
	return m_impl->m_function;
}

bool ibDataHash::IsKeyed() const
{
	return m_impl->m_keyed;
}

void ibDataHash::Append(const void* data, size_t length)
{
	m_impl->m_inner.Update(data, length);
}

ibMemoryBuffer ibDataHash::GetSum() const
{
	const size_t size = mbedtls_md_get_size(m_impl->m_info);
	unsigned char digest[MBEDTLS_MD_MAX_SIZE];

	// A copy is finished, never the state itself: appending goes on after the sum was read.
	ibMdContext copy;
	if (mbedtls_md_setup(&copy.m_ctx, m_impl->m_info, 0) != 0
		|| mbedtls_md_clone(&copy.m_ctx, &m_impl->m_inner.m_ctx) != 0
		|| mbedtls_md_finish(&copy.m_ctx, digest) != 0)
		Refuse();

	if (m_impl->m_keyed) {
		ibMdContext outer;
		outer.Start(m_impl->m_info);
		outer.Update(m_impl->m_outerPad, BlockOf(m_impl->m_function));
		outer.Update(digest, size);
		if (mbedtls_md_finish(&outer.m_ctx, digest) != 0)
			Refuse();
	}

	return ibMemoryBuffer(digest, size);
}

size_t ibDataHash::SumSize(Function function)
{
	return mbedtls_md_get_size(InfoOf(function));
}
