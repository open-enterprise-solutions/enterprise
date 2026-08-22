#include "firebirdBlobCompression.h"

#include "backend/backend_exception.h"
#include "backend/diagnostics/journal.h"   // ibJournal — this TU does not pull in backend_core.h

#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/zstream.h>

#include <cstring>

bool ibFirebirdBlobCompression::HasMagic(const void* data, size_t size) {
	if (size < kHeaderLen)
		return false;
	const unsigned char* p = static_cast<const unsigned char*>(data);
	return p[0] == kMagic0 && p[1] == kMagic1 && p[2] == kMagic2 && p[3] == kMagic3
	    && p[4] == kVersion
	    && (p[5] == kFlagRaw || p[5] == kFlagZlib);
}

wxMemoryBuffer ibFirebirdBlobCompression::Wrap(const void* data, size_t size) {
	wxMemoryBuffer out;

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
		wxMemoryOutputStream zStream;
		{
			// Default zlib compression level (6). FB's BLOB sub-system
			// charges per-segment overhead; tight zlib output saves
			// segments too, so default level is the right trade-off
			// between compression ratio and CPU.
			wxZlibOutputStream zOut(zStream, wxZ_DEFAULT_COMPRESSION,
			                        wxZLIB_ZLIB);
			zOut.Write(data, size);
			// zOut closes when leaving scope; flushes the deflate state.
		}
		const size_t compressedSize = zStream.GetSize();
		if (compressedSize > 0 && static_cast<double>(compressedSize)
		    < static_cast<double>(size) * kAcceptableRatio) {
			// Worth keeping. Header flag = zlib, then deflated body.
			// Append directly into `out` via GetAppendBuf — skips the
			// intermediate copy through a temp wxMemoryBuffer.
			out.AppendByte(kFlagZlib);
			void* dst = out.GetAppendBuf(compressedSize);
			const size_t copied = zStream.CopyTo(dst, compressedSize);
			out.UngetAppendBuf(copied);
			if (copied != compressedSize) {
				// Stream reported a different size than we just queried —
				// internal inconsistency, refuse to write a half-baked
				// body. Fall through to raw branch below.
				ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdBlobCompression::Wrap: zStream ")
				             wxT("CopyTo copied %zu/%zu bytes; falling back ")
				             wxT("to raw"), copied, compressedSize);
				// Truncate the partial body + flag byte back off.
				out.SetDataLen(out.GetDataLen() - copied - /*flag=*/1);
			} else {
				stored = true;
			}
		}
	}

	if (!stored) {
		// Raw body — header flag = raw, then original bytes verbatim.
		out.AppendByte(kFlagRaw);
		out.AppendData(data, size);
	}

	return out;
}

wxMemoryBuffer ibFirebirdBlobCompression::Unwrap(const void* data, size_t size) {
	// Legacy payload — no magic header. Return the original bytes
	// untouched. This lets the driver read existing databases that
	// pre-date the compression feature without rewriting them.
	if (!HasMagic(data, size)) {
		wxMemoryBuffer raw(size);
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
		wxMemoryBuffer out(bodyN);
		if (bodyN > 0) {
			void* dst = out.GetWriteBuf(bodyN);
			std::memcpy(dst, body, bodyN);
			out.UngetWriteBuf(bodyN);
		}
		out.SetDataLen(bodyN);
		return out;
	}

	// kFlagZlib — inflate via wxZlibInputStream.
	wxMemoryInputStream inStream(body, bodyN);
	wxZlibInputStream zIn(inStream, wxZLIB_ZLIB);

	wxMemoryBuffer out;
	// Read in 64 KB chunks until EOF. Inflated size is unknown ahead of
	// time; AppendData grows the buffer geometrically.
	constexpr size_t kChunk = 64 * 1024;
	wxMemoryBuffer chunk(kChunk);
	while (true) {
		zIn.Read(chunk.GetWriteBuf(kChunk), kChunk);
		chunk.UngetWriteBuf(kChunk);
		const size_t n = zIn.LastRead();
		if (n == 0)
			break;
		out.AppendData(chunk.GetData(), n);
		if (zIn.Eof())
			break;
	}

	// zlib stream errors (corrupt header / truncated body / bad
	// checksum) are reported through GetLastError(); LastRead() alone
	// just returns 0 and we'd silently return the partial inflate.
	// Surface this — caller treats as I/O error.
	// ⭐ RAISES, and used to only log — while STILL RETURNING the partial inflate. The comment here
	// said "caller treats as I/O error", but nothing made that true: the caller got a short buffer
	// and no failure, so a corrupt blob read back as a SHORTER, PERFECTLY VALID-LOOKING one. There
	// is no answer to give here. Half of a value is not a value, and handing it back is worse than
	// refusing, because refusing is visible.
	if (zIn.GetLastError() != wxSTREAM_NO_ERROR
	 && zIn.GetLastError() != wxSTREAM_EOF) {
		ibBackendCoreException::Error(
			_("Firebird: the compressed value is corrupt (zlib error %d after %llu bytes)"),
			(int)zIn.GetLastError(), (unsigned long long)out.GetDataLen());
	}

	return out;
}
