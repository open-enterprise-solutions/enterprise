#ifndef __CORE_MEMORY_ZIP_H__
#define __CORE_MEMORY_ZIP_H__

// A ZIP ARCHIVE, read and written over the standard streams - an Excel workbook and a Word document are one, so are a
// base's backup and the help's pages. Only what those are made of: entries stored or deflated (RFC 1951, by the zlib
// the core links already), names in UTF-8, ZIP64 where a size or an offset does not fit in four bytes. No encryption,
// no other method, no archive spanned over several files.

#include "core/core.h"
#include "core/fstring.h"               // ibString - an entry's name
#include "core/memory/memoryBuffer.h"   // ibMemoryBuffer - an entry's bytes

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

// FORWARD, ENTRY BY ENTRY: the local headers in the order they were stored, as a stream that cannot seek back has them
// (the central directory at the end is not read). An entry that is not read is passed over by the next GetNextEntry.
class CORE_API ibZipReader {
public:
	explicit ibZipReader(std::istream& input);

	bool GetNextEntry(ibString& name);       // false at the end, or on a damaged archive
	// The entry whole, appended to `bytes`, its CRC checked. False: damaged, encrypted, or packed by a method other
	// than stored and deflated (`bytes` then as it was).
	bool ReadEntry(ibMemoryBuffer& bytes);

private:
	bool Fill();
	bool Take(void* data, std::size_t size);
	bool Copy(std::uint64_t size, ibMemoryBuffer* bytes);
	bool Inflate(ibMemoryBuffer* bytes, std::uint32_t& crc, std::uint64_t& packed, std::uint64_t& size);
	bool Unpack(ibMemoryBuffer* bytes);
	bool Stop();

	std::istream& m_input;
	std::string   m_buffer;               // read ahead: an entry's deflated bytes end inside it
	std::size_t   m_at = 0;
	bool          m_pending = false;      // an entry's header is read, its bytes are not
	bool          m_ended = false;
	bool          m_zip64 = false;
	std::uint16_t m_flags = 0;
	std::uint16_t m_method = 0;
	std::uint32_t m_crc = 0;
	std::uint64_t m_packedSize = 0;
	std::uint64_t m_size = 0;
};

// AN ENTRY AT A TIME: its bytes are deflated as they come and held until the next entry (or Close), when its local
// header goes out saying its sizes and CRC, and the bytes after it. No data descriptor, so every reader reads it; no
// seeking back, so any stream takes it. An archive is whole when Close answers true - not before.
class CORE_API ibZipWriter {
public:
	explicit ibZipWriter(std::ostream& output);
	~ibZipWriter();

	bool PutNextEntry(const ibString& name);
	bool Write(const void* data, std::size_t size);
	bool Close();                            // the central directory; false: something did not go out

private:
	struct Packer;                           // the entry's deflate stream - zlib's, not named here
	struct Entry {
		std::string   m_name;                // UTF-8
		std::uint16_t m_flags = 0;
		std::uint32_t m_crc = 0;
		std::uint64_t m_packedSize = 0;
		std::uint64_t m_size = 0;
		std::uint64_t m_offset = 0;          // of its local header
	};

	bool Deflate(const void* data, std::size_t size, int flush);
	bool Flush();
	bool Out(const void* data, std::size_t size);
	bool Fail();

	std::ostream&           m_output;
	std::unique_ptr<Packer> m_packer;        // null: no entry open
	std::vector<Entry>      m_entries;
	ibMemoryBuffer          m_packed;        // the open entry, deflated
	std::uint64_t           m_offset = 0;    // bytes gone out
	std::uint16_t           m_time = 0;      // MS-DOS's form, the moment the archive was begun
	std::uint16_t           m_date = 0;
	bool                    m_failed = false;
	bool                    m_closed = false;
};

#endif
