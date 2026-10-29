#ifndef __DATA_HASH_H__
#define __DATA_HASH_H__

#include "core/core.h"

#include "core/memory/memoryBuffer.h"

#include <cstddef>
#include <memory>

// ⭐ THE DIGEST OF BYTES FED IN PIECES - what a script's DataHashing is made of (backend/system/value/
// valueDataHashing.h). Mbed TLS computes it, and the core is the one module that links Mbed TLS, so its context
// stays behind this facade: nothing above the core includes an mbedtls header.
//
// KEYED, IT IS AN HMAC (RFC 2104) - the same digest with the key folded in at both ends. A key given empty is
// still a key: an HMAC with an empty key is a value of its own, not the plain digest.
//
// THE SUM CAN BE ASKED AT ANY MOMENT, AND FEEDING GOES ON AFTER IT. The state is copied and the copy finished;
// the state itself is never finished in place, so two reads of the sum with nothing appended between them agree.
class CORE_API ibDataHash {
public:
	enum class Function {
		MD5,
		SHA1,
		SHA256,
		SHA384,
		SHA512,
	};

	explicit ibDataHash(Function function);
	// …keyed: an HMAC over the same function.
	ibDataHash(Function function, const void* key, size_t keyLength);
	~ibDataHash();

	ibDataHash(const ibDataHash&) = delete;
	ibDataHash& operator=(const ibDataHash&) = delete;

	Function GetFunction() const;
	bool IsKeyed() const;

	void Append(const void* data, size_t length);

	// The digest of everything appended so far - 16 bytes for MD5, 20 for SHA-1, 32, 48 and 64 for the SHA-2s.
	ibMemoryBuffer GetSum() const;

	static size_t SumSize(Function function);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

#endif // !__DATA_HASH_H__
