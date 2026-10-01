#ifndef __DATABASE_STRING_CONVERTER_H__
#define __DATABASE_STRING_CONVERTER_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "databaseLayerDef.h"

// ⭐⭐ THE WIRE IS UTF-8, BOTH WAYS, FOR EVERY DRIVER — and nothing here is a setting.
//
// This class used to carry an ENCODING: a wxCSConv copied into every statement, parameter and result
// set as it was made, under one mutex for the whole process. The conversions stopped reading it long
// ago — every one of them already went through UTF-8 — but the copying stayed, a lock and a converter
// built on the heap per statement for nobody (Max, 2026-09-12: "it used to be read and copied; now it
// is only copied"). The state is gone, and what is left are the two directions of UTF-8, each made in
// one pass over the text.
//
// ⭐ BOTH TEXTS, ONE CODEC. A value's text is an ibString, and a field read into a value or a value's
// text bound as a parameter crosses as one with no wxString made on the way; the statement's own SQL,
// a column's name and a server's message stay wxString. The same one-pass codec serves both.
class BACKEND_API ibDatabaseStringConverter
{
public:
	virtual ~ibDatabaseStringConverter() = default;

	static const wxCharBuffer ConvertToUnicodeStream(const wxString& inputString);
	static const wxCharBuffer ConvertToUnicodeStream(const ibString& inputString);
	static unsigned int GetEncodedStreamLength(const wxString& inputString);
	static unsigned int GetEncodedStreamLength(const ibString& inputString);
	static wxString ConvertFromUnicodeStream(const char* inputBuffer);
	static void ConvertFromUnicodeStream(const char* inputBuffer, ibString& outputString);
};

#endif // __DATABASE_STRING_CONVERTER_H__
