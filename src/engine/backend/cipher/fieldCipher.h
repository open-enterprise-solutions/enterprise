#ifndef __FIELD_CIPHER_H__
#define __FIELD_CIPHER_H__

// ibFieldCipher — THE CIPHER FLOOR (docs/private/data-protection-arc.md § 3b): bytes in, bytes out, and a
// context saying which key and which field. No wx, no driver, no metadata — everything above decides, this
// executes, which is what lets it be tested on its own.
//
// AES-256-GCM through Mbed TLS (in the build since 2026-09-26). A sealed value is
//
//     nonce (12) | ciphertext | tag (16)
//
// with a fresh random nonce every time, and the FIELD bound into the tag as associated data: a sealed value
// moved to another field does not open there.
//
// Its first tenant is the application server's config — the passwords of the bases it serves (multi-base-process.md
// § 5.4); sys_config and the fields of a configuration come after, on the same floor.

#include "backend/backend.h"

#include <cstddef>
#include <string>
#include <vector>

struct ibCipherContext {
	std::vector<unsigned char> m_key;     // 32 bytes — AES-256
	std::string                m_field;   // bound into the tag
};

class BACKEND_API ibFieldCipher {
public:
	static constexpr std::size_t kKeySize = 32;

	// The sealed value; empty when the key is not a key or the platform gave no randomness.
	static std::vector<unsigned char> Encrypt(const std::vector<unsigned char>& plain, const ibCipherContext& context);

	// False when the value does not open: another key, another field, or a value tampered with — the tag
	// cannot tell them apart, and neither does this.
	static bool Decrypt(const std::vector<unsigned char>& sealed, const ibCipherContext& context,
		std::vector<unsigned char>& plain);

	// Cryptographically random bytes — a new key, a nonce. Empty when the platform gave none.
	static std::vector<unsigned char> Random(std::size_t count);
};

#endif
