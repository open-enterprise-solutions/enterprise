#ifndef _VALUE_BINARY_DATA_H__
#define _VALUE_BINARY_DATA_H__

#include "backend/compiler/value.h"

#include <wx/buffer.h>

// ⭐⭐ BYTES ARE A TYPE, NOT A STRING THAT HAPPENS TO HOLD THEM.
//
// A blob read off a cursor had nowhere to land: `ibValue` knew primitives and objects, and binary
// content was smuggled through TYPE_STRING — which reads back as text, compares as text, and quietly
// mangles anything that is not valid in the string's encoding. The row key had the same shape of
// problem and its own answer already (ibValueGuid); this is the same answer for content that is not
// an identity: hold the bytes, say what they are, and let the TEXT be a projection of them rather
// than their storage.
//
// Built by analogy with ibValueGuid, deliberately: one file pair, one registration, the same
// value-type surface. Base64 is the text form — chosen because it is what the JSON provider already
// writes binaries as, so a value crossing to text and back keeps its bytes.
//
// ⭐ …AND THE BYTES CAN COME FROM A FILE AND GO TO ONE. `Read(path)` takes the file's content whole, `Write(path)`
// puts the content there, `Size()` says how much there is. A constructor from a PATH is deliberately not
// offered: a string handed to the constructor already means base64, and the same argument meaning two
// things is a file that silently fails to decode.
void ibValueBinaryData_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueBinaryData : public ibValueStaticMembers<&ibValueBinaryData_BindNames> {
	enum Func {
		enSize,
		enRead,
		enWrite,
	};
public:

	operator wxMemoryBuffer() const {
		return m_data;
	}

	ibValueBinaryData();
	ibValueBinaryData(const wxMemoryBuffer& data);
	ibValueBinaryData(const void* data, size_t length);

	// The bytes themselves — for a caller that stores or writes them (the codec, the wire).
	const wxMemoryBuffer& GetBuffer() const { return m_data; }
	size_t GetLength() const { return m_data.GetDataLen(); }

	virtual bool Init();
	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	// The same verbs for a caller in C++.
	void ReadFile(const wxString& fileName);
	void WriteFile(const wxString& fileName) const;

	// A file's bytes, whole - the one place a file is read into memory (TextReader decodes what this hands
	// over). `who` opens the refusal, so the message names the type the script was talking to.
	static void ReadWholeFile(const wxString& fileName, const wxString& who, wxMemoryBuffer& bytes);

	// The TEXT projection: base64. Not the storage — GetBuffer is.
	virtual wxString GetString() const;

	//check is empty
	virtual bool IsEmpty() const {
		return m_data.GetDataLen() == 0;
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibValue& cParam) const;

	//operator '!='
	virtual bool CompareValueNE(const ibValue& cParam) const;

private:
	wxMemoryBuffer m_data;
};

#endif // !_VALUE_BINARY_DATA_H__
