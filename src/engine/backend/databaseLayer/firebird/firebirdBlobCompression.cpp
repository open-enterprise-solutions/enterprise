#include "firebirdBlobCompression.h"

#include "backend/backend_exception.h"
#include "core/diagnostics/journal.h"   // ibJournal — this TU does not pull in backend_core.h
#include "core/memory/compression.h"   // ibZlibDeflate / ibZlibInflate

#include <cstring>

bool ibFirebirdBlobCompression::HasMagic(const void* data, size_t size) {
	if (size < kHeaderLen)
		return false;
	const unsigned char* p = static_cast<const unsigned char*>(data);
	return p[0] == kMagic0 && p[1] == kMagic1 && p[2] == kMagic2 && p[3] == kMagic3
	    && p[4] == kVersion
	    && (p[5] == kFlagRaw || p[5] == kFlagZlib);
}

ibMemoryBuffer ibFirebirdBlobCompression::Wrap(const void* data, size_t size) {
	ibMemoryBuffer out;

	// Header (6 bytes).
	out.AppendByte(kMagic0);
	out.AppendByte(kMagic1);
	out.AppendByte(kMagic2);
	out.AppendByte(kMagic3);
	out.AppendByte(kVersion);

	// Defensive — caller may pass (nullptr, 0) for empty BLOB.
	// Treat as zero-length raw body; header still applies so the
	// read path sees a well-formed marker.
	if (data == nullptr || size == 0) {
		out.AppendByte(kFlagRaw);
		return out;
	}

	// Decide whether to compress. Two short-circuit cases land in the
	// raw-body branch:
	//   1. Payload below threshold — zlib overhead would exceed savings.
	//   2. Payload above threshold but tries-and-fails to compress
	//      well — already-compressed media types (JPEG / PDF / DOCX /
	//      ZIP). Storing raw avoids the per-read decompression hit.
	bool stored = false;
	if (size >= kCompressThresholdBytes) {
		// Header flag = zlib, then the deflated body, packed straight into `out`.
		out.AppendByte(kFlagZlib);
		const size_t bodyAt = out.GetDataLen();
		if (ibZlibDeflate(data, size, out)) {
			const size_t compressedSize = out.GetDataLen() - bodyAt;
			stored = compressedSize > 0 && static_cast<double>(compressedSize)
			    < static_cast<double>(size) * kAcceptableRatio;
		}
		else {
			ibJournalWarning(ibT("db.firebird"), ibT("ibFirebirdBlobCompression::Wrap: deflate failed; falling back to raw"));
		}
		if (!stored)
			out.SetDataLen(bodyAt - /*flag=*/1);   // the flag byte and whatever was packed come back off
	}

	if (!stored) {
		// Raw body — header flag = raw, then original bytes verbatim.
		out.AppendByte(kFlagRaw);
		out.AppendData(data, size);
	}

	return out;
}

ibMemoryBuffer ibFirebirdBlobCompression::Unwrap(const void* data, size_t size) {
	// Legacy payload — no magic header. Return the original bytes
	// untouched. This lets the driver read existing databases that
	// pre-date the compression feature without rewriting them.
	if (!HasMagic(data, size)) {
		ibMemoryBuffer raw(size);
		if (size > 0 && data != nullptr) {
			void* dst = raw.GetWriteBuf(size);
			std::memcpy(dst, data, size);
			raw.UngetWriteBuf(size);
		}
		raw.SetDataLen(size);
		return raw;
	}

	const unsigned char* p     = static_cast<const unsigned char*>(data);
	const unsigned char  flag  = p[5];
	const unsigned char* body  = p + kHeaderLen;
	const size_t         bodyN = size - kHeaderLen;

	if (flag == kFlagRaw) {
		ibMemoryBuffer out(bodyN);
		if (bodyN > 0) {
			void* dst = out.GetWriteBuf(bodyN);
			std::memcpy(dst, body, bodyN);
			out.UngetWriteBuf(bodyN);
		}
		out.SetDataLen(bodyN);
		return out;
	}

	// kFlagZlib — a corrupt header, a truncated body or a bad checksum is a false here.
	// ⭐ RAISES, and used to only log — while STILL RETURNING the partial inflate. The comment here
	// said "caller treats as I/O error", but nothing made that true: the caller got a short buffer
	// and no failure, so a corrupt blob read back as a SHORTER, PERFECTLY VALID-LOOKING one. There
	// is no answer to give here. Half of a value is not a value, and handing it back is worse than
	// refusing, because refusing is visible.
	ibMemoryBuffer out;
	if (!ibZlibInflate(body, bodyN, out)) {
		ibBackendCoreException::Error(
			_("Firebird: the compressed value is corrupt (%llu bytes read before the error)"),
			(unsigned long long)out.GetDataLen());
	}

	return out;
}
