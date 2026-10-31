#include "webSocketFrame.h"

#include <random>

#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include <wx/mstream.h>
#include <wx/zstream.h>

#include "core/fstring.h"   // ibString — the text a deflated message is
#include "core/memory/memoryBuffer.h"   // ibMemoryBuffer — …and the packet one is

namespace {

// Where a frame's head ends and what it says, read off its first bytes.
constexpr std::size_t kShortHead = 2;

// The bytes already read are dropped once this many of them are behind: a socket that is never quiet would
// otherwise keep everything it was ever sent.
constexpr std::size_t kCompactFrom = 64 * 1024;

std::string Base64(const unsigned char* data, std::size_t size)
{
	unsigned char text[64] = {};
	std::size_t written = 0;
	if (mbedtls_base64_encode(text, sizeof(text), &written, data, size) != 0)
		return std::string();
	return std::string(reinterpret_cast<const char*>(text), written);
}

// Four bytes the peer cannot foresee — a masking key (RFC 6455 § 5.3: a proxy must not be able to predict it).
std::uint32_t Unforeseen()
{
	std::random_device device;
	return device();
}

bool IsControl(std::uint8_t opcode)
{
	return (opcode & 0x08) != 0;
}

bool IsKnown(std::uint8_t opcode)
{
	switch (opcode) {
	case ibWebSocketOpcode::Continuation:
	case ibWebSocketOpcode::Text:
	case ibWebSocketOpcode::Binary:
	case ibWebSocketOpcode::Close:
	case ibWebSocketOpcode::Ping:
	case ibWebSocketOpcode::Pong:
		return true;
	default:
		return false;
	}
}

} // namespace

std::string ibWebSocketFrame::Encode(bool masked) const
{
	std::string bytes;
	const std::uint64_t size = m_payload.size();
	bytes.reserve(m_payload.size() + 14);
	bytes.push_back(static_cast<char>((m_final ? 0x80 : 0x00) | (m_opcode & 0x0F)));
	const unsigned char mask = masked ? 0x80 : 0x00;
	if (size < 126) {
		bytes.push_back(static_cast<char>(mask | size));
	}
	else if (size <= 0xFFFF) {
		bytes.push_back(static_cast<char>(mask | 126));
		bytes.push_back(static_cast<char>((size >> 8) & 0xFF));
		bytes.push_back(static_cast<char>(size & 0xFF));
	}
	else {
		bytes.push_back(static_cast<char>(mask | 127));
		for (int shift = 56; shift >= 0; shift -= 8)
			bytes.push_back(static_cast<char>((size >> shift) & 0xFF));
	}
	if (!masked) {
		bytes += m_payload;
		return bytes;
	}
	const std::uint32_t key = Unforeseen();
	const unsigned char keyBytes[4] = {
		static_cast<unsigned char>(key >> 24), static_cast<unsigned char>(key >> 16),
		static_cast<unsigned char>(key >> 8), static_cast<unsigned char>(key) };
	bytes.append(reinterpret_cast<const char*>(keyBytes), 4);
	const std::size_t from = bytes.size();
	bytes += m_payload;
	for (std::size_t i = 0; i < m_payload.size(); ++i)
		bytes[from + i] = static_cast<char>(bytes[from + i] ^ keyBytes[i & 3]);
	return bytes;
}

ibWebSocketFrameReader::ibWebSocketFrameReader(bool masked, std::size_t limit) :
	m_masked(masked), m_limit(limit)
{
}

void ibWebSocketFrameReader::Feed(const char* data, std::size_t size)
{
	if (m_at > kCompactFrom && m_at * 2 > m_bytes.size()) {
		m_bytes.erase(0, m_at);
		m_at = 0;
	}
	m_bytes.append(data, size);
}

bool ibWebSocketFrameReader::Next(ibWebSocketFrame& frame, std::uint16_t& refusal)
{
	refusal = 0;
	const std::size_t held = m_bytes.size() - m_at;
	if (held < kShortHead)
		return false;
	const unsigned char* const head = reinterpret_cast<const unsigned char*>(m_bytes.data()) + m_at;

	const bool final = (head[0] & 0x80) != 0;
	const std::uint8_t opcode = head[0] & 0x0F;
	const bool masked = (head[1] & 0x80) != 0;
	std::uint64_t size = head[1] & 0x7F;
	if ((head[0] & 0x70) != 0 || !IsKnown(opcode) || masked != m_masked) {
		refusal = ibWebSocketClose::ProtocolError;
		return false;
	}
	if (IsControl(opcode) && (!final || size > 125)) {
		refusal = ibWebSocketClose::ProtocolError;
		return false;
	}

	std::size_t length = kShortHead;
	if (size == 126) {
		if (held < 4)
			return false;
		size = (static_cast<std::uint64_t>(head[2]) << 8) | head[3];
		length = 4;
	}
	else if (size == 127) {
		if (held < 10)
			return false;
		size = 0;
		for (int i = 2; i < 10; ++i)
			size = (size << 8) | head[i];
		if ((size >> 63) != 0) {
			refusal = ibWebSocketClose::ProtocolError;
			return false;
		}
		length = 10;
	}
	if (size > m_limit) {
		refusal = ibWebSocketClose::TooBig;
		return false;
	}
	const unsigned char* key = nullptr;
	if (masked) {
		if (held < length + 4)
			return false;
		key = head + length;
		length += 4;
	}
	if (held < length + size)
		return false;

	frame.m_final = final;
	frame.m_opcode = opcode;
	frame.m_payload.assign(reinterpret_cast<const char*>(head + length), static_cast<std::size_t>(size));
	if (key != nullptr) {
		for (std::size_t i = 0; i < frame.m_payload.size(); ++i)
			frame.m_payload[i] = static_cast<char>(frame.m_payload[i] ^ key[i & 3]);
	}
	m_at += length + static_cast<std::size_t>(size);
	if (m_at == m_bytes.size()) {
		m_bytes.clear();
		m_at = 0;
	}
	return true;
}

std::string ibWebSocketAccept(const std::string& key)
{
	static const char s_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	const std::string joined = key + s_guid;
	unsigned char digest[20] = {};
	if (mbedtls_sha1(reinterpret_cast<const unsigned char*>(joined.data()), joined.size(), digest) != 0)
		return std::string();
	return Base64(digest, sizeof(digest));
}

std::string ibWebSocketKey()
{
	unsigned char nonce[16];
	for (std::size_t i = 0; i < sizeof(nonce); i += 4) {
		const std::uint32_t random = Unforeseen();
		nonce[i] = static_cast<unsigned char>(random >> 24);
		nonce[i + 1] = static_cast<unsigned char>(random >> 16);
		nonce[i + 2] = static_cast<unsigned char>(random >> 8);
		nonce[i + 3] = static_cast<unsigned char>(random);
	}
	return Base64(nonce, sizeof(nonce));
}

bool ibIsUtf8(const std::string& text)
{
	const unsigned char* at = reinterpret_cast<const unsigned char*>(text.data());
	const unsigned char* const end = at + text.size();
	while (at < end) {
		const unsigned char lead = *at;
		if (lead < 0x80) {
			++at;
			continue;
		}
		std::size_t follow = 0;
		std::uint32_t point = 0;
		std::uint32_t least = 0;
		if ((lead & 0xE0) == 0xC0) {
			follow = 1; point = lead & 0x1F; least = 0x80;
		}
		else if ((lead & 0xF0) == 0xE0) {
			follow = 2; point = lead & 0x0F; least = 0x800;
		}
		else if ((lead & 0xF8) == 0xF0) {
			follow = 3; point = lead & 0x07; least = 0x10000;
		}
		else {
			return false;
		}
		if (static_cast<std::size_t>(end - at) <= follow)
			return false;
		for (std::size_t i = 1; i <= follow; ++i) {
			if ((at[i] & 0xC0) != 0x80)
				return false;
			point = (point << 6) | (at[i] & 0x3F);
		}
		// An overlong form, a surrogate, past the last code point — none is UTF-8.
		if (point < least || (point >= 0xD800 && point <= 0xDFFF) || point > 0x10FFFF)
			return false;
		at += follow + 1;
	}
	return true;
}

bool ibDeflate(const std::string& bytes, std::string& packed)
{
	wxMemoryOutputStream out;
	{
		wxZlibOutputStream zip(out, wxZ_DEFAULT_COMPRESSION, wxZLIB_NO_HEADER);
		zip.Write(bytes.data(), bytes.size());
		if (!zip.Close())
			return false;
	}
	const wxStreamBuffer* const buffer = out.GetOutputStreamBuffer();
	packed.assign(static_cast<const char*>(buffer->GetBufferStart()), static_cast<std::size_t>(out.GetLength()));
	return true;
}

namespace {

// The bytes a deflated message holds, appended to `to` a chunk at a time — false when they do not inflate.
template<class Bytes, class Append>
bool Inflate(const std::string& packed, Bytes& to, Append append)
{
	wxMemoryInputStream in(packed.data(), packed.size());
	wxZlibInputStream unzip(in, wxZLIB_NO_HEADER);
	char chunk[16384];
	for (;;) {
		unzip.Read(chunk, sizeof(chunk));
		const std::size_t got = unzip.LastRead();
		if (got == 0)
			break;
		append(to, chunk, got);
	}
	return unzip.GetLastError() == wxSTREAM_NO_ERROR || unzip.GetLastError() == wxSTREAM_EOF;
}

} // namespace

bool ibInflate(const std::string& packed, ibString& text)
{
	std::string bytes;
	if (!Inflate(packed, bytes, [](std::string& to, const char* chunk, std::size_t got) { to.append(chunk, got); }))
		return false;
	text.SetUtf8(bytes.data(), bytes.size());
	return true;
}

bool ibInflate(const std::string& packed, ibMemoryBuffer& bytes)
{
	bytes.SetDataLen(0);
	return Inflate(packed, bytes, [](ibMemoryBuffer& to, const char* chunk, std::size_t got) { to.AppendData(chunk, got); });
}
