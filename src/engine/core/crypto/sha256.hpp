/////////////////////////////////////////////////////////////////////////////
// Name:        sha256.hpp
// Purpose:     SHA-256 (FIPS 180-4) — used by password hashing (PBKDF2).
//              Do not use for metadata integrity hashes — ibMD5 stays for that.
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_SHA256_H_
#define _IB_SHA256_H_

#include "core/core.h"
#include <cstdint>

class CORE_API ibSHA256 {
public:
	static constexpr size_t DIGEST_SIZE = 32;
	static constexpr size_t BLOCK_SIZE  = 64;

	ibSHA256();
	void Update(const uint8_t* data, size_t len);
	void Final(uint8_t out[DIGEST_SIZE]);

	// one-shot
	static void Hash(const uint8_t* data, size_t len, uint8_t out[DIGEST_SIZE]);

private:
	void Transform(const uint8_t* block);

	uint64_t m_bitLen;
	uint32_t m_state[8];
	uint8_t  m_buffer[BLOCK_SIZE];
	size_t   m_bufLen;
};

#endif // _IB_SHA256_H_
