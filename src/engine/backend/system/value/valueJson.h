#ifndef __VALUE_JSON_H__
#define __VALUE_JSON_H__

#include "backend/compiler/value.h"
#include "backend/system/systemEnum.h"

#include <string>
#include <vector>

// ⭐⭐ JSON, READ AND WRITTEN BY A SCRIPT — two types, reading apart from writing, and the kinds of things said
// with system enumerations rather than numbers or strings.
//
// The engine has spoken JSON for a long time - the assistant's door does, the configuration's diff view does -
// and a script could not: an exchange with a cash register, a marketplace, a bank was a COM object or nothing.
//
// THE WHOLE VALUE, when the document is small enough to hold - which an exchange message is:
//
//     var reader = New JSONReader(text);
//     var order = reader.ReadValue();                      // Structure / Array / String / Number / Boolean / Null
//     Message(order.customer.name + ": " + order.lines.Count());
//
// …OR TOKEN BY TOKEN, when only a part is wanted:
//
//     var reader = New JSONReader(text);
//     while (reader.Read()) {
//         if (reader.CurrentValueType = JSONValueType.PropertyName And reader.CurrentValue = "lines") {
//             lines = reader.ReadValue();                  // the value that FOLLOWS, whole
//         }
//     }
//
// ⚠ A NUMBER IS READ FROM ITS TEXT, NOT THROUGH A double. `0.1` in a message is one tenth, and money that came
// through a binary fraction is money that no longer adds up. Every digit arrives as it was written - within
// limits that are said rather than met by surprise: the parser itself refuses a magnitude past a double's range
// (about 1e308), and a token longer than a thousand characters or with an exponent beyond a thousand is
// refused here, because reading it costs what no token from outside is allowed to cost. Refused, never rounded.
//
// An object becomes a Structure: its keys may be any strings and keep the order they came in (`s.name` for a
// key that is a name, `s["odd key"]` for one that is not). A key met twice keeps its LAST value, as every
// other reader of JSON does. `null` is Null.
//
// ⚠ ONE THING A Structure CANNOT HOLD: two keys that differ only in case. Its keys fold case and JSON's do not,
// so `{"ab12":1,"AB12":2}` read whole would come out as ONE member - a quiet loss. ReadValue() REFUSES such an
// object, naming both keys; walked token by token it reads like any other.
//
// The text is a string. A file is the business of whatever reads files: read it, then hand the text over.

void ibValueJsonReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueJsonReader : public ibValueStaticMembers<&ibValueJsonReader_BindNames> {
	enum Prop {
		enCurrentValueType,
		enCurrentValue,
	};
	enum Func {
		enSetString,
		enRead,
		enSkip,
		enReadValue,
		enClose,
	};
public:

	struct ibToken {
		ibJsonValueType m_type = ibJsonValueType_None;
		ibValue         m_value;      // Boolean / Number / String / PropertyName carry one; the rest do not
	};

	ibValueJsonReader();
	virtual ~ibValueJsonReader();

	virtual bool IsEmpty() const { return !m_loaded; }

	virtual bool Init() { return true; }                                    // New JSONReader() — the text is given later
	virtual bool Init(ibValue** paParams, const long lSizeArray);           // New JSONReader(text)

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	// The same verbs for a caller in C++ (and for the tests).
	void SetText(const wxString& text);      // the script's SetString (ibValue has a SetString of its own); raises on text that is not JSON
	bool Read();                               // false when nothing is left
	void Skip();                               // a container, or a property with its value, as one step

	// The whole value the reader STANDS ON. Standing on a property name - or before the first token, or on a
	// value it has already handed over - it is the value that FOLLOWS. So a member's value is `ReadValue()`
	// right after its name, the whole document is `ReadValue()` on a fresh reader, and the elements of an array
	// are taken one at a time with no exception for a stop sign:
	//     reader.Read();                                                   // [
	//     while (reader.Read() And reader.CurrentValueType <> JSONValueType.ArrayEnd) { item = reader.ReadValue(); }
	ibValue ReadValue();
	void Close();

	ibJsonValueType CurrentType() const;
	ibValue CurrentValue() const;

private:
	ibValue BuildValue(int depth);             // from the current token to the end of the value it opens

	// What CurrentValueType answers, made once per kind: a fresh enumeration object per token is dozens of
	// allocations in the loop a walk IS (`while (reader.Read()) { if (reader.CurrentValueType = ...`).
	ibValue m_typeValues[ibJsonValueType_ArrayEnd + 1];

	// The text is parsed once, when it is given: the tokens below are what Read() walks. A message that does
	// not parse is refused THERE, whole - not after a script has acted on its first half.
	std::vector<ibToken> m_tokens;
	size_t m_at = 0;
	bool   m_started = false;
	bool   m_taken = false;                    // the value under the reader has been handed over by ReadValue()
	bool   m_loaded = false;
};

// A number token as the parser hands it over -> the number, or why not. Apart from the reader so that what it
// does to the token can be asked directly (the locale's decimal point, above all: a suite cannot count on a
// comma locale being installed where it runs).
BACKEND_API bool ibJsonNumberFromToken(const std::string& token, ibNumber& number, wxString& refusal);

// ⭐ …AND WRITTEN. The writer keeps the document well-formed by itself: a value inside an object needs its name
// first, an end closes what was opened, a document holds one value, and Close() refuses a text with anything
// left open - so what it hands over always parses.
//
//     var writer = New JSONWriter(JSONFormatting.Indented);
//     writer.WriteStartObject();
//     writer.WritePropertyName("number");  writer.WriteValue(doc.Number);
//     writer.WritePropertyName("lines");   writer.WriteValue(lines);      // an Array of Structures, whole
//     writer.WriteEndObject();
//     var text = writer.Close();
//
// WriteValue takes a String, a Number (written exactly, never through a double), a Boolean, a Date (ISO 8601,
// `2026-09-20T21:05:39`), Null and Undefined (`null`), and a Structure / Container / Array of those to any depth.
// Anything else - a reference, a table - has no JSON form of its own and is refused by name: what goes out in
// its place (a code, a description, a Guid) is the exchange's decision, not the writer's.

void ibValueJsonWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueJsonWriter : public ibValueStaticMembers<&ibValueJsonWriter_BindNames> {
	enum Func {
		enWriteStartObject,
		enWriteEndObject,
		enWriteStartArray,
		enWriteEndArray,
		enWritePropertyName,
		enWriteValue,
		enClose,
	};
public:

	ibValueJsonWriter();
	virtual ~ibValueJsonWriter();

	virtual bool Init() { return true; }                                    // New JSONWriter() — compact
	virtual bool Init(ibValue** paParams, const long lSizeArray);           // New JSONWriter(JSONFormatting.X)

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	void SetFormatting(ibJsonFormatting formatting) { m_formatting = formatting; }
	void WriteStartObject();
	void WriteEndObject();
	void WriteStartArray();
	void WriteEndArray();
	void WritePropertyName(const wxString& name);
	void WriteValue(const ibValue& value);
	wxString Close();                          // the text; raises while anything is still open

private:
	struct ibFrame {
		bool m_object = false;
		bool m_hasMembers = false;
		bool m_nameWritten = false;            // an object's member: the name is out, the value is owed
	};

	void BeginValue();                         // what stands before ANY value: the comma, the line, the rules
	void EndContainer(bool object);
	void NewLine(size_t depth);
	void WriteAny(const ibValue& value, int depth);

	std::vector<ibFrame> m_open;
	wxString             m_text;
	ibJsonFormatting     m_formatting = ibJsonFormatting_Compact;
	bool                 m_rootWritten = false;
};

#endif // !__VALUE_JSON_H__
