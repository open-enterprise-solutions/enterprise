#include "core/zip/zip.h"
#include "core/fdatetime.h"   // ibDateTime - the moment an entry is stamped with

#include <algorithm>
#include <climits>
#include <cstring>
#include <istream>
#include <ostream>

#define ZLIB_CONST   // next_in is a const Bytef*: what is fed to zlib is read, never written
#include <zlib.h>

namespace {

// The records, by their signatures (APPNOTE.TXT 4.3).
constexpr std::uint32_t kLocalHeader = 0x04034b50;
constexpr std::uint32_t kDescriptor = 0x08074b50;
constexpr std::uint32_t kCentralHeader = 0x02014b50;
constexpr std::uint32_t kEnd = 0x06054b50;
constexpr std::uint32_t kEnd64 = 0x06064b50;
constexpr std::uint32_t kEnd64Locator = 0x07064b50;

constexpr std::uint16_t kEncrypted = 0x0001;
constexpr std::uint16_t kDescribed = 0x0008;   // the CRC and the sizes follow the bytes, in a descriptor
constexpr std::uint16_t kUtf8Name = 0x0800;

constexpr std::uint16_t kStored = 0;
constexpr std::uint16_t kDeflated = 8;

constexpr std::uint16_t kZip64Extra = 0x0001;
constexpr std::uint32_t kNoFit32 = 0xFFFFFFFF;   // "in the ZIP64 field"
constexpr std::uint16_t kNoFit16 = 0xFFFF;

constexpr std::uint16_t kVersion = 20;     // 2.0: deflate
constexpr std::uint16_t kVersion64 = 45;   // 4.5: ZIP64

constexpr std::size_t kChunk = 64 * 1024;

std::uint16_t Le16(const unsigned char* at)
{
	return static_cast<std::uint16_t>(at[0] | (at[1] << 8));
}

std::uint32_t Le32(const unsigned char* at)
{
	return Le16(at) | (static_cast<std::uint32_t>(Le16(at + 2)) << 16);
}

std::uint64_t Le64(const unsigned char* at)
{
	return Le32(at) | (static_cast<std::uint64_t>(Le32(at + 4)) << 32);
}

void Put16(std::string& to, std::uint16_t value)
{
	to += static_cast<char>(value & 0xFF);
	to += static_cast<char>(value >> 8);
}

void Put32(std::string& to, std::uint32_t value)
{
	Put16(to, static_cast<std::uint16_t>(value));
	Put16(to, static_cast<std::uint16_t>(value >> 16));
}

void Put64(std::string& to, std::uint64_t value)
{
	Put32(to, static_cast<std::uint32_t>(value));
	Put32(to, static_cast<std::uint32_t>(value >> 32));
}

bool NoFit(std::uint64_t value)
{
	return value >= kNoFit32;
}

// zlib's CRC over any size - it counts in uInt.
std::uint32_t Crc(std::uint32_t crc, const void* data, std::size_t size)
{
	const Bytef* at = static_cast<const Bytef*>(data);
	while (size > 0) {
		const uInt piece = static_cast<uInt>(std::min<std::size_t>(size, UINT_MAX));
		crc = static_cast<std::uint32_t>(crc32(crc, at, piece));
		at += piece;
		size -= piece;
	}
	return crc;
}

} // namespace

//////////////////////////////////////////////////////////////////////
// The reader
//////////////////////////////////////////////////////////////////////

ibZipReader::ibZipReader(std::istream& input) : m_input(input)
{
}

bool ibZipReader::GetNextEntry(ibString& name)
{
	if (m_pending && !Unpack(nullptr))
		return false;
	if (m_ended)
		return false;

	// The central directory, the end of the bytes, or anything else that is not an entry: no more entries.
	unsigned char head[30];
	if (!Take(head, 4) || Le32(head) != kLocalHeader || !Take(head + 4, sizeof(head) - 4))
		return Stop();
	m_flags = Le16(head + 6);
	m_method = Le16(head + 8);
	m_crc = Le32(head + 14);
	m_packedSize = Le32(head + 18);
	m_size = Le32(head + 22);
	std::string spelled(Le16(head + 26), '\0'), extra(Le16(head + 28), '\0');
	if (!Take(spelled.data(), spelled.size()) || !Take(extra.data(), extra.size()))
		return Stop();

	// The sizes that did not fit, in the ZIP64 field - the uncompressed first, each only when its own field says so.
	m_zip64 = false;
	const unsigned char* const fields = reinterpret_cast<const unsigned char*>(extra.data());
	for (std::size_t at = 0; at + 4 <= extra.size(); ) {
		const std::uint16_t id = Le16(fields + at);
		const std::size_t length = Le16(fields + at + 2);
		at += 4;
		if (at + length > extra.size())
			break;
		if (id == kZip64Extra) {
			m_zip64 = true;
			std::size_t in = at;
			if (m_size == kNoFit32 && in + 8 <= at + length) {
				m_size = Le64(fields + in);
				in += 8;
			}
			if (m_packedSize == kNoFit32 && in + 8 <= at + length)
				m_packedSize = Le64(fields + in);
		}
		at += length;
	}

	// UTF-8 whether or not the flag says so: what the engine writes is, and a name in the old DOS page is ASCII here.
	name = ibString::FromUTF8(spelled);
	m_pending = true;
	return true;
}

bool ibZipReader::ReadEntry(ibMemoryBuffer& bytes)
{
	if (!m_pending)
		return false;
	const std::size_t was = bytes.GetDataLen();
	if (Unpack(&bytes))
		return true;
	bytes.SetDataLen(was);
	return false;
}

// More of the archive behind what is held, the part already read let go. False: nothing more.
bool ibZipReader::Fill()
{
	if (m_at > 0) {
		m_buffer.erase(0, m_at);
		m_at = 0;
	}
	const std::size_t held = m_buffer.size();
	m_buffer.resize(held + kChunk);
	m_input.read(&m_buffer[held], static_cast<std::streamsize>(kChunk));
	const std::size_t got = static_cast<std::size_t>(m_input.gcount());
	m_buffer.resize(held + got);
	return got > 0;
}

bool ibZipReader::Take(void* data, std::size_t size)
{
	while (m_buffer.size() - m_at < size) {
		if (!Fill())
			return false;
	}
	if (size > 0)
		std::memcpy(data, m_buffer.data() + m_at, size);
	m_at += size;
	return true;
}

// `size` bytes of the archive appended to `bytes` - or passed over, when it is null.
bool ibZipReader::Copy(std::uint64_t size, ibMemoryBuffer* bytes)
{
	while (size > 0) {
		if (m_at == m_buffer.size() && !Fill())
			return false;
		const std::size_t piece = static_cast<std::size_t>(std::min<std::uint64_t>(size, m_buffer.size() - m_at));
		if (bytes != nullptr)
			bytes->AppendData(m_buffer.data() + m_at, piece);
		m_at += piece;
		size -= piece;
	}
	return true;
}

// The entry's deflated bytes up to the end the stream itself marks, inflated into `bytes` (or nowhere) - how many went
// in, how many came out, and their CRC when they are kept.
bool ibZipReader::Inflate(ibMemoryBuffer* bytes, std::uint32_t& crc, std::uint64_t& packed, std::uint64_t& size)
{
	z_stream stream{};
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
		return false;
	std::string scratch(bytes == nullptr ? kChunk : 0, '\0');   // where an entry passed over is inflated to

	int state = Z_OK;
	do {
		if (m_at == m_buffer.size() && !Fill())
			break;   // the archive ends inside the entry
		const std::size_t held = std::min<std::size_t>(m_buffer.size() - m_at, UINT_MAX);
		Bytef* const out = bytes != nullptr ? static_cast<Bytef*>(bytes->GetAppendBuf(kChunk))
			: reinterpret_cast<Bytef*>(&scratch[0]);
		stream.next_in = reinterpret_cast<Bytef*>(&m_buffer[m_at]);
		stream.avail_in = static_cast<uInt>(held);
		stream.next_out = out;
		stream.avail_out = static_cast<uInt>(kChunk);
		state = inflate(&stream, Z_NO_FLUSH);
		const std::size_t taken = held - stream.avail_in;
		const std::size_t made = kChunk - stream.avail_out;
		m_at += taken;
		packed += taken;
		size += made;
		if (bytes != nullptr) {
			crc = Crc(crc, out, made);
			bytes->UngetAppendBuf(made);
		}
		if (state == Z_BUF_ERROR && taken == 0 && made == 0 && m_at != m_buffer.size())
			break;   // no step forward with the input at hand
	} while (state == Z_OK || state == Z_BUF_ERROR);
	inflateEnd(&stream);
	return state == Z_STREAM_END;
}

// THE ENTRY'S BYTES, into `bytes` or passed over when it is null, and the descriptor after them. False: the entry is not
// whole or not readable here - and when where it ends cannot be told, nothing after it is read either.
bool ibZipReader::Unpack(ibMemoryBuffer* bytes)
{
	m_pending = false;
	const bool described = (m_flags & kDescribed) != 0;
	const bool readable = (m_flags & kEncrypted) == 0 && (m_method == kStored || m_method == kDeflated);

	// Its end said only after it: found by inflating up to it, which a stored or an encrypted entry cannot be.
	if (described && (!readable || m_method != kDeflated))
		return Stop();
	// Passed over by the size its header says.
	if (!described && (bytes == nullptr || !readable)) {
		if (!Copy(m_packedSize, nullptr))
			return Stop();
		return bytes == nullptr;
	}

	std::uint32_t crc = 0;
	std::uint64_t packed = 0, size = 0;
	if (m_method == kStored) {
		const std::size_t from = bytes->GetDataLen();
		if (!Copy(m_packedSize, bytes))
			return Stop();
		crc = Crc(0, static_cast<const char*>(bytes->GetData()) + from, bytes->GetDataLen() - from);
		packed = size = m_packedSize;
	}
	else if (!Inflate(bytes, crc, packed, size)) {
		return Stop();
	}

	if (described) {
		unsigned char word[8];
		if (!Take(word, 4))
			return Stop();
		// The signature is optional: a first word that is it, and not the CRC itself, is passed.
		if (Le32(word) == kDescriptor && (bytes == nullptr || crc != kDescriptor) && !Take(word, 4))
			return Stop();
		m_crc = Le32(word);
		const std::size_t width = m_zip64 ? 8 : 4;
		if (!Take(word, width))
			return Stop();
		m_packedSize = m_zip64 ? Le64(word) : Le32(word);
		if (!Take(word, width))
			return Stop();
		m_size = m_zip64 ? Le64(word) : Le32(word);
	}
	return bytes == nullptr || (crc == m_crc && packed == m_packedSize && size == m_size);
}

bool ibZipReader::Stop()
{
	m_ended = true;
	m_pending = false;
	return false;
}

//////////////////////////////////////////////////////////////////////
// The writer
//////////////////////////////////////////////////////////////////////

struct ibZipWriter::Packer {
	z_stream m_stream{};
	bool     m_ready = false;

	~Packer() {
		if (m_ready)
			deflateEnd(&m_stream);
	}
};

ibZipWriter::ibZipWriter(std::ostream& output) : m_output(output)
{
	ibDateTimeParts now;
	ibDateTime::Now().ToParts(now);
	m_time = static_cast<std::uint16_t>((now.m_hour << 11) | (now.m_minute << 5) | (now.m_second / 2));
	m_date = static_cast<std::uint16_t>(((std::max(now.m_year, 1980) - 1980) << 9) | (now.m_month << 5) | now.m_day);
}

ibZipWriter::~ibZipWriter() = default;

bool ibZipWriter::PutNextEntry(const ibString& name)
{
	if (m_closed || !Flush())
		return false;

	Entry entry;
	entry.m_name = name.ToUtf8();
	if (entry.m_name.size() > kNoFit16)
		return Fail();
	for (const char c : entry.m_name) {
		if (static_cast<unsigned char>(c) >= 0x80) {
			entry.m_flags = kUtf8Name;
			break;
		}
	}
	entry.m_offset = m_offset;

	auto packer = std::make_unique<Packer>();
	// The default level, as every archiver writes by default.
	if (deflateInit2(&packer->m_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return Fail();
	packer->m_ready = true;

	m_packer = std::move(packer);
	m_entries.push_back(std::move(entry));
	m_packed.Clear();
	return true;
}

bool ibZipWriter::Write(const void* data, std::size_t size)
{
	if (m_failed || m_packer == nullptr)
		return false;
	Entry& entry = m_entries.back();
	entry.m_crc = Crc(entry.m_crc, data, size);
	entry.m_size += size;
	return Deflate(data, size, Z_NO_FLUSH) || Fail();
}

bool ibZipWriter::Close()
{
	if (m_closed)
		return !m_failed;
	if (!Flush())
		return false;
	m_closed = true;

	const std::uint64_t start = m_offset;
	std::string directory;
	for (const Entry& entry : m_entries) {
		const bool sizes64 = NoFit(entry.m_size) || NoFit(entry.m_packedSize);
		const bool offset64 = NoFit(entry.m_offset);
		std::string extra;
		if (sizes64) {
			Put64(extra, entry.m_size);
			Put64(extra, entry.m_packedSize);
		}
		if (offset64)
			Put64(extra, entry.m_offset);
		const std::uint16_t version = extra.empty() ? kVersion : kVersion64;

		Put32(directory, kCentralHeader);
		Put16(directory, version);   // made by - MS-DOS's attributes, which say nothing
		Put16(directory, version);   // needed
		Put16(directory, entry.m_flags);
		Put16(directory, kDeflated);
		Put16(directory, m_time);
		Put16(directory, m_date);
		Put32(directory, entry.m_crc);
		Put32(directory, sizes64 ? kNoFit32 : static_cast<std::uint32_t>(entry.m_packedSize));
		Put32(directory, sizes64 ? kNoFit32 : static_cast<std::uint32_t>(entry.m_size));
		Put16(directory, static_cast<std::uint16_t>(entry.m_name.size()));
		Put16(directory, static_cast<std::uint16_t>(extra.empty() ? 0 : extra.size() + 4));
		Put16(directory, 0);   // comment
		Put16(directory, 0);   // disk
		Put16(directory, 0);   // internal attributes
		Put32(directory, 0);   // external attributes
		Put32(directory, offset64 ? kNoFit32 : static_cast<std::uint32_t>(entry.m_offset));
		directory += entry.m_name;
		if (!extra.empty()) {
			Put16(directory, kZip64Extra);
			Put16(directory, static_cast<std::uint16_t>(extra.size()));
			directory += extra;
		}
	}

	const std::uint64_t size = directory.size();
	const std::uint64_t count = m_entries.size();
	std::string end;
	if (NoFit(start) || NoFit(size) || count >= kNoFit16) {
		Put32(end, kEnd64);
		Put64(end, 44);   // what of the record follows this field
		Put16(end, kVersion64);
		Put16(end, kVersion64);
		Put32(end, 0);    // this disk
		Put32(end, 0);    // the directory's disk
		Put64(end, count);
		Put64(end, count);
		Put64(end, size);
		Put64(end, start);
		Put32(end, kEnd64Locator);
		Put32(end, 0);    // the record's disk
		Put64(end, start + size);
		Put32(end, 1);    // disks in all
	}
	Put32(end, kEnd);
	Put16(end, 0);
	Put16(end, 0);
	Put16(end, static_cast<std::uint16_t>(count >= kNoFit16 ? kNoFit16 : count));
	Put16(end, static_cast<std::uint16_t>(count >= kNoFit16 ? kNoFit16 : count));
	Put32(end, NoFit(size) ? kNoFit32 : static_cast<std::uint32_t>(size));
	Put32(end, NoFit(start) ? kNoFit32 : static_cast<std::uint32_t>(start));
	Put16(end, 0);    // comment

	if (!Out(directory.data(), directory.size()) || !Out(end.data(), end.size()))
		return false;
	m_output.flush();
	return m_output.good() || Fail();
}

// `size` bytes at `data` through the open entry's deflate stream into m_packed - and with Z_FINISH the stream ended.
bool ibZipWriter::Deflate(const void* data, std::size_t size, int flush)
{
	z_stream& stream = m_packer->m_stream;
	const Bytef* at = static_cast<const Bytef*>(data);
	std::size_t left = size;
	int state = Z_OK;
	do {
		if (stream.avail_in == 0 && left > 0) {
			const uInt piece = static_cast<uInt>(std::min<std::size_t>(left, UINT_MAX));
			stream.next_in = at;
			stream.avail_in = piece;
			at += piece;
			left -= piece;
		}
		stream.next_out = static_cast<Bytef*>(m_packed.GetAppendBuf(kChunk));
		stream.avail_out = static_cast<uInt>(kChunk);
		state = deflate(&stream, left == 0 ? flush : Z_NO_FLUSH);
		m_packed.UngetAppendBuf(kChunk - stream.avail_out);
		if (state == Z_STREAM_ERROR)
			return false;
	} while (state != Z_STREAM_END && (left > 0 || stream.avail_in > 0 || stream.avail_out == 0 || flush == Z_FINISH));
	return flush != Z_FINISH || state == Z_STREAM_END;
}

// THE OPEN ENTRY OUT: its stream finished, then its local header - the sizes and the CRC known now - and its bytes.
bool ibZipWriter::Flush()
{
	if (m_failed)
		return false;
	if (m_packer == nullptr)
		return true;
	const bool finished = Deflate(nullptr, 0, Z_FINISH);
	m_packer.reset();
	if (!finished)
		return Fail();

	Entry& entry = m_entries.back();
	entry.m_packedSize = m_packed.GetDataLen();
	const bool zip64 = NoFit(entry.m_size) || NoFit(entry.m_packedSize);

	std::string head;
	Put32(head, kLocalHeader);
	Put16(head, zip64 ? kVersion64 : kVersion);
	Put16(head, entry.m_flags);
	Put16(head, kDeflated);
	Put16(head, m_time);
	Put16(head, m_date);
	Put32(head, entry.m_crc);
	Put32(head, zip64 ? kNoFit32 : static_cast<std::uint32_t>(entry.m_packedSize));
	Put32(head, zip64 ? kNoFit32 : static_cast<std::uint32_t>(entry.m_size));
	Put16(head, static_cast<std::uint16_t>(entry.m_name.size()));
	Put16(head, static_cast<std::uint16_t>(zip64 ? 20 : 0));
	head += entry.m_name;
	if (zip64) {
		Put16(head, kZip64Extra);
		Put16(head, 16);
		Put64(head, entry.m_size);
		Put64(head, entry.m_packedSize);
	}

	const bool written = Out(head.data(), head.size()) && Out(m_packed.GetData(), m_packed.GetDataLen());
	m_packed.Clear();
	return written;
}

bool ibZipWriter::Out(const void* data, std::size_t size)
{
	if (size == 0)
		return true;
	m_output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	m_offset += size;
	return m_output.good() || Fail();
}

bool ibZipWriter::Fail()
{
	m_failed = true;
	return false;
}
