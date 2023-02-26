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

#ifndef _GUID_H__
#define _GUID_H__

#include <array>
#include <wx/wx.h>

struct guid_t  // UUID = GUID = CLSID = LIBID = IID
{
	unsigned long   m_data1;
	unsigned short  m_data2;
	unsigned short  m_data3;
	unsigned char   m_data4[8];
};

#if defined(__WXMSW__)
#define GUID_WINDOWS
#elif defined(__WXGTK__)
#define GUID_LIBUUID
#elif defined(__WXOSX__)
#define GUID_CFUUID
#endif

#define GUID_TIME_BASED 1
#define GUID_DCE_SECURITY 2
#define GUID_NAME_BASED_MD5 3
#define GUID_RANDOM 4
#define GUID_NAME_BASED_SHA1 5

// Class to represent a GUID/UUID. Each instance acts as a wrapper around a
// 16 byte value that can be passed around by value. It also supports
// conversion to string (via the stream operator <<) and conversion from a
// string via constructor.
class Guid
{
public:

	explicit Guid(const std::array<unsigned char, 16>& bytes);
	explicit Guid(const std::array<unsigned char, 16>&& bytes);
	explicit Guid(const std::string_view& fromString);

	Guid();
	Guid(const guid_t& bytes);
	Guid(const wxString& fromString);

	Guid(const Guid& other) = default;
	Guid& operator=(const Guid& other) = default;
	Guid(Guid&& other) = default;
	Guid& operator=(Guid&& other) = default;

	static Guid newGuid(int version = GUID_RANDOM);

	bool operator > (const Guid& other) const;
	bool operator >= (const Guid& other) const;
	bool operator < (const Guid& other) const;
	bool operator <= (const Guid& other) const;

	// overload equality and inequality operator
	bool operator==(const Guid& other) const;
	bool operator!=(const Guid& other) const;

	operator guid_t() const;

	std::string str() const;
	operator std::string() const;

	const std::array<unsigned char, 16>& bytes() const;
	void swap(Guid& other);
	bool isValid() const;

	void reset() { zeroify(); }

private:

	void zeroify();

	// actual data
	std::array<unsigned char, 16> _bytes;

	// make the << operator a friend so it can access _bytes
	friend std::ostream& operator<<(std::ostream& s, const Guid& guid);
};

#define wxNullGuid	Guid()
#define wxNewUniqueGuid	Guid::newGuid(GUID_RANDOM)

#endif