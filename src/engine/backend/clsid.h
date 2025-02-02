#ifndef _CLSID_H__
#define _CLSID_H__

typedef unsigned wxLongLong_t class_identifier_t;

//*******************************************************************************************
//*                                 Clsid support											*
//*******************************************************************************************

#define MK_CLSID(a,b,c,d,e,f,g,h) \
    	class_identifier_t((class_identifier_t(a)<<class_identifier_t(56))|(class_identifier_t(b)<<class_identifier_t(48))|(class_identifier_t(c)<<class_identifier_t(40))|(class_identifier_t(d)<<class_identifier_t(32))|(class_identifier_t(e)<<class_identifier_t(24))|(class_identifier_t(f)<<class_identifier_t(16))|(class_identifier_t(g)<<class_identifier_t(8))|(class_identifier_t(h)))

#define MK_CLSID_INV(a,b,c,d,e,f,g,h) MK_CLSID(h,g,f,e,d,c,b,a)

inline wxString clsid_to_string(const class_identifier_t& clsid) {
	if (clsid != 0) {
		return std::initializer_list{
			char((clsid >> 56) & 0xff),
			char((clsid >> 48) & 0xff),
			char((clsid >> 40) & 0xff),
			char((clsid >> 32) & 0xff),
			char((clsid >> 24) & 0xff),
			char((clsid >> 16) & 0xff),
			char((clsid >> 8) & 0xff),
			char((clsid >> 0) & 0xff)
		};
	}
	return wxEmptyString;
}

inline class_identifier_t string_to_clsid(const wxString& buf) {

	const size_t length = buf.length();
	wxASSERT(length < 9);
	if (length > 0) {
		return MK_CLSID(
			length > 0 ? (char)buf.at(0) : ' ', length > 1 ? (char)buf.at(1) : ' ',
			length > 2 ? (char)buf.at(2) : ' ', length > 3 ? (char)buf.at(3) : ' ',
			length > 4 ? (char)buf.at(4) : ' ', length > 5 ? (char)buf.at(5) : ' ',
			length > 6 ? (char)buf.at(6) : ' ', length > 7 ? (char)buf.at(7) : ' '
		);
	}
	return 0;
}

#endif