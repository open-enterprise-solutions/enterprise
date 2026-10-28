/////////////////////////////////////////////////////////////////////////////
// SHA-256 implementation per FIPS 180-4.
// Straight transliteration of the spec — no tricks, easy to audit.
/////////////////////////////////////////////////////////////////////////////

#include "sha256.hpp"
#include <cstring>

namespace {

constexpr uint32_t K[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

} // namespace

ibSHA256::ibSHA256() : m_bitLen(0), m_bufLen(0)
{
	m_state[0] = 0x6a09e667u; m_state[1] = 0xbb67ae85u;
	m_state[2] = 0x3c6ef372u; m_state[3] = 0xa54ff53au;
	m_state[4] = 0x510e527fu; m_state[5] = 0x9b05688cu;
	m_state[6] = 0x1f83d9abu; m_state[7] = 0x5be0cd19u;
}

void ibSHA256::Transform(const uint8_t* block)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++) {
		w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16)
			| (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
	}
	for (int i = 16; i < 64; i++) {
		const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
	uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

	for (int i = 0; i < 64; i++) {
		const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		const uint32_t ch = (e & f) ^ (~e & g);
		const uint32_t t1 = h + S1 + ch + K[i] + w[i];
		const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
		const uint32_t t2 = S0 + mj;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
	m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
}

void ibSHA256::Update(const uint8_t* data, size_t len)
{
	while (len > 0) {
		const size_t room = BLOCK_SIZE - m_bufLen;
		const size_t take = len < room ? len : room;
		memcpy(m_buffer + m_bufLen, data, take);
		m_bufLen += take;
		data += take;
		len -= take;
		m_bitLen += take * 8;
		if (m_bufLen == BLOCK_SIZE) {
			Transform(m_buffer);
			m_bufLen = 0;
		}
	}
}

void ibSHA256::Final(uint8_t out[DIGEST_SIZE])
{
	const uint64_t totalBits = m_bitLen;
	m_buffer[m_bufLen++] = 0x80;
	if (m_bufLen > BLOCK_SIZE - 8) {
		memset(m_buffer + m_bufLen, 0, BLOCK_SIZE - m_bufLen);
		Transform(m_buffer);
		m_bufLen = 0;
	}
	memset(m_buffer + m_bufLen, 0, BLOCK_SIZE - 8 - m_bufLen);
	for (int i = 0; i < 8; i++)
		m_buffer[BLOCK_SIZE - 1 - i] = uint8_t(totalBits >> (i * 8));
	Transform(m_buffer);

	for (int i = 0; i < 8; i++) {
		out[i * 4]     = uint8_t(m_state[i] >> 24);
		out[i * 4 + 1] = uint8_t(m_state[i] >> 16);
		out[i * 4 + 2] = uint8_t(m_state[i] >> 8);
		out[i * 4 + 3] = uint8_t(m_state[i]);
	}
	// wipe state
	memset(m_state, 0, sizeof(m_state));
	memset(m_buffer, 0, sizeof(m_buffer));
	m_bitLen = 0; m_bufLen = 0;
}

void ibSHA256::Hash(const uint8_t* data, size_t len, uint8_t out[DIGEST_SIZE])
{
	ibSHA256 h;
	h.Update(data, len);
	h.Final(out);
}
