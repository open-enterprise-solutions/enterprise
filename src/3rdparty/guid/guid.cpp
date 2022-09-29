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

// overload << so that it's easy to convert to a string
std::ostream &operator<<(std::ostream &s, const Guid &guid)
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

bool operator<(const Guid &lhs, const Guid &rhs)
{
	return lhs.bytes() < rhs.bytes();
}

bool Guid::isValid() const
{
	Guid empty;
	return *this != empty;
}

// convert to string using std::snprintf() and std::string
std::string  Guid::str() const
{
	char one[10], two[6], three[6], four[6], five[14];

	wxSnprintf(one, 10, "%02x%02x%02x%02x",
		_bytes[0], _bytes[1], _bytes[2], _bytes[3]);
	wxSnprintf(two, 6, "%02x%02x",
		_bytes[4], _bytes[5]);
	wxSnprintf(three, 6, "%02x%02x",
		_bytes[6], _bytes[7]);
	wxSnprintf(four, 6, "%02x%02x",
		_bytes[8], _bytes[9]);
	wxSnprintf(five, 14, "%02x%02x%02x%02x%02x%02x",
		_bytes[10], _bytes[11], _bytes[12], _bytes[13], _bytes[14], _bytes[15]);
	const std::string  sep("-");
	std::string out(one);

	out += sep + two;
	out += sep + three;
	out += sep + four;
	out += sep + five;

	return out;
}

// conversion operator for guid_t
Guid::operator guid_t() const
{
	guid_t guid;
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

// conversion operator for wxString
Guid::operator std::string() const
{
	return str();
}

// Access underlying bytes
const std::array<unsigned char, 16>& Guid::bytes() const
{
	return _bytes;
}

// create a guid from vector of bytes
Guid::Guid(const std::array<unsigned char, 16> &bytes) : _bytes(bytes)
{ }

// create a guid from vector of bytes
Guid::Guid(const std::array<unsigned char, 16> &&bytes) : _bytes(std::move(bytes))
{ }

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
Guid::Guid(const std::string_view &fromString)
{
	char charOne = '\0';
	char charTwo = '\0';
	bool lookingForFirstChar = true;
	unsigned nextByte = 0;

	for (const char &ch : fromString)
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

// create a guid from string
Guid::Guid(const wxString &fromString)
{
	char charOne = '\0';
	char charTwo = '\0';
	bool lookingForFirstChar = true;
	unsigned nextByte = 0;

	for (const char &ch : fromString)
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

Guid::Guid(const guid_t &guid)
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
Guid::Guid() : _bytes{ {0} }
{ }

// set all bytes to zero
void Guid::zeroify()
{
	std::fill(_bytes.begin(), _bytes.end(), static_cast<unsigned char>(0));
}

bool Guid::operator > (const Guid &other) const
{
	return _bytes > other._bytes;
}

bool Guid::operator >= (const Guid &other) const
{
	return _bytes >= other._bytes;
}

bool Guid::operator < (const Guid &other) const
{
	return _bytes < other._bytes;
}

bool Guid::operator <= (const Guid &other) const
{
	return _bytes <= other._bytes;
}

// overload equality operator
bool Guid::operator==(const Guid &other) const
{
	return _bytes == other._bytes;
}

// overload inequality operator
bool Guid::operator!=(const Guid &other) const
{
	return !((*this) == other);
}

// member swap function
void Guid::swap(Guid &other)
{
	_bytes.swap(other._bytes);
}

// This is the linux friendly implementation, but it could work on other
// systems that have libuuid available
#ifdef GUID_LIBUUID
Guid Guid::newGuid()
{
	std::array<unsigned char, 16> data;
	static_assert(std::is_same<unsigned char[16], uuid_t>::value, "Wrong type!");
	uuid_generate(data.data());
	return Guid{ std::move(data) };
}
#endif

// this is the mac and ios version
#ifdef GUID_CFUUID
Guid Guid::newGuid()
{
	auto newId = CFUUIDCreate(NULL);
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
	return Guid{ std::move(byteArray) };
}
#endif

// obviously this is the windows version
#ifdef GUID_WINDOWS
Guid Guid::newGuid()
{
	GUID newId;
	::CoCreateGuid(&newId);

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

	return Guid{ std::move(bytes) };
}
#endif