/*
The MIT License (MIT)
Copyright (c) 2014 Graeme Hill (http://graemehill.ca)
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "guid.h"

#ifdef GUID_LIBUUID
#include <guid/guid.h>
#endif

#ifdef GUID_CFUUID
#include <CoreFoundation/CFUUID.h>
#endif

#ifdef GUID_WINDOWS
#include <objbase.h>
#endif

#include <array>
#include <iomanip>
#include <string_view>

// overload << so that it's easy to convert to a string
std::ostream& operator<<(std::ostream& s, const ibGuid& guid)
{
	std::ios_base::fmtflags f(s.flags()); // politely don't leave the ostream in hex mode
	s << std::hex << std::setfill('0')
		<< std::setw(2) << (int)guid._bytes[0]
		<< std::setw(2) << (int)guid._bytes[1]
		<< std::setw(2) << (int)guid._bytes[2]
		<< std::setw(2) << (int)guid._bytes[3]
		<< "-"
		<< std::setw(2) << (int)guid._bytes[4]
		<< std::setw(2) << (int)guid._bytes[5]
		<< "-"
		<< std::setw(2) << (int)guid._bytes[6]
		<< std::setw(2) << (int)guid._bytes[7]
		<< "-"
		<< std::setw(2) << (int)guid._bytes[8]
		<< std::setw(2) << (int)guid._bytes[9]
		<< "-"
		<< std::setw(2) << (int)guid._bytes[10]
		<< std::setw(2) << (int)guid._bytes[11]
		<< std::setw(2) << (int)guid._bytes[12]
		<< std::setw(2) << (int)guid._bytes[13]
		<< std::setw(2) << (int)guid._bytes[14]
		<< std::setw(2) << (int)guid._bytes[15];
	s.flags(f);
	return s;
}

bool operator<(const ibGuid& lhs, const ibGuid& rhs)
{
	return lhs.bytes() < rhs.bytes();
}

bool ibGuid::isValid() const
{
	ibGuid empty;
	return *this != empty;
}

template <typename I>
inline void hex_to_string(wxString& out, I& w, size_t hex_len = sizeof(I) << 1) {
	static const char* digits = "0123456789abcdef";
	for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
		out.append(digits[(w >> j) & 0x0f]);
}

// convert to string using std::snprintf() and std::string
wxString ibGuid::str() const
{
	wxString out; out.reserve(36);
	hex_to_string(out, _bytes[0]);
	hex_to_string(out, _bytes[1]);
	hex_to_string(out, _bytes[2]);
	hex_to_string(out, _bytes[3]);
	out.append('-');
	hex_to_string(out, _bytes[4]);
	hex_to_string(out, _bytes[5]);
	out.append('-');
	hex_to_string(out, _bytes[6]);
	hex_to_string(out, _bytes[7]);
	out.append('-');
	hex_to_string(out, _bytes[8]);
	hex_to_string(out, _bytes[9]);
	out.append('-');
	hex_to_string(out, _bytes[10]);
	hex_to_string(out, _bytes[11]);
	hex_to_string(out, _bytes[12]);
	hex_to_string(out, _bytes[13]);
	hex_to_string(out, _bytes[14]);
	hex_to_string(out, _bytes[15]);
	return out;
}

// conversion operator for ibGuidImpl
ibGuid::operator ibGuidImpl() const
{
	ibGuidImpl guid;
	guid.m_data1 = _bytes[0] << 24 ^ _bytes[1] << 16 ^ _bytes[2] << 8 ^ _bytes[3];
	guid.m_data2 = _bytes[4] << 8 ^ _bytes[5];
	guid.m_data3 = _bytes[6] << 8 ^ _bytes[7];
	guid.m_data4[0] = (unsigned char)_bytes[8];
	guid.m_data4[1] = (unsigned char)_bytes[9];
	guid.m_data4[2] = (unsigned char)_bytes[10];
	guid.m_data4[3] = (unsigned char)_bytes[11];
	guid.m_data4[4] = (unsigned char)_bytes[12];
	guid.m_data4[5] = (unsigned char)_bytes[13];
	guid.m_data4[6] = (unsigned char)_bytes[14];
	guid.m_data4[7] = (unsigned char)_bytes[15];
	return guid;
}

// Access underlying bytes
const std::array<unsigned char, 16>& ibGuid::bytes() const
{
	return _bytes;
}

// create a guid from vector of bytes
ibGuid::ibGuid(const std::array<unsigned char, 16>& bytes) : _bytes(bytes)
{
}

// create a guid from vector of bytes
ibGuid::ibGuid(const std::array<unsigned char, 16>&& bytes) : _bytes(std::move(bytes))
{
}

// converts a single hex char to a number (0 - 15)
inline unsigned char hexDigitToChar(char ch)
{
	// 0-9
	if (ch > 47 && ch < 58)
		return ch - 48;

	// a-f
	if (ch > 96 && ch < 103)
		return ch - 87;

	// A-F
	if (ch > 64 && ch < 71)
		return ch - 55;

	return 0;
}

inline bool isValidHexChar(char ch)
{
	// 0-9
	if (ch > 47 && ch < 58)
		return true;

	// a-f
	if (ch > 96 && ch < 103)
		return true;

	// A-F
	if (ch > 64 && ch < 71)
		return true;

	return false;
}

// converts the two hexadecimal characters to an unsigned char (a byte)
inline unsigned char hexPairToChar(char a, char b)
{
	return hexDigitToChar(a) * 16 + hexDigitToChar(b);
}

// create a guid from string
ibGuid::ibGuid(const std::string_view& fromString)
{
	char charOne = '\0';
	char charTwo = '\0';
	bool lookingForFirstChar = true;
	unsigned nextByte = 0;

	for (const char& ch : fromString)
	{
		if (ch == '-')
			continue;

		if (nextByte >= 16 || !isValidHexChar(ch))
		{
			// Invalid string so bail
			zeroify();
			return;
		}

		if (lookingForFirstChar)
		{
			charOne = ch;
			lookingForFirstChar = false;
		}
		else
		{
			charTwo = ch;
			auto byte = hexPairToChar(charOne, charTwo);
			_bytes[nextByte++] = byte;
			lookingForFirstChar = true;
		}
	}

	// if there were fewer than 16 bytes in the string then guid is bad
	if (nextByte < 16)
	{
		zeroify();
		return;
	}
}

#if __WXWINDOWS__
// create a guid from string
ibGuid::ibGuid(const wxString& fromString)
{
	char charOne = '\0';
	char charTwo = '\0';
	bool lookingForFirstChar = true;
	unsigned nextByte = 0;

	for (const wxUniChar& ch : fromString)
	{
		if (ch == '-')
			continue;

		if (nextByte >= 16 || !isValidHexChar(ch))
		{
			// Invalid string so bail
			zeroify();
			return;
		}

		if (lookingForFirstChar)
		{
			charOne = ch;
			lookingForFirstChar = false;
		}
		else
		{
			charTwo = ch;
			auto byte = hexPairToChar(charOne, charTwo);
			_bytes[nextByte++] = byte;
			lookingForFirstChar = true;
		}
	}

	// if there were fewer than 16 bytes in the string then guid is bad
	if (nextByte < 16)
	{
		zeroify();
		return;
	}
}
#endif

ibGuid::ibGuid(const ibGuidImpl& guid)
{
	_bytes = {
		(unsigned char)((guid.m_data1 >> 24) & 0xFF),
		(unsigned char)((guid.m_data1 >> 16) & 0xFF),
		(unsigned char)((guid.m_data1 >> 8) & 0xFF),
		(unsigned char)((guid.m_data1) & 0xff),

		(unsigned char)((guid.m_data2 >> 8) & 0xFF),
		(unsigned char)((guid.m_data2) & 0xff),

		(unsigned char)((guid.m_data3 >> 8) & 0xFF),
		(unsigned char)((guid.m_data3) & 0xFF),

		(unsigned char)guid.m_data4[0],
		(unsigned char)guid.m_data4[1],
		(unsigned char)guid.m_data4[2],
		(unsigned char)guid.m_data4[3],
		(unsigned char)guid.m_data4[4],
		(unsigned char)guid.m_data4[5],
		(unsigned char)guid.m_data4[6],
		(unsigned char)guid.m_data4[7]
	};
}

// create empty guid
ibGuid::ibGuid() : _bytes{ {0} }
{
}

// set all bytes to zero
void ibGuid::zeroify()
{
	std::fill(_bytes.begin(), _bytes.end(), static_cast<unsigned char>(0));
}

namespace {
// Compare two guids by their VALUE order — "is this guid greater or smaller". A GUID's first three fields
// (Data1 / Data2 / Data3) are stored little-endian, so a plain memcmp weighs their LOW byte first and gives a
// meaningless order. Compare byte-wise in the field-normalized (big-endian) sequence [3,2,1,0, 5,4, 7,6, 8..15]
// — the SAME byte order the stored _RRRef reference blob uses (identical field-swap), so an in-memory
// guid/reference sort matches server-side ORDER BY _RRRef. Fast: no copies/allocation, early-out on the first
// differing byte (Data1 usually decides in the first compare); the fixed Data4 tail rides one memcmp.
inline int guidValueCompare(const std::array<unsigned char, 16>& x, const std::array<unsigned char, 16>& y)
{
	static const unsigned char idx[8] = { 3, 2, 1, 0, 5, 4, 7, 6 };
	for (int i = 0; i < 8; ++i) {
		const unsigned char a = x[idx[i]], b = y[idx[i]];
		if (a != b) return a < b ? -1 : 1;
	}
	return std::memcmp(&x[8], &y[8], 8);   // Data4 (node) is already in byte order
}
}

bool ibGuid::operator > (const ibGuid& other) const
{
	return guidValueCompare(_bytes, other._bytes) > 0;
}

bool ibGuid::operator >= (const ibGuid& other) const
{
	return guidValueCompare(_bytes, other._bytes) >= 0;
}

bool ibGuid::operator < (const ibGuid& other) const
{
	return guidValueCompare(_bytes, other._bytes) < 0;
}

bool ibGuid::operator <= (const ibGuid& other) const
{
	return guidValueCompare(_bytes, other._bytes) <= 0;
}

// overload equality operator
bool ibGuid::operator==(const ibGuid& other) const
{
	return std::memcmp(_bytes.data(), other._bytes.data(), 16) == 0;
}

// overload inequality operator
bool ibGuid::operator!=(const ibGuid& other) const
{
	return !((*this) == other);
}

// member swap function
void ibGuid::swap(ibGuid& other)
{
	_bytes.swap(other._bytes);
}

// Platform entropy source. newGuid returns the STORAGE form (ibGuidImpl): it flows implicitly into an
// ibGuid, while the metaclass takes it directly and patches Data1 with the metaID (no intermediate
// ibGuid). Each body builds the canonical big-endian byte array and lets ibGuid -> ibGuidImpl (the
// field-swap in operator ibGuidImpl) produce native-endian fields. ibGuid stays a primitive: it mints
// guids, it does not know metaIDs.

#ifdef GUID_LIBUUID
// linux (libuuid); works anywhere libuuid is available
ibGuidImpl ibGuid::newGuid(short /*version*/)
{
	std::array<unsigned char, 16> data;
	static_assert(std::is_same<unsigned char[16], uuid_t>::value, "Wrong type!");
	uuid_generate(data.data());
	return ibGuid{ std::move(data) };
}
#endif

#ifdef GUID_CFUUID
// mac / ios
ibGuidImpl ibGuid::newGuid(short /*version*/)
{
	auto newId = CFUUIDCreate(nullptr);
	auto bytes = CFUUIDGetUUIDBytes(newId);
	CFRelease(newId);

	std::array<unsigned char, 16> byteArray =
	{ {
		bytes.byte0,
		bytes.byte1,
		bytes.byte2,
		bytes.byte3,
		bytes.byte4,
		bytes.byte5,
		bytes.byte6,
		bytes.byte7,
		bytes.byte8,
		bytes.byte9,
		bytes.byte10,
		bytes.byte11,
		bytes.byte12,
		bytes.byte13,
		bytes.byte14,
		bytes.byte15
	} };
	return ibGuid{ std::move(byteArray) };
}
#endif

#ifdef GUID_WINDOWS
// windows
ibGuidImpl ibGuid::newGuid(short version)
{
	GUID newId = { 0 };
	if (version == GUID_TIME_BASED)
		::UuidCreateSequential(&newId);
	else if (version == GUID_RANDOM)
		::UuidCreate(&newId);

	std::array<unsigned char, 16> bytes =
	{
		(unsigned char)((newId.Data1 >> 24) & 0xFF),
		(unsigned char)((newId.Data1 >> 16) & 0xFF),
		(unsigned char)((newId.Data1 >> 8) & 0xFF),
		(unsigned char)((newId.Data1) & 0xff),

		(unsigned char)((newId.Data2 >> 8) & 0xFF),
		(unsigned char)((newId.Data2) & 0xff),

		(unsigned char)((newId.Data3 >> 8) & 0xFF),
		(unsigned char)((newId.Data3) & 0xFF),

		(unsigned char)newId.Data4[0],
		(unsigned char)newId.Data4[1],
		(unsigned char)newId.Data4[2],
		(unsigned char)newId.Data4[3],
		(unsigned char)newId.Data4[4],
		(unsigned char)newId.Data4[5],
		(unsigned char)newId.Data4[6],
		(unsigned char)newId.Data4[7]
	};
	ibGuid guid(std::move(bytes));       // explicit array ctor
	return guid;                          // ibGuid -> ibGuidImpl via operator ibGuidImpl (field-swap)
}
#endif

#ifdef GUID_WINDOWS
#pragma comment( lib, "rpcrt4.lib" )
#endif