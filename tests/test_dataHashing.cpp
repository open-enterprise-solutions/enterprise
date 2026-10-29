// =============================================================================
// DataHashing, HashFunction and the six conversions of bytes (GetBinaryDataFromString, GetStringFromBinaryData,
// the base64 pair, the hex pair) - pinned to the published vectors: FIPS 180 for the digests, RFC 1321 for MD5,
// RFC 2202 and RFC 4231 for the HMACs. The corpus suite, tests/scripts/test_binary_data_suite.txt, checks the
// language side: New, the enumeration, the property, the global functions by their names.
// =============================================================================

#include <gtest/gtest.h>

#include <string>

#include <wx/ffile.h>
#include <wx/filename.h>

#include "backend/backend_exception.h"
#include "backend/system/systemManager.h"
#include "backend/system/value/valueBinaryData.h"
#include "backend/system/value/valueDataHashing.h"

#include "core/crypto/dataHash.h"

namespace {

std::string Hex(const ibMemoryBuffer& bytes)
{
	static const char digits[] = "0123456789abcdef";
	const unsigned char* at = static_cast<const unsigned char*>(bytes.GetData());
	std::string text;
	for (size_t i = 0; i < bytes.GetDataLen(); ++i) {
		text += digits[at[i] >> 4];
		text += digits[at[i] & 0x0F];
	}
	return text;
}

std::string Digest(ibDataHash::Function function, const std::string& data)
{
	ibDataHash hash(function);
	hash.Append(data.data(), data.size());
	return Hex(hash.GetSum());
}

std::string Hmac(ibDataHash::Function function, const std::string& key, const std::string& data)
{
	ibDataHash hash(function, key.data(), key.size());
	hash.Append(data.data(), data.size());
	return Hex(hash.GetSum());
}

// The bytes a BinaryData value holds - or nothing, when the value is not one.
std::string BytesOf(const ibValue& value)
{
	const ibValueBinaryData* binary = dynamic_cast<const ibValueBinaryData*>(value.GetRef());
	if (binary == nullptr)
		return "<not BinaryData>";
	return std::string(static_cast<const char*>(binary->GetBuffer().GetData()), binary->GetLength());
}

ibValue Binary(const std::string& bytes)
{
	const ibValuePtr<ibValueBinaryData> made(new ibValueBinaryData(bytes.data(), bytes.size()));
	return made;
}

} // namespace

// --- the digests: FIPS 180 "abc", RFC 1321 for MD5 ---------------------------

TEST(DataHash, KnownVectors_Abc) {
	EXPECT_EQ(Digest(ibDataHash::Function::MD5, "abc"), "900150983cd24fb0d6963f7d28e17f72");
	EXPECT_EQ(Digest(ibDataHash::Function::SHA1, "abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
	EXPECT_EQ(Digest(ibDataHash::Function::SHA256, "abc"),
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	EXPECT_EQ(Digest(ibDataHash::Function::SHA384, "abc"),
		"cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
	EXPECT_EQ(Digest(ibDataHash::Function::SHA512, "abc"),
		"ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd"
		"454d4423643ce80e2a9ac94fa54ca49f");
}

TEST(DataHash, NothingAppendedIsTheDigestOfNoBytes) {
	EXPECT_EQ(Digest(ibDataHash::Function::MD5, ""), "d41d8cd98f00b204e9800998ecf8427e");
	EXPECT_EQ(Digest(ibDataHash::Function::SHA1, ""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST(DataHash, PiecesAreTheWhole) {
	ibDataHash hash(ibDataHash::Function::SHA256);
	hash.Append("a", 1);
	hash.Append(nullptr, 0);
	hash.Append("bc", 2);
	EXPECT_EQ(Hex(hash.GetSum()), Digest(ibDataHash::Function::SHA256, "abc"));
}

// The sum is read from a COPY of the state: asked twice it agrees, and appending after it goes on from where it was.
TEST(DataHash, TheSumCanBeAskedMidwayAndAppendingGoesOn) {
	ibDataHash hash(ibDataHash::Function::SHA1);
	hash.Append("ab", 2);
	EXPECT_EQ(Hex(hash.GetSum()), Digest(ibDataHash::Function::SHA1, "ab"));
	EXPECT_EQ(Hex(hash.GetSum()), Digest(ibDataHash::Function::SHA1, "ab"));
	hash.Append("c", 1);
	EXPECT_EQ(Hex(hash.GetSum()), Digest(ibDataHash::Function::SHA1, "abc"));
}

TEST(DataHash, SumSizes) {
	EXPECT_EQ(ibDataHash::SumSize(ibDataHash::Function::MD5), 16u);
	EXPECT_EQ(ibDataHash::SumSize(ibDataHash::Function::SHA1), 20u);
	EXPECT_EQ(ibDataHash::SumSize(ibDataHash::Function::SHA256), 32u);
	EXPECT_EQ(ibDataHash::SumSize(ibDataHash::Function::SHA384), 48u);
	EXPECT_EQ(ibDataHash::SumSize(ibDataHash::Function::SHA512), 64u);
}

// --- the HMACs: RFC 2202 (MD5, SHA-1), RFC 4231 (SHA-2) ----------------------

TEST(DataHash, Hmac_Rfc2202) {
	EXPECT_EQ(Hmac(ibDataHash::Function::MD5, std::string(16, '\x0b'), "Hi There"), "9294727a3638bb1c13f48ef8158bfc9d");
	EXPECT_EQ(Hmac(ibDataHash::Function::MD5, "Jefe", "what do ya want for nothing?"), "750c783e6ab0b503eaa86e310a5db738");
	EXPECT_EQ(Hmac(ibDataHash::Function::SHA1, std::string(20, '\x0b'), "Hi There"),
		"b617318655057264e28bc0b6fb378c8ef146be00");
	EXPECT_EQ(Hmac(ibDataHash::Function::SHA1, "Jefe", "what do ya want for nothing?"),
		"effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
}

TEST(DataHash, Hmac_Rfc4231) {
	EXPECT_EQ(Hmac(ibDataHash::Function::SHA256, "Jefe", "what do ya want for nothing?"),
		"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
	EXPECT_EQ(Hmac(ibDataHash::Function::SHA384, "Jefe", "what do ya want for nothing?"),
		"af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e8e2240ca5e69e2c78b3239ecfab21649");
	EXPECT_EQ(Hmac(ibDataHash::Function::SHA512, "Jefe", "what do ya want for nothing?"),
		"164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fd"
		"caeab1a34d4a6b4b636e070a38bce737");
}

// A key longer than the block is hashed first (RFC 4231 test case 6: 131 bytes of 0xaa).
TEST(DataHash, Hmac_AKeyLongerThanTheBlock) {
	EXPECT_EQ(Hmac(ibDataHash::Function::SHA256, std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First"),
		"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
	EXPECT_EQ(Hmac(ibDataHash::Function::SHA512, std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First"),
		"80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e52"
		"95e64f73f63f0aec8b915a985d786598");
}

// An empty key is a key: the HMAC with it is a value of its own, not the plain digest.
TEST(DataHash, Hmac_AnEmptyKeyIsStillAKey) {
	ibDataHash keyed(ibDataHash::Function::SHA256, "", 0);
	EXPECT_TRUE(keyed.IsKeyed());
	EXPECT_EQ(Hex(keyed.GetSum()), "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad");
	EXPECT_NE(Hex(keyed.GetSum()), Digest(ibDataHash::Function::SHA256, ""));
}

TEST(DataHash, Hmac_TheSumCanBeAskedMidway) {
	ibDataHash hash(ibDataHash::Function::SHA256, "Jefe", 4);
	hash.Append("what do ya want ", 16);
	hash.GetSum();
	hash.Append("for nothing?", 12);
	EXPECT_EQ(Hex(hash.GetSum()), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// --- DataHashing, the script's object -----------------------------------------

TEST(DataHashing, AStringIsHashedAsItsUtf8Bytes) {
	ibValueDataHashing hashing;
	hashing.Start(ibHashFunction_MD5);
	hashing.Append(ibValue(ibString(wxT("абв"))));
	EXPECT_EQ(Hex(hashing.GetHashSum()), Digest(ibDataHash::Function::MD5, "\xD0\xB0\xD0\xB1\xD0\xB2"));
}

TEST(DataHashing, BinaryDataIsHashedAsItsBytes) {
	ibValueDataHashing hashing;
	hashing.Start(ibHashFunction_SHA1);
	hashing.Append(Binary(std::string("a\0c", 3)));
	EXPECT_EQ(Hex(hashing.GetHashSum()), Digest(ibDataHash::Function::SHA1, std::string("a\0c", 3)));
}

TEST(DataHashing, AFileIsHashedWhole) {
	const ibString path = wxFileName::CreateTempFileName(wxT("oes"));
	{
		wxFFile f(path, wxT("wb"));
		ASSERT_TRUE(f.IsOpened());
		f.Write("abc", 3);
	}
	ibValueDataHashing hashing;
	hashing.Start(ibHashFunction_SHA256);
	hashing.AppendFile(path);
	wxRemoveFile(path);
	EXPECT_EQ(Hex(hashing.GetHashSum()), Digest(ibDataHash::Function::SHA256, "abc"));
}

TEST(DataHashing, ANumberIsNotData) {
	ibValueDataHashing hashing;
	hashing.Start(ibHashFunction_SHA256);
	EXPECT_THROW(hashing.Append(ibValue(ibNumber(12))), ibBackendException);
}

TEST(DataHashing, AFunctionThatIsNoMemberIsRefused) {
	ibValueDataHashing hashing;
	EXPECT_THROW(hashing.Start(ibHashFunction()), ibBackendException);
	EXPECT_THROW(hashing.Init(), ibBackendException) << "a hashing is built for a function";
}

TEST(DataHashing, NoFunctionNoSum) {
	ibValueDataHashing hashing;
	EXPECT_THROW(hashing.GetHashSum(), ibBackendException);
}

// --- the six conversions -------------------------------------------------------

TEST(BinaryDataConversions, StringToBytesAndBack) {
	const ibValue bytes = ibValueSystemFunction::GetBinaryDataFromString(wxT("Борщ"));
	EXPECT_EQ(BytesOf(bytes), "\xD0\x91\xD0\xBE\xD1\x80\xD1\x89");
	EXPECT_EQ(ibValueSystemFunction::GetStringFromBinaryData(bytes), ibString(wxT("Борщ")));
}

TEST(BinaryDataConversions, Utf16IsLittleEndian_AndTheMarkIsAskedFor) {
	EXPECT_EQ(BytesOf(ibValueSystemFunction::GetBinaryDataFromString(wxT("ab"), ibTextEncoding_UTF16)),
		std::string("a\0b\0", 4));
	EXPECT_EQ(BytesOf(ibValueSystemFunction::GetBinaryDataFromString(wxT("ab"), ibTextEncoding_UTF16, true)),
		std::string("\xFF\xFE" "a\0b\0", 6));
	EXPECT_EQ(BytesOf(ibValueSystemFunction::GetBinaryDataFromString(wxT("ab"), ibTextEncoding_UTF8, true)),
		"\xEF\xBB\xBF" "ab");
}

TEST(BinaryDataConversions, AMarkIsReadAsAMark) {
	EXPECT_EQ(ibValueSystemFunction::GetStringFromBinaryData(Binary("\xEF\xBB\xBF" "ab")), ibString(wxT("ab")));
	EXPECT_EQ(ibValueSystemFunction::GetStringFromBinaryData(Binary(std::string("\xFF\xFE" "a\0b\0", 6)), ibTextEncoding_UTF16),
		ibString(wxT("ab")));
}

TEST(BinaryDataConversions, BytesThatAreNotTextAreRefused) {
	EXPECT_THROW(ibValueSystemFunction::GetStringFromBinaryData(Binary("\xFF\xFE\xFD")), ibBackendException);
	EXPECT_THROW(ibValueSystemFunction::GetStringFromBinaryData(ibValue(ibString(wxT("text")))), ibBackendException)
		<< "a string is not BinaryData";
}

TEST(BinaryDataConversions, Base64BothWays) {
	EXPECT_EQ(ibValueSystemFunction::GetBase64StringFromBinaryData(Binary("Man")), ibString(wxT("TWFu")));
	EXPECT_EQ(ibValueSystemFunction::GetBase64StringFromBinaryData(Binary("Ma")), ibString(wxT("TWE=")));
	EXPECT_EQ(ibValueSystemFunction::GetBase64StringFromBinaryData(Binary("")), ibString());
	EXPECT_EQ(BytesOf(ibValueSystemFunction::GetBinaryDataFromBase64String(wxT("TWE="))), "Ma");
}

// Another system wraps base64 at 76 characters; the line breaks are not part of it.
TEST(BinaryDataConversions, Base64LineBreaksAreSkipped) {
	EXPECT_EQ(BytesOf(ibValueSystemFunction::GetBinaryDataFromBase64String(wxT("TW\r\nFu TWE=\n"))), "ManMa");
}

TEST(BinaryDataConversions, TextThatIsNotBase64IsRefused) {
	EXPECT_THROW(ibValueSystemFunction::GetBinaryDataFromBase64String(wxT("TWF")), ibBackendException);
	EXPECT_THROW(ibValueSystemFunction::GetBinaryDataFromBase64String(wxT("TW*u")), ibBackendException);
}

TEST(BinaryDataConversions, HexBothWays) {
	EXPECT_EQ(ibValueSystemFunction::GetHexStringFromBinaryData(Binary(std::string("\x00\x0A\xFF", 3))), ibString(wxT("000AFF")));
	EXPECT_EQ(BytesOf(ibValueSystemFunction::GetBinaryDataFromHexString(wxT("00 0a Ff"))), std::string("\x00\x0A\xFF", 3));
	EXPECT_EQ(BytesOf(ibValueSystemFunction::GetBinaryDataFromHexString(wxT(""))), "");
}

TEST(BinaryDataConversions, TextThatIsNotHexIsRefused) {
	EXPECT_THROW(ibValueSystemFunction::GetBinaryDataFromHexString(wxT("ABC")), ibBackendException);
	EXPECT_THROW(ibValueSystemFunction::GetBinaryDataFromHexString(wxT("0G")), ibBackendException);
}
