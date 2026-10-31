#ifndef __VALUE_DATA_HASHING_H__
#define __VALUE_DATA_HASHING_H__

#include "backend/compiler/value.h"
#include "backend/system/systemEnum.h"

#include "core/crypto/dataHash.h"

#include <memory>

// ⭐⭐ A DIGEST OF DATA, COMPUTED BY A SCRIPT: an object built for one hash function, fed with strings, bytes and
// files, asked for its sum.
//
//     var hashing = New DataHashing(HashFunction.SHA1);
//     hashing.Append(login + ":" + stamp);
//     var signature = String(hashing.HashSum);   // base64; HexString(hashing.HashSum) for hex
//
// Signing a request to a payment or exchange service, checking a webhook, a checksum of a file - each one is a
// digest of bytes, and until now a script could not compute one at all.
//
// ⭐ A KEY MAKES IT AN HMAC (RFC 2104) - `New DataHashing(HashFunction.SHA256, key)`: a webhook's signature is exactly
// that and nothing else.
//
// A STRING IS HASHED AS ITS UTF-8 BYTES. Another encoding is said by turning the string into bytes first -
// New BinaryData(text, TextEncoding.ANSI) - and appending those.
//
// HashSum is the digest of everything appended SO FAR, and appending goes on after it was read: the state is
// copied and the copy finished (core/crypto/dataHash.h).
void ibValueDataHashing_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueDataHashing : public ibValueStaticMembers<&ibValueDataHashing_BindNames> {
	enum Prop {
		enHashFunction,
		enHashSum,
	};
	enum Func {
		enAppend,
		enAppendFile,
	};
public:

	ibValueDataHashing();
	virtual ~ibValueDataHashing();

	virtual bool IsEmpty() const override { return m_hash == nullptr; }

	virtual bool Init() override;                                                     // refused: a hashing is built for a function
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;           // New DataHashing(function [, key])

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

	// The same verbs for a caller in C++ (and for the tests).
	void Start(ibHashFunction function);
	void Start(ibHashFunction function, const void* key, size_t keyLength);
	void Append(const ibValue& data);
	void AppendFile(const ibString& fileName);
	ibMemoryBuffer GetHashSum() const;

private:
	ibHashFunction              m_function = ibHashFunction();
	std::unique_ptr<ibDataHash> m_hash;
};

#endif // !__VALUE_DATA_HASHING_H__
