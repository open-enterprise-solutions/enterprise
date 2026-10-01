// =============================================================================
// OES Enterprise — Firebird BLOB compression round-trip tests
//
// Validates the zstd/zlib-wrapped BLOB envelope written by
// `ibFirebirdBlobCompression::Wrap` and read back by `Unwrap`. The
// envelope carries an `OESC` magic prefix + version + flag byte so
// the read path can distinguish:
//   - new wrapped raw     (flag=0x00)
//   - new wrapped zlib    (flag=0x01)
//   - legacy uncompressed (no magic — read straight through)
//
// Compression failures here would silently corrupt every document
// description / attachment / TEXT BLOB written by the new driver.
// Round-trip tests are cheap and catch the entire failure surface.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/databaseLayer/firebird/firebirdBlobCompression.h"

#include <wx/buffer.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

// Convenience — compare two byte buffers and return true on identical
// contents. Lengths must match.
bool BuffersEqual(const wxMemoryBuffer& a, const void* b, size_t bLen) {
	if (a.GetDataLen() != bLen) return false;
	if (bLen == 0) return true;
	return std::memcmp(a.GetData(), b, bLen) == 0;
}

// Build a buffer of N bytes filled with the given value. Used to
// generate compressible payloads (all-same bytes deflates near-100%).
std::vector<uint8_t> MakeRepeated(size_t n, uint8_t value) {
	return std::vector<uint8_t>(n, value);
}

// Build a buffer of N bytes filled with deterministic
// pseudo-random data. Used as an incompressible payload (zlib won't
// shrink it; should land in the wrapped-raw path).
std::vector<uint8_t> MakeRandom(size_t n, uint32_t seed) {
	std::mt19937 rng(seed);
	std::vector<uint8_t> out(n);
	for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(rng() & 0xFF);
	return out;
}

} // namespace

// ===========================================================================
// Round-trip — wrap then unwrap returns identical bytes
// ===========================================================================

TEST(BlobCompression, RoundTrip_Empty) {
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(nullptr, 0);
	wxMemoryBuffer back    = ibFirebirdBlobCompression::Unwrap(
		wrapped.GetData(), wrapped.GetDataLen());
	EXPECT_EQ(back.GetDataLen(), 0u);
}

TEST(BlobCompression, RoundTrip_TinyBelowThreshold) {
	// 100 bytes — below the 512-byte compression threshold; should
	// store wrapped-raw (flag=0x00).
	auto src = MakeRepeated(100, 'A');
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());

	// Header is the magic + version + flag = kHeaderLen bytes.
	ASSERT_GE(wrapped.GetDataLen(), ibFirebirdBlobCompression::kHeaderLen);
	const uint8_t* p = static_cast<const uint8_t*>(wrapped.GetData());
	EXPECT_EQ(p[0], ibFirebirdBlobCompression::kMagic0);
	EXPECT_EQ(p[1], ibFirebirdBlobCompression::kMagic1);
	EXPECT_EQ(p[2], ibFirebirdBlobCompression::kMagic2);
	EXPECT_EQ(p[3], ibFirebirdBlobCompression::kMagic3);
	EXPECT_EQ(p[4], ibFirebirdBlobCompression::kVersion);
	EXPECT_EQ(p[5], ibFirebirdBlobCompression::kFlagRaw);

	// Body should be identical to source.
	EXPECT_EQ(wrapped.GetDataLen(),
	          ibFirebirdBlobCompression::kHeaderLen + src.size());

	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(
		wrapped.GetData(), wrapped.GetDataLen());
	EXPECT_TRUE(BuffersEqual(back, src.data(), src.size()));
}

TEST(BlobCompression, RoundTrip_HighlyCompressible) {
	// 16 KB of the same byte. zlib should shrink this to <100 bytes
	// — well below the 0.85 ratio threshold, so it lands in the
	// zlib-wrapped path (flag=0x01).
	auto src = MakeRepeated(16384, 'X');
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());

	ASSERT_GE(wrapped.GetDataLen(), ibFirebirdBlobCompression::kHeaderLen);
	const uint8_t* p = static_cast<const uint8_t*>(wrapped.GetData());
	EXPECT_EQ(p[5], ibFirebirdBlobCompression::kFlagZlib);

	// Wrapped should be much smaller than the source.
	EXPECT_LT(wrapped.GetDataLen(), src.size() / 4);

	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(
		wrapped.GetData(), wrapped.GetDataLen());
	EXPECT_TRUE(BuffersEqual(back, src.data(), src.size()));
}

TEST(BlobCompression, RoundTrip_Incompressible) {
	// 4 KB of pseudo-random bytes — zlib can't shrink random data
	// below the 0.85 ratio. Should fall back to wrapped-raw.
	auto src = MakeRandom(4096, /*seed=*/42);
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());

	const uint8_t* p = static_cast<const uint8_t*>(wrapped.GetData());
	EXPECT_EQ(p[5], ibFirebirdBlobCompression::kFlagRaw);

	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(
		wrapped.GetData(), wrapped.GetDataLen());
	EXPECT_TRUE(BuffersEqual(back, src.data(), src.size()));
}

TEST(BlobCompression, RoundTrip_LargeText) {
	// 64 KB of repeating "ABCD" — typical document description /
	// comment pattern. Should compress well (>5×) and round-trip.
	std::vector<uint8_t> src;
	src.reserve(64 * 1024);
	for (int i = 0; i < 16384; ++i) {
		src.push_back('A'); src.push_back('B');
		src.push_back('C'); src.push_back('D');
	}
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());

	const uint8_t* p = static_cast<const uint8_t*>(wrapped.GetData());
	EXPECT_EQ(p[5], ibFirebirdBlobCompression::kFlagZlib);
	EXPECT_LT(wrapped.GetDataLen(), src.size() / 3);

	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(
		wrapped.GetData(), wrapped.GetDataLen());
	EXPECT_TRUE(BuffersEqual(back, src.data(), src.size()));
}

TEST(BlobCompression, RoundTrip_ExactBoundary) {
	// Exactly 512 bytes (the compression threshold). Implementation
	// uses >= so this enters the compress path; with all-same bytes
	// it should land in zlib.
	auto src = MakeRepeated(512, 'Z');
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());

	const uint8_t* p = static_cast<const uint8_t*>(wrapped.GetData());
	EXPECT_EQ(p[5], ibFirebirdBlobCompression::kFlagZlib);

	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(
		wrapped.GetData(), wrapped.GetDataLen());
	EXPECT_TRUE(BuffersEqual(back, src.data(), src.size()));
}

TEST(BlobCompression, RoundTrip_OneUnderBoundary) {
	// 511 bytes — one under threshold; must land in wrapped-raw.
	auto src = MakeRepeated(511, 'Z');
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());

	const uint8_t* p = static_cast<const uint8_t*>(wrapped.GetData());
	EXPECT_EQ(p[5], ibFirebirdBlobCompression::kFlagRaw);

	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(
		wrapped.GetData(), wrapped.GetDataLen());
	EXPECT_TRUE(BuffersEqual(back, src.data(), src.size()));
}

// ===========================================================================
// Legacy back-compat — payloads without magic must pass through
// ===========================================================================

TEST(BlobCompression, Legacy_NoMagic_PassesThrough) {
	// Simulates a BLOB written before the compression feature landed:
	// no OESC magic, just raw bytes. Unwrap must return them
	// untouched (otherwise existing databases break on first read).
	const char src[] = "Plain old uncompressed BLOB data — no magic header";
	const size_t srcLen = sizeof(src) - 1;

	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(src, srcLen);
	EXPECT_TRUE(BuffersEqual(back, src, srcLen));
}

TEST(BlobCompression, Legacy_BytesThatLookLikeMagic_ButNotVersion) {
	// Adversarial: payload starts with `OESC` but the version byte
	// isn't 1. HasMagic returns false → treated as legacy raw.
	const uint8_t src[] = {
		'O', 'E', 'S', 'C',    // matches magic prefix
		0xFF,                  // bogus version (not 1)
		0x01,                  // bogus flag
		'h', 'e', 'l', 'l', 'o'
	};
	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(src, sizeof(src));
	EXPECT_TRUE(BuffersEqual(back, src, sizeof(src)));
}

TEST(BlobCompression, Legacy_BytesThatLookLikeMagic_ButBadFlag) {
	// Adversarial: version=1 but flag is neither 0 nor 1 — treated
	// as legacy raw so we don't try to decompress garbage.
	const uint8_t src[] = {
		'O', 'E', 'S', 'C',
		0x01,                  // valid version
		0xAB,                  // invalid flag
		'd', 'a', 't', 'a'
	};
	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(src, sizeof(src));
	EXPECT_TRUE(BuffersEqual(back, src, sizeof(src)));
}

TEST(BlobCompression, Legacy_TooShortForMagic) {
	// 5 bytes — shorter than the 6-byte header. HasMagic returns
	// false; payload returned untouched.
	const uint8_t src[] = { 'O', 'E', 'S', 'C', 1 };
	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(src, sizeof(src));
	EXPECT_TRUE(BuffersEqual(back, src, sizeof(src)));
}

TEST(BlobCompression, Legacy_EmptyInput) {
	// Zero-length input — should return an empty buffer, not crash.
	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(nullptr, 0);
	EXPECT_EQ(back.GetDataLen(), 0u);
}

// ===========================================================================
// HasMagic probe
// ===========================================================================

TEST(BlobCompression, HasMagic_ValidWrappedRaw) {
	auto src = MakeRepeated(100, 'A');
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());
	EXPECT_TRUE(ibFirebirdBlobCompression::HasMagic(
		wrapped.GetData(), wrapped.GetDataLen()));
}

TEST(BlobCompression, HasMagic_ValidWrappedZlib) {
	auto src = MakeRepeated(2000, 'A');
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());
	EXPECT_TRUE(ibFirebirdBlobCompression::HasMagic(
		wrapped.GetData(), wrapped.GetDataLen()));
}

TEST(BlobCompression, HasMagic_NoMagic) {
	const char src[] = "raw data";
	EXPECT_FALSE(ibFirebirdBlobCompression::HasMagic(src, sizeof(src) - 1));
}

TEST(BlobCompression, HasMagic_TooShort) {
	const uint8_t src[] = { 'O', 'E', 'S' };
	EXPECT_FALSE(ibFirebirdBlobCompression::HasMagic(src, sizeof(src)));
}

// ===========================================================================
// Compression effectiveness sanity checks
// ===========================================================================

TEST(BlobCompression, Compression_ShrinksRepetitive) {
	// 64 KB of repeating "Документ номер %d" pattern (realistic OES
	// document-description content). Must compress significantly.
	std::vector<uint8_t> src;
	src.reserve(64 * 1024);
	for (int i = 0; i < 4000; ++i) {
		char buf[32];
		int n = std::snprintf(buf, sizeof(buf), "Document number %d\n", i);
		for (int j = 0; j < n; ++j) src.push_back((uint8_t)buf[j]);
	}
	wxMemoryBuffer wrapped = ibFirebirdBlobCompression::Wrap(src.data(), src.size());
	const uint8_t* p = static_cast<const uint8_t*>(wrapped.GetData());
	EXPECT_EQ(p[5], ibFirebirdBlobCompression::kFlagZlib);
	// Expect at least 3× compression on this pattern.
	EXPECT_LT(wrapped.GetDataLen(), src.size() / 3);

	wxMemoryBuffer back = ibFirebirdBlobCompression::Unwrap(
		wrapped.GetData(), wrapped.GetDataLen());
	EXPECT_TRUE(BuffersEqual(back, src.data(), src.size()));
}
