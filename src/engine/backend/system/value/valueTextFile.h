#ifndef __VALUE_TEXT_FILE_H__
#define __VALUE_TEXT_FILE_H__

#include "backend/compiler/value.h"
#include "backend/system/systemEnum.h"

#include <wx/buffer.h>

#include <memory>

class wxFFile;
class wxMBConv;

// ⭐⭐ THE TEXT OF A FILE, READ AND WRITTEN BY A SCRIPT — two types, as the reference system has them:
// a reader and a writer, each holding ONE open file.
//
// Until now a script could ask whether a file exists and how large it is (File) and could do nothing with
// what is in it: sales out of a cash-register system, an exchange file of another accounting system, a
// price list — every import went through ComObject("ADODB.Stream"), which is Windows and nowhere else.
//
//     var reader = New TextReader("C:\in\sales.csv", TextEncoding.UTF8);
//     var line = reader.ReadLine();
//     while (line <> Undefined) { … line = reader.ReadLine(); }
//     reader.Close();
//
// ⚠ THE END OF THE FILE IS `Undefined`, NOT AN EMPTY STRING. An empty line is a line; a file ends where
// there is nothing more to hand over, and the two must not look alike to a `while`.
//
// The encoding is said with the TextEncoding enumeration, UTF-8 when left out. A byte-order mark at the
// head of a UTF-8 or UTF-16 file is read as what it is — a mark — and never handed over as text.

BACKEND_API std::unique_ptr<wxMBConv> ibCreateTextConv(ibTextEncoding encoding);

void ibValueTextReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueTextReader : public ibValueStaticMembers<&ibValueTextReader_BindNames> {
	enum Func {
		enOpen,
		enReadLine,
		enRead,
		enClose,
	};
public:

	ibValueTextReader();
	virtual ~ibValueTextReader();

	virtual bool IsEmpty() const { return !IsOpen(); }

	virtual bool Init() { return true; }                                    // New TextReader() — opened later, by Open
	virtual bool Init(ibValue** paParams, const long lSizeArray);           // New TextReader(path [, encoding])

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	// The same verbs for a caller in C++ (and for the tests).
	void Open(const wxString& fileName, ibTextEncoding encoding = ibTextEncoding_UTF8);
	bool ReadLine(wxString& line);     // false at the end of the file
	wxString ReadRest();
	void Close();
	bool IsOpen() const { return m_loaded; }

private:
	// The whole file is decoded once, when it is opened: a text file a script reads line by line is an
	// exchange file, not a log of gigabytes, and one decode is what makes every encoding — a UTF-16 pair, a
	// multi-byte sequence split across a buffer's edge — come out whole.
	wxString m_text;
	size_t   m_at = 0;
	bool     m_loaded = false;
};

void ibValueTextWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueTextWriter : public ibValueStaticMembers<&ibValueTextWriter_BindNames> {
	enum Func {
		enOpen,
		enWrite,
		enWriteLine,
		enClose,
	};
public:

	ibValueTextWriter();
	virtual ~ibValueTextWriter();

	virtual bool IsEmpty() const { return !IsOpen(); }

	virtual bool Init() { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray);           // New TextWriter(path [, encoding [, append]])

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	void Open(const wxString& fileName, ibTextEncoding encoding = ibTextEncoding_UTF8, bool append = false);
	void Write(const wxString& text);
	void Close();
	bool IsOpen() const;

private:
	std::unique_ptr<wxFFile>  m_file;
	std::unique_ptr<wxMBConv> m_conv;
	wxString                  m_fileName;
};

#endif // !__VALUE_TEXT_FILE_H__
