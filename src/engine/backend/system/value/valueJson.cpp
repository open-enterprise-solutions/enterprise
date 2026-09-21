////////////////////////////////////////////////////////////////////////////
//	Description : JSON read and written by a script
////////////////////////////////////////////////////////////////////////////

#include "valueJson.h"

#include "backend/backend_exception.h"
#include "backend/serialize/jsonText.h"            // ibJsonText — how a string is spelled in JSON, said once
#include "backend/compiler/enumUnit.h"            // ConvertToEnumValue<> is declared in value.h and DEFINED here
#include "backend/system/systemManagerEnum.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueMap.h"

#include "3rdparty/nlohmann/json.hpp"

#include <algorithm>   // std::min
#include <cstddef>     // std::size_t
#include <cstdlib>     // std::strtol
#include <exception>
#include <string>
#include <utility>     // std::move, std::pair

namespace {

// How deep a value may nest before it is refused. The parser walks any depth without a stack of its own; what
// BUILDS the value and what WRITES one recurse, and a message of a hundred thousand `[` is an attack on that
// stack, not a document. No exchange message comes near this.
constexpr int kMaxDepth = 256;

// …and how far a number's exponent may reach. `1e-999999` is nine characters, is accepted exactly - and the
// first `qty + 1` on it aligns two numbers a million digits apart: many seconds of arithmetic for one token that
// came from outside. A double ends at 308; nothing an exchange carries comes near a thousand.
constexpr long kMaxExponent = 1000;

// …and how long a number may be. ibNumber reads a decimal text digit by digit, each digit a pass over all the
// limbs so far: a 300 KB `0.1111…` is a finite double, so the parser lets it through - and is tens of seconds
// of multiplication. Nothing an exchange carries is a thousand characters of one number.
constexpr size_t kMaxNumberLength = 1000;

// What the parser says about a text, as something a person can read. Its message quotes the bytes it stopped
// on, and stopped in the MIDDLE of a character (`{"name": Борщ}`) those are not UTF-8 - decoded strictly the
// whole sentence, line and column included, came out EMPTY. The sentence is ASCII; what is not becomes `?`.
wxString ibJsonParserText(const char* what)
{
	if (what == nullptr)
		return wxString();
	const wxString strict = wxString::FromUTF8(what);
	if (!strict.empty() || *what == '\0')
		return strict;
	std::string ascii(what);
	for (char& c : ascii)
		if (static_cast<unsigned char>(c) >= 0x80)
			c = '?';
	return wxString::FromAscii(ascii.c_str());
}

// ⭐ THE PARSER'S EVENTS BECOME THE READER'S TOKENS. nlohmann's SAX interface pushes; a script pulls - so the
// text is parsed once, here, and Read() walks what this collected.
class ibJsonTokenSink {
public:
	using number_integer_t  = nlohmann::json::number_integer_t;
	using number_unsigned_t = nlohmann::json::number_unsigned_t;
	using number_float_t    = nlohmann::json::number_float_t;
	using string_t          = nlohmann::json::string_t;
	using binary_t          = nlohmann::json::binary_t;

	explicit ibJsonTokenSink(std::vector<ibValueJsonReader::ibToken>& tokens) : m_tokens(tokens) {}

	bool null()                           { return Push(ibJsonValueType_Null, ibValue(ibValueTypes::TYPE_NULL)); }
	bool boolean(bool value)              { return Push(ibJsonValueType_Boolean, ibValue(value)); }
	bool number_integer(number_integer_t value)   { return Push(ibJsonValueType_Number, ibValue(ibNumber(static_cast<long long>(value)))); }
	bool number_unsigned(number_unsigned_t value) { return Push(ibJsonValueType_Number, ibValue(ibNumber(static_cast<unsigned long long>(value)))); }

	// ⚠ FROM THE TEXT, NOT FROM THE double. The parser hands over both; the double is what `0.1` is NOT. The
	// same road takes an integer too long for 64 bits, which arrives here as well - whole.
	//
	// 🛑 …AND THE TEXT IS NOT QUITE THE TEXT. This lexer writes the PROCESS LOCALE's decimal point into the token
	// it hands over (so that strtod reads it), and every real host runs under one: on a Ukrainian desktop
	// `1518500.10` arrives here as `1518500,10`, FromString stops at the comma and answers 1518500 - the
	// fraction of every amount, gone without a word. A JSON number is digits, a sign, `e` and ONE point, so
	// whatever else stands in the token IS the point.
	//
	// The double is NOT a fallback: a token the number cannot be read from is refused, like everything else
	// this reader cannot do exactly (ibJsonNumberFromToken).
	bool number_float(number_float_t, const string_t& text) {
		ibNumber number;
		if (!ibJsonNumberFromToken(text, number, m_error))
			return false;
		return Push(ibJsonValueType_Number, ibValue(number));
	}

	bool string(string_t& value)          { return Push(ibJsonValueType_String, ibValue(wxString::FromUTF8(value.data(), value.size()))); }
	bool binary(binary_t&)                { return false; }   // not a thing JSON text has
	bool start_object(std::size_t)        { return Push(ibJsonValueType_ObjectStart, ibValue()); }
	bool key(string_t& value)             { return Push(ibJsonValueType_PropertyName, ibValue(wxString::FromUTF8(value.data(), value.size()))); }
	bool end_object()                     { return Push(ibJsonValueType_ObjectEnd, ibValue()); }
	bool start_array(std::size_t)         { return Push(ibJsonValueType_ArrayStart, ibValue()); }
	bool end_array()                      { return Push(ibJsonValueType_ArrayEnd, ibValue()); }

	bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception& error) {
		m_error = ibJsonParserText(error.what());
		return false;
	}

	const wxString& GetError() const { return m_error; }

private:
	bool Push(ibJsonValueType type, ibValue&& value) {
		m_tokens.emplace_back();
		m_tokens.back().m_type = type;
		m_tokens.back().m_value = std::move(value);
		return true;
	}

	std::vector<ibValueJsonReader::ibToken>& m_tokens;
	wxString m_error;
};

} // namespace

bool ibJsonNumberFromToken(const std::string& token, ibNumber& number, wxString& refusal)
{
	if (token.size() > kMaxNumberLength) {
		refusal = wxString::Format(_("a number of %lu characters - more than %lu is refused"),
			static_cast<unsigned long>(token.size()), static_cast<unsigned long>(kMaxNumberLength));
		return false;
	}

	std::string plain(token);
	long exponent = 0;
	for (size_t i = 0; i < plain.size(); i++) {
		const char c = plain[i];
		if (c == 'e' || c == 'E') {
			exponent = std::strtol(plain.c_str() + i + 1, nullptr, 10);
			break;
		}
		if (!(c >= '0' && c <= '9') && c != '-' && c != '+')
			plain[i] = '.';   // the locale's decimal point, whatever it is: nothing else can stand here
	}
	if (exponent > kMaxExponent || exponent < -kMaxExponent) {
		refusal = wxString::Format(_("a number's exponent is beyond %ld: %s"), kMaxExponent, wxString::FromAscii(plain.c_str()));
		return false;
	}

	if (!number.FromString(wxString::FromAscii(plain.c_str()))) {
		refusal = wxString::Format(_("a number that cannot be read exactly: %s"), wxString::FromAscii(plain.c_str()));
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////
// JSONReader
//////////////////////////////////////////////////////////////////////

// Order MUST match ibValueJsonReader::Prop and ::Func - the number is the index into each table.
void ibValueJsonReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, wxT("JSONReader()"));
	helper.AppendConstructor(1, wxT("JSONReader(text : string)"));

	helper.AppendProp(wxT("CurrentValueType"), true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("CurrentValue"), true, false, wxNOT_FOUND);

	helper.AppendFunc(wxT("SetString"), 1, wxT("SetString(text : string)"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Skip"), wxT("Skip()"));
	helper.AppendFunc(wxT("ReadValue"), wxT("ReadValue()"));
	helper.AppendFunc(wxT("Close"), wxT("Close()"));
}

ibValueJsonReader::ibValueJsonReader() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueJsonReader::~ibValueJsonReader() {}

bool ibValueJsonReader::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	SetText(paParams[0]->GetString());
	return true;
}

void ibValueJsonReader::SetText(const wxString& text)
{
	Close();

	const wxScopedCharBuffer utf8 = text.utf8_str();   // parsed where it lies - not copied a second time

	std::vector<ibToken> tokens;
	ibJsonTokenSink sink(tokens);
	bool parsed = false;
	wxString failure;
	try {
		// Room for the first tokens at once - and NO MORE than that on the text's word: a 30 MB message that is
		// one base64 string is ten tokens, and a reserve by its length was 360 MB committed before a byte was
		// parsed. Inside the try, so that memory refused here is a refusal a script can catch like any other.
		tokens.reserve(std::min<size_t>(utf8.length() / 4 + 1, 65536));
		// STRICT: the whole text is one value. Something after it is not "extra", it is a text that is not JSON.
		parsed = nlohmann::json::sax_parse(utf8.data(), utf8.data() + utf8.length(), &sink,
			nlohmann::json::input_format_t::json, /*strict*/ true);
		if (!parsed)
			failure = sink.GetError();
	}
	catch (const ibBackendException&) {
		throw;
	}
	catch (const std::exception& error) {
		failure = ibJsonParserText(error.what());
	}
	if (!parsed)
		ibBackendCoreException::Error(_("JSONReader: the text is not JSON: %s"), failure);

	m_tokens.swap(tokens);
	m_at = 0;
	m_started = false;
	m_taken = false;
	m_loaded = true;
}

bool ibValueJsonReader::Read()
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("JSONReader: no text is set"));
	if (!m_started)
		m_started = true;
	else if (m_at < m_tokens.size())
		++m_at;
	m_taken = false;
	return m_at < m_tokens.size();
}

ibJsonValueType ibValueJsonReader::CurrentType() const
{
	return m_started && m_at < m_tokens.size() ? m_tokens[m_at].m_type : ibJsonValueType_None;
}

ibValue ibValueJsonReader::CurrentValue() const
{
	return m_started && m_at < m_tokens.size() ? m_tokens[m_at].m_value : ibValue();
}

// A property goes with its value, and a container goes whole: after Skip() the next Read() stands on what
// FOLLOWS the thing skipped. On a plain value there is nothing more to step over.
void ibValueJsonReader::Skip()
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("JSONReader: no text is set"));
	if (!m_started || m_at >= m_tokens.size())
		return;
	m_taken = true;   // stepped over is as good as handed over: ReadValue() after it takes what FOLLOWS

	if (m_tokens[m_at].m_type == ibJsonValueType_PropertyName && m_at + 1 < m_tokens.size())
		++m_at;

	int depth = 0;
	for (; m_at < m_tokens.size(); ++m_at) {
		const ibJsonValueType type = m_tokens[m_at].m_type;
		if (type == ibJsonValueType_ObjectStart || type == ibJsonValueType_ArrayStart)
			++depth;
		else if (type == ibJsonValueType_ObjectEnd || type == ibJsonValueType_ArrayEnd)
			--depth;
		if (depth <= 0)
			break;
	}
}

// ALL OR NOTHING, like the writer's WriteValue. A refusal leaves the reader where it stood: refused once and
// asked again it refuses again (it used to step first and check after - so the same call, repeated, SUCCEEDED
// on the token it had been refused for), and a value refused for its depth does not leave the walk standing
// in the middle of it.
ibValue ibValueJsonReader::ReadValue()
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("JSONReader: no text is set"));

	const auto opensValue = [](ibJsonValueType type) {
		return type != ibJsonValueType_PropertyName && type != ibJsonValueType_ObjectEnd
			&& type != ibJsonValueType_ArrayEnd && type != ibJsonValueType_None;
	};

	// The value the reader STANDS ON - unless it stands on a name, before the first token, or on something
	// already handed over: then the value that FOLLOWS. (It used to be always the one that follows, and the
	// elements of an array could only be taken one at a time by calling until it raised.)
	size_t from = m_at;
	if (!(m_started && !m_taken && m_at < m_tokens.size() && opensValue(m_tokens[m_at].m_type))) {
		from = m_started ? m_at + 1 : 0;
		if (from >= m_tokens.size())
			ibBackendCoreException::Error(_("JSONReader: nothing is left to read"));
		if (m_tokens[from].m_type == ibJsonValueType_PropertyName)
			ibBackendCoreException::Error(_("JSONReader: what follows is a property name, not a value - Read() it, then ReadValue() takes its value"));
		if (!opensValue(m_tokens[from].m_type))
			ibBackendCoreException::Error(_("JSONReader: what follows is the end of an object or an array, not a value"));
	}

	const size_t at = m_at;
	const bool started = m_started;
	const bool taken = m_taken;
	m_started = true;
	m_at = from;
	try {
		const ibValue value = BuildValue(0);
		m_taken = true;
		return value;
	}
	catch (...) {
		m_at = at;
		m_started = started;
		m_taken = taken;
		throw;
	}
}

// The tokens are well-formed - the parser refused anything else - so every start has its end and every name
// its value; what is checked here is only how deep it goes.
ibValue ibValueJsonReader::BuildValue(int depth)
{
	const ibToken& token = m_tokens[m_at];

	if (token.m_type == ibJsonValueType_ObjectStart) {
		if (depth >= kMaxDepth)
			ibBackendCoreException::Error(_("JSONReader: the value nests deeper than %d levels"), kMaxDepth);

		ibValueStructure* const object = new ibValueStructure();
		const ibValue holder(object);   // owns it from here, whatever is raised below
		for (++m_at; m_tokens[m_at].m_type != ibJsonValueType_ObjectEnd; ++m_at) {
			const ibValue name = m_tokens[m_at].m_value;
			++m_at;
			const ibValue member = BuildValue(depth + 1);

			// A name met twice keeps its LAST value - what every other reader of JSON answers, and what SetAt
			// does (overwrite, else create). 🛑 But a Structure's keys FOLD CASE and JSON's do not: `ab12` and
			// `AB12` are two members there and one here, so the second would silently overwrite the first - a
			// dictionary keyed by codes losing entries without a word. That is refused, by both names.
			//
			// WHICH entry the Structure itself matched is asked of the Structure (FindProp answers its index):
			// one lookup, and ITS fold - a second opinion on what "the same key" means is a collision it would
			// have overwritten and this would not have seen, and a scan of all the entries per repeated key was
			// quadratic in a message from outside.
			const wxString spelled = name.GetString();
			const long matched = object->FindProp(spelled);
			if (matched >= 0) {
				const wxString held = object->Entries()[static_cast<size_t>(matched)].first.GetString();
				if (held != spelled)
					ibBackendCoreException::Error(_("JSONReader: the object has the keys '%s' and '%s', which differ only in case - a Structure cannot hold both; walk it with Read() instead"),
						held, spelled);
			}
			object->SetAt(name, member);
		}
		return holder;
	}

	if (token.m_type == ibJsonValueType_ArrayStart) {
		if (depth >= kMaxDepth)
			ibBackendCoreException::Error(_("JSONReader: the value nests deeper than %d levels"), kMaxDepth);

		ibValueArray* const list = new ibValueArray();
		const ibValue holder(list);
		for (++m_at; m_tokens[m_at].m_type != ibJsonValueType_ArrayEnd; ++m_at)
			list->Add(BuildValue(depth + 1));
		return holder;
	}

	return token.m_value;
}

void ibValueJsonReader::Close()
{
	std::vector<ibToken>().swap(m_tokens);   // the memory too, not only the count
	m_at = 0;
	m_started = false;
	m_taken = false;
	m_loaded = false;
}

bool ibValueJsonReader::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enCurrentValueType: {
		ibValue& made = m_typeValues[CurrentType()];
		if (made.GetType() == ibValueTypes::TYPE_EMPTY)
			made = ibValue::CreateEnumObject<ibValueEnumJsonValueType>(CurrentType());
		pvarPropVal = made;
		return true;
	}
	case enCurrentValue:
		pvarPropVal = CurrentValue();
		return true;
	}
	return false;
}

bool ibValueJsonReader::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enSetString:
		if (lSizeArray < 1)
			return false;
		SetText(paParams[0]->GetString());
		return true;
	case enRead:
		pvarRetValue = Read();
		return true;
	case enSkip:
		Skip();
		return true;
	case enReadValue:
		pvarRetValue = ReadValue();
		return true;
	case enClose:
		Close();
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
// JSONWriter
//////////////////////////////////////////////////////////////////////

// Order MUST match ibValueJsonWriter::Func.
void ibValueJsonWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, wxT("JSONWriter()"));
	helper.AppendConstructor(1, wxT("JSONWriter(formatting : JSONFormatting)"));

	helper.AppendFunc(wxT("WriteStartObject"), wxT("WriteStartObject()"));
	helper.AppendFunc(wxT("WriteEndObject"), wxT("WriteEndObject()"));
	helper.AppendFunc(wxT("WriteStartArray"), wxT("WriteStartArray()"));
	helper.AppendFunc(wxT("WriteEndArray"), wxT("WriteEndArray()"));
	helper.AppendFunc(wxT("WritePropertyName"), 1, wxT("WritePropertyName(name : string)"));
	helper.AppendFunc(wxT("WriteValue"), 1, wxT("WriteValue(value : any)"));
	helper.AppendFunc(wxT("Close"), wxT("Close()"));
}

ibValueJsonWriter::ibValueJsonWriter() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueJsonWriter::~ibValueJsonWriter() {}

bool ibValueJsonWriter::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray > 0 && !paParams[0]->IsEmpty()) {
		// A JSONFormatting value and nothing else: a number or a string that "means" one is what the
		// enumeration is there instead of, and anything that is not a member would write compact without a word.
		ibJsonFormatting formatting = ibJsonFormatting();
		if (paParams[0]->GetType() == ibValueTypes::TYPE_ENUM)
			formatting = paParams[0]->ConvertToEnumValue<ibJsonFormatting>();
		if (formatting != ibJsonFormatting_Compact && formatting != ibJsonFormatting_Indented)
			ibBackendCoreException::Error(_("JSONWriter: the formatting is a JSONFormatting value - JSONFormatting.Compact or JSONFormatting.Indented"));
		m_formatting = formatting;
	}
	return true;
}

void ibValueJsonWriter::NewLine(size_t depth)
{
	if (m_formatting != ibJsonFormatting_Indented)
		return;
	m_text += wxT('\n');
	m_text.append(depth, wxT('\t'));
}

void ibValueJsonWriter::BeginValue()
{
	if (m_open.empty()) {
		if (m_rootWritten)
			ibBackendCoreException::Error(_("JSONWriter: a document holds ONE value, and it is already written"));
		m_rootWritten = true;
		return;
	}

	ibFrame& top = m_open.back();
	if (top.m_object) {
		if (!top.m_nameWritten)
			ibBackendCoreException::Error(_("JSONWriter: a value inside an object needs its name first - WritePropertyName"));
		top.m_nameWritten = false;
		return;
	}

	if (top.m_hasMembers)
		m_text += wxT(',');
	top.m_hasMembers = true;
	NewLine(m_open.size());
}

void ibValueJsonWriter::WriteStartObject()
{
	BeginValue();
	m_text += wxT('{');
	ibFrame frame;
	frame.m_object = true;
	m_open.push_back(frame);
}

void ibValueJsonWriter::WriteStartArray()
{
	BeginValue();
	m_text += wxT('[');
	m_open.push_back(ibFrame());
}

void ibValueJsonWriter::EndContainer(bool object)
{
	if (m_open.empty() || m_open.back().m_object != object) {
		if (object)
			ibBackendCoreException::Error(_("JSONWriter: WriteEndObject, and the thing open is not an object"));
		ibBackendCoreException::Error(_("JSONWriter: WriteEndArray, and the thing open is not an array"));
	}
	if (m_open.back().m_nameWritten)
		ibBackendCoreException::Error(_("JSONWriter: a property name is written and its value is not"));

	const bool hadMembers = m_open.back().m_hasMembers;
	m_open.pop_back();
	if (hadMembers)
		NewLine(m_open.size());
	m_text += object ? wxT('}') : wxT(']');
}

void ibValueJsonWriter::WriteEndObject() { EndContainer(true); }
void ibValueJsonWriter::WriteEndArray()  { EndContainer(false); }

void ibValueJsonWriter::WritePropertyName(const wxString& name)
{
	if (m_open.empty() || !m_open.back().m_object)
		ibBackendCoreException::Error(_("JSONWriter: a property name belongs inside an object - WriteStartObject first"));

	ibFrame& top = m_open.back();
	if (top.m_nameWritten)
		ibBackendCoreException::Error(_("JSONWriter: a property name is written and its value is not"));

	if (top.m_hasMembers)
		m_text += wxT(',');
	top.m_hasMembers = true;
	NewLine(m_open.size());
	ibJsonText::AppendQuoted(m_text, name);
	m_text += m_formatting == ibJsonFormatting_Indented ? wxT(": ") : wxT(":");
	top.m_nameWritten = true;
}

// ALL OF A VALUE OR NONE OF IT. A Structure with a reference three levels down is refused - and the two levels
// already written are taken back, so the writer stands where it stood and the script that catches the refusal
// can write something else in its place.
//
// Only a COMPOSITE can be refused half-written, so only a composite pays for the way back: a scalar either
// passes the rules before it touches the text or is refused before it does.
void ibValueJsonWriter::WriteValue(const ibValue& value)
{
	// …and only an OBJECT can be a composite: a string, a number, a date answer by their type, with no cast.
	if (value.GetRef()->GetType() != ibValueTypes::TYPE_VALUE) {
		WriteAny(value, 0);
		return;
	}

	const size_t length = m_text.length();
	const std::vector<ibFrame> open = m_open;
	const bool rootWritten = m_rootWritten;
	try {
		WriteAny(value, 0);
	}
	catch (...) {
		m_text.Truncate(length);
		m_open = open;
		m_rootWritten = rootWritten;
		throw;
	}
}

void ibValueJsonWriter::WriteAny(const ibValue& value, int depth)
{
	const ibValue* const target = value.GetRef();   // the value itself when it refers to nothing else
	const bool object = target->GetType() == ibValueTypes::TYPE_VALUE;   // only an object is worth a cast

	if (const ibValueContainer* const map = object ? dynamic_cast<const ibValueContainer*>(target) : nullptr) {
		if (depth >= kMaxDepth)
			ibBackendCoreException::Error(_("JSONWriter: the value nests deeper than %d levels"), kMaxDepth);
		WriteStartObject();
		for (const std::pair<ibValue, ibValue>& entry : map->Entries()) {
			if (entry.first.GetType() != ibValueTypes::TYPE_STRING)
				ibBackendCoreException::Error(_("JSONWriter: the names in a JSON object are strings, and this one has a key of type '%s'"),
					entry.first.GetClassName());
			WritePropertyName(entry.first.GetString());
			WriteAny(entry.second, depth + 1);
		}
		WriteEndObject();
		return;
	}

	if (const ibValueArray* const list = object ? dynamic_cast<const ibValueArray*>(target) : nullptr) {
		if (depth >= kMaxDepth)
			ibBackendCoreException::Error(_("JSONWriter: the value nests deeper than %d levels"), kMaxDepth);
		WriteStartArray();
		for (const ibValue& item : list->Values())
			WriteAny(item, depth + 1);
		WriteEndArray();
		return;
	}

	switch (target->GetType()) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
		BeginValue();
		m_text += wxT("null");
		return;
	case ibValueTypes::TYPE_BOOLEAN:
		BeginValue();
		m_text += target->GetBoolean() ? wxT("true") : wxT("false");
		return;
	case ibValueTypes::TYPE_NUMBER:
		// The number's own text: exact, a point for the fraction whatever the locale, never through a double.
		BeginValue();
		m_text += target->GetNumber().ToString();
		return;
	case ibValueTypes::TYPE_DATE: {
		// An EMPTY date is `null`, not a moment in the year 1: the platform's empty date is a perfectly valid
		// wxDateTime, and sent as one the other system receives `0001-01-01T00:00:00` for "not filled".
		const wxDateTime moment = target->GetDateTime();
		BeginValue();
		if (!target->IsEmpty() && moment.IsValid())
			ibJsonText::AppendQuoted(m_text, moment.FormatISOCombined(wxT('T')));
		else
			m_text += wxT("null");
		return;
	}
	case ibValueTypes::TYPE_STRING:
		BeginValue();
		ibJsonText::AppendQuoted(m_text, target->GetString());
		return;
	default:
		break;
	}

	// A reference, a table, an enumeration member: nothing here knows what should stand for it in somebody
	// else's system - a code, a description, a Guid - and guessing is how an exchange goes quietly wrong.
	ibBackendCoreException::Error(_("JSONWriter: a value of type '%s' has no JSON form - write what stands for it (a code, a description, a Guid)"),
		target->GetClassName());
}

wxString ibValueJsonWriter::Close()
{
	if (!m_open.empty())
		ibBackendCoreException::Error(_("JSONWriter: %u object(s) or array(s) are still open"), static_cast<unsigned>(m_open.size()));
	// Nothing written is not a document either: an empty text does not parse, and a branch that forgot to
	// write would send it as a body. What Close() hands over always parses.
	if (!m_rootWritten)
		ibBackendCoreException::Error(_("JSONWriter: nothing is written - a document holds one value"));

	wxString text;
	text.swap(m_text);
	m_open.clear();
	m_rootWritten = false;
	return text;
}

bool ibValueJsonWriter::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enWriteStartObject:
		WriteStartObject();
		return true;
	case enWriteEndObject:
		WriteEndObject();
		return true;
	case enWriteStartArray:
		WriteStartArray();
		return true;
	case enWriteEndArray:
		WriteEndArray();
		return true;
	case enWritePropertyName:
		if (lSizeArray < 1)
			return false;
		WritePropertyName(paParams[0]->GetString());
		return true;
	case enWriteValue:
		if (lSizeArray < 1)
			return false;
		WriteValue(*paParams[0]);
		return true;
	case enClose:
		pvarRetValue = Close();
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueJsonReader, "JSONReader", value_to_clsid("VL_JSRD"));
VALUE_TYPE_REGISTER(ibValueJsonWriter, "JSONWriter", value_to_clsid("VL_JSWR"));
