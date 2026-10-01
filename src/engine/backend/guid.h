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

#ifndef __GUID_H__
#define __GUID_H__

#include <array>
#include <cstdint>
#include <wx/wx.h>

struct ibGuidImpl { // UUID = GUID = CLSID = LIBID = IID
	uint32_t        m_data1;   // fixed-width, NOT `unsigned long`: that is 64-bit on LP64 (Linux/macOS),
	unsigned short  m_data2;   // which would make this struct 20 bytes there and desync the persisted
	unsigned short  m_data3;   // 16-byte _RRRef key. Pinned so ibGuidImpl behaves identically on every
	unsigned char   m_data4[8];// platform — this is the storage dupe of a guid. All four fields are pure
};                             // identity (a reference key carries no type; the type is the _RTRef column).
static_assert(sizeof(ibGuidImpl) == 16, "ibGuidImpl must stay a portable 16-byte POD (guid storage dupe / _RRRef key)");

#if defined(__WXMSW__)
#define GUID_WINDOWS
#elif defined(__WXGTK__)
#define GUID_LIBUUID
#elif defined(__WXOSX__)
#define GUID_CFUUID
#endif

#include "backend/backend.h"

#define GUID_TIME_BASED 1
#define GUID_DCE_SECURITY 2
#define GUID_NAME_BASED_MD5 3
#define GUID_RANDOM 4
#define GUID_NAME_BASED_SHA1 5

// Class to represent a GUID/UUID. Each instance acts as a wrapper around a
// 16 byte value that can be passed around by value. It also supports
// conversion to string (via the stream operator <<) and conversion from a
// string via constructor.
class BACKEND_API ibGuid
{
public:

	// EXPLICIT: wrapping raw bytes into a guid is a deliberate act (`ibGuid g(ibGuid::newGuid());`). The
	// IMPLICIT conversion is reserved for ibGuidImpl (the storage dupe, ctor below), so an ibGuidImpl flows
	// into an ibGuid freely while a bare array does not.
	explicit ibGuid(const std::array<unsigned char, 16>& bytes);
	explicit ibGuid(const std::array<unsigned char, 16>&& bytes);
	explicit ibGuid(const std::string_view& fromString);

	ibGuid();
	ibGuid(const ibGuidImpl& bytes);   // IMPLICIT on purpose: the storage dupe flows into an ibGuid freely
	                                   // (the metaclass patches Data1 on an ibGuidImpl, then returns it as a guid)
#if __WXWINDOWS__
	ibGuid(const wxString& fromString);
#endif

	ibGuid(const ibGuid& other) = default;
	ibGuid& operator=(const ibGuid& other) = default;
	ibGuid(ibGuid&& other) = default;
	ibGuid& operator=(ibGuid&& other) = default;

	// Mint a fresh guid in its STORAGE form (ibGuidImpl, the dupe): it flows implicitly into an ibGuid
	// (`ibGuid g = ibGuid::newGuid();`). A reference key is just this guid — pure identity, no type baked in
	// (the type is the _RTRef column); ibGuid stays a primitive that mints guids, it does not know metaIDs.
	static ibGuidImpl newGuid(short version = GUID_RANDOM);

	bool operator > (const ibGuid& other) const;
	bool operator >= (const ibGuid& other) const;
	bool operator < (const ibGuid& other) const;
	bool operator <= (const ibGuid& other) const;

	// overload equality and inequality operator
	bool operator==(const ibGuid& other) const;
	bool operator!=(const ibGuid& other) const;

	operator ibGuidImpl() const;

	// convert to string using std::snprintf() and wxString
	wxString str() const;

	// conversion operator for string
	operator wxString() const { return str(); }

	const std::array<unsigned char, 16>& bytes() const;
	void swap(ibGuid& other);
	bool isValid() const;

	void reset() { zeroify(); }

private:

	void zeroify();

	// actual data
	std::array<unsigned char, 16> _bytes;

	// make the << operator a friend so it can access _bytes
	// BACKEND_API: exported so consumers (tools / gtest diagnostics) can stream
	// an ibGuid across the DLL boundary.
	friend BACKEND_API std::ostream& operator<<(std::ostream& s, const ibGuid& guid);
};

#define wxNullGuid	ibGuid()
// Wrapped so wxNewUniqueGuid is an ibGuid (newGuid returns ibGuidImpl): several call sites do
// `ibUniqueKey k = wxNewUniqueGuid`, and ibUniqueKey <- ibGuidImpl would need TWO user conversions
// (ibGuidImpl -> ibGuid -> ibUniqueKey), which copy-init forbids. Invisible at the call site.
#define wxNewUniqueGuid	ibGuid(ibGuid::newGuid(GUID_RANDOM))

#endif