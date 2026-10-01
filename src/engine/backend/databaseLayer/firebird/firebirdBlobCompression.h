#ifndef __FIREBIRD_BLOB_COMPRESSION_H__
#define __FIREBIRD_BLOB_COMPRESSION_H__

// Transparent zlib compression for BLOB columns at the Firebird driver
// boundary. Engine OES has no knowledge — `SetParamBlob(data)` and
// `GetResultBlob()` keep their existing wire-level semantics; the
// driver wraps payloads with a magic header on write and strips it
// on read. Existing databases continue to work because the magic is
// chosen to not collide with arbitrary BLOB content (legacy payloads
// have no header → driver detects "no magic" and returns the data
// as-is).
//
// Wire format on disk:
//   [4-byte magic = 'O','E','S','C']
//   [1-byte version = 1]
//   [1-byte flag — 0 = raw, 1 = zlib]
//   [payload bytes — raw or zlib-compressed depending on flag]
//
// On read, the magic prefix is checked first; missing magic means
// legacy uncompressed data, returned untouched. Layered headers
// (multiple compression passes) are not supported — we wrap once,
// unwrap once.
//
// VERSION COMPAT WARNING: `HasMagic` requires the version byte to
// match `kVersion` exactly. When `kVersion` is ever bumped, payloads
// written under the old version will be mis-classified as legacy
// (no magic) and returned raw — i.e., the caller will receive the
// compressed bytes verbatim. Plan a migration step (re-wrap pass
// or a multi-version-accepting Unwrap branch) before bumping.
//
// Compression policy:
//   - Skip if payload < kCompressThresholdBytes (overhead not worth it).
//   - Try zlib; if compressed.size() > original.size() * kAcceptableRatio,
//     fall back to "wrapped raw" (header + uncompressed body). This
//     handles already-compressed payloads (JPEG / PDF / DOCX / ZIP)
//     gracefully — they get the header but skip the zlib pass.
//   - We always wrap with the header on write, even when payload is
//     below threshold or doesn't compress well. The marker tells future
//     versions of this code "this BLOB was written by the new driver"
//     so the read path can rely on the format unambiguously.

#include "backend/backend.h"

#include <wx/buffer.h>

class BACKEND_API ibFirebirdBlobCompression {
public:
	// Default compression cutoff. Payloads smaller than this skip the
	// zlib step (overhead would dominate for tiny BLOBs). Header is
	// still applied so the read path always sees the magic.
	static constexpr size_t kCompressThresholdBytes = 512;

	// Accept compressed output only if it's at least 15% smaller than
	// raw. Tight ratios mean the body is incompressible (JPEG, etc.) —
	// store raw to skip the decompression cost on every read.
	static constexpr double kAcceptableRatio = 0.85;

	// Header layout — kept in code rather than embedded magic numbers
	// so future format bumps are explicit.
	static constexpr unsigned char kMagic0 = 'O';
	static constexpr unsigned char kMagic1 = 'E';
	static constexpr unsigned char kMagic2 = 'S';
	static constexpr unsigned char kMagic3 = 'C';
	static constexpr unsigned char kVersion = 1;
	static constexpr unsigned char kFlagRaw  = 0;
	static constexpr unsigned char kFlagZlib = 1;
	static constexpr size_t        kHeaderLen = 6;

	// Wrap raw payload for write. Always returns a buffer starting with
	// the magic header. Compression is attempted only when the payload
	// is large enough and zlib actually gains us anything; otherwise
	// the body stays raw under the header.
	static wxMemoryBuffer Wrap(const void* data, size_t size);

	// Unwrap a payload from read. Detects the magic header; on miss
	// returns the input verbatim (legacy uncompressed payload from a
	// pre-compression-feature write).
	static wxMemoryBuffer Unwrap(const void* data, size_t size);

	// True if the buffer starts with our magic. Cheap probe — read
	// paths use this before paying the decompression cost.
	static bool HasMagic(const void* data, size_t size);
};

#endif // __FIREBIRD_BLOB_COMPRESSION_H__
