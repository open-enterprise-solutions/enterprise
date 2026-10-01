// ibFieldCipher — the cipher floor (docs/private/data-protection-arc.md § 3b), tested on its own as the
// design asks: a value opens with its key and its field, and with nothing else.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "backend/cipher/fieldCipher.h"

namespace {

std::vector<unsigned char> Bytes(const std::string& text)
{
	return std::vector<unsigned char>(text.begin(), text.end());
}

ibCipherContext Context(const std::string& field)
{
	ibCipherContext context;
	context.m_key   = ibFieldCipher::Random(ibFieldCipher::kKeySize);
	context.m_field = field;
	return context;
}

} // namespace

TEST(FieldCipher, AValueOpensWithItsKeyAndField)
{
	const ibCipherContext context = Context("trade/Password");
	ASSERT_EQ(context.m_key.size(), ibFieldCipher::kKeySize);

	const std::vector<unsigned char> sealed = ibFieldCipher::Encrypt(Bytes("s3cret"), context);
	ASSERT_FALSE(sealed.empty());
	EXPECT_EQ(sealed.size(), 12u + 6u + 16u) << "nonce | ciphertext | tag";

	std::vector<unsigned char> plain;
	ASSERT_TRUE(ibFieldCipher::Decrypt(sealed, context, plain));
	EXPECT_EQ(plain, Bytes("s3cret"));
}

TEST(FieldCipher, TheSameValueSealsDifferentlyEveryTime)
{
	const ibCipherContext context = Context("trade/Password");
	EXPECT_NE(ibFieldCipher::Encrypt(Bytes("s3cret"), context), ibFieldCipher::Encrypt(Bytes("s3cret"), context))
		<< "a fresh nonce every time — two equal passwords must not look equal in the file";
}

TEST(FieldCipher, AnotherFieldDoesNotOpenIt)
{
	ibCipherContext context = Context("trade/Password");
	const std::vector<unsigned char> sealed = ibFieldCipher::Encrypt(Bytes("s3cret"), context);

	context.m_field = "ledger/Password";
	std::vector<unsigned char> plain;
	EXPECT_FALSE(ibFieldCipher::Decrypt(sealed, context, plain)) << "a value moved to another field must not open";
	EXPECT_TRUE(plain.empty());
}

TEST(FieldCipher, AnotherKeyDoesNotOpenIt)
{
	const ibCipherContext mine = Context("trade/Password");
	const ibCipherContext other = Context("trade/Password");
	const std::vector<unsigned char> sealed = ibFieldCipher::Encrypt(Bytes("s3cret"), mine);

	std::vector<unsigned char> plain;
	EXPECT_FALSE(ibFieldCipher::Decrypt(sealed, other, plain)) << "a copy taken to another installation must not open";
}

TEST(FieldCipher, ATamperedValueDoesNotOpen)
{
	const ibCipherContext context = Context("trade/Password");
	std::vector<unsigned char> sealed = ibFieldCipher::Encrypt(Bytes("s3cret"), context);
	sealed[14] ^= 0x01;

	std::vector<unsigned char> plain;
	EXPECT_FALSE(ibFieldCipher::Decrypt(sealed, context, plain));
}

TEST(FieldCipher, NotAKeyIsRefused)
{
	ibCipherContext context;
	context.m_key = Bytes("short");
	EXPECT_TRUE(ibFieldCipher::Encrypt(Bytes("s3cret"), context).empty());
}
