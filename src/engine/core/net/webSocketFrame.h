#ifndef __CORE_NET_WEB_SOCKET_FRAME_H__
#define __CORE_NET_WEB_SOCKET_FRAME_H__

// A WEBSOCKET'S FRAMES (RFC 6455 § 5) — read as the bytes come and written whole, for both ends: the server's, whose
// peer must mask what it sends, and the client's (the web server's own connection to an application server), which
// masks and must not be sent anything masked. No extension is spoken, so a frame with a reserved bit set is refused.

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/core.h"

namespace ibWebSocketOpcode {
	constexpr std::uint8_t Continuation = 0x0;
	constexpr std::uint8_t Text         = 0x1;
	constexpr std::uint8_t Binary       = 0x2;
	constexpr std::uint8_t Close        = 0x8;
	constexpr std::uint8_t Ping         = 0x9;
	constexpr std::uint8_t Pong         = 0xA;
}

// The closing codes this side sends (RFC 6455 § 7.4.1).
namespace ibWebSocketClose {
	constexpr std::uint16_t Normal        = 1000;
	constexpr std::uint16_t GoingAway     = 1001;
	constexpr std::uint16_t ProtocolError = 1002;
	constexpr std::uint16_t Unsupported   = 1003;
	constexpr std::uint16_t NotText       = 1007;
	constexpr std::uint16_t Policy        = 1008;
	constexpr std::uint16_t TooBig        = 1009;
	constexpr std::uint16_t ServerError   = 1011;
	constexpr std::uint16_t BadGateway    = 1014;
}

struct CORE_API ibWebSocketFrame {
	bool         m_final = true;
	std::uint8_t m_opcode = ibWebSocketOpcode::Text;
	std::string  m_payload;   // unmasked

	// The frame's bytes — masked with a fresh key when `masked` (a client's frame), as they are not otherwise.
	std::string Encode(bool masked) const;
};

class CORE_API ibWebSocketFrameReader {
public:

	// `masked` — the peer is a client, and every frame it sends is masked; otherwise none may be. `limit` — the
	// longest payload taken.
	ibWebSocketFrameReader(bool masked, std::size_t limit);

	void Feed(const char* data, std::size_t size);

	// THE NEXT FRAME, if its bytes are all here: true with it in `frame`; false — wait for more, or, with `refusal`
	// set, the peer broke the protocol and the socket is to be closed with that code.
	bool Next(ibWebSocketFrame& frame, std::uint16_t& refusal);

private:
	std::string m_bytes;
	std::size_t m_at = 0;
	bool        m_masked;
	std::size_t m_limit;
};

// Sec-WebSocket-Accept for a Sec-WebSocket-Key (RFC 6455 § 4.2.2): the key and the protocol's GUID, SHA-1, base64.
std::string ibWebSocketAccept(const std::string& key);

// A fresh Sec-WebSocket-Key — sixteen random bytes, base64.
std::string ibWebSocketKey();

// True when `text` is well-formed UTF-8 — what a text message must be (RFC 6455 § 8.1).
bool ibIsUtf8(const std::string& text);

// ⭐ THE ENGINE'S TEXT AS A DEFLATED MESSAGE — what a peer that asked for it as it connected (?compress=deflate) is sent
// for a text of some size (ibWebSocket::Send): its bytes deflated, raw (RFC 1951 — what DecompressionStream
// ("deflate-raw") reads), in a binary message. False — not packed: it goes as text.
bool ibDeflate(const std::string& bytes, std::string& packed);

// …and the text such a message is — false when it does not inflate; and the packet one is (OES-RPC), its bytes.
CORE_API bool ibInflate(const std::string& packed, class ibString& text);
CORE_API bool ibInflate(const std::string& packed, class ibMemoryBuffer& bytes);

#endif
