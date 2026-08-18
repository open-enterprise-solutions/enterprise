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
class BACKEND_API ibValueBinaryData : public ibValue {
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
