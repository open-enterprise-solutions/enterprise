// =============================================================================
// JSONReader / JSONWriter - what a script cannot check of itself: that a number arrives EXACT, what the text a
// writer hands over looks like to the byte, and every refusal (the corpus suite,
// tests/scripts/test_json_suite.txt, checks the language side: New, the properties, the enumerations).
// =============================================================================

#include <gtest/gtest.h>

#include <clocale>
#include <initializer_list>
#include <string>
#include <vector>

#include "backend/backend_exception.h"
#include "backend/system/value/valueJson.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueMap.h"
#include "backend/serialize/jsonText.h"

namespace {

wxString U(const char* utf8) { return wxString::FromUTF8(utf8); }

ibValue ReadWhole(const wxString& text) {
	ibValueJsonReader reader;
	reader.SetText(text);
	return reader.ReadValue();
}

const ibValueArray* AsArray(const ibValue& value) {
	return value.IsReference() ? dynamic_cast<const ibValueArray*>(value.GetRef()) : nullptr;
}

const ibValueStructure* AsStructure(const ibValue& value) {
	return value.IsReference() ? dynamic_cast<const ibValueStructure*>(value.GetRef()) : nullptr;
}

} // namespace

// ⚠ THE REASON THE READER EXISTS IN THIS SHAPE. A tenth that came through a double is not a tenth, and money
// that is not exact does not add up. Also whole: an integer too long for 64 bits, and an exponent.
TEST(JsonReader, ANumberIsReadFromItsTextNotThroughADouble) {
	const ibValue value = ReadWhole(wxT("[0.1, 0.2, 12345678901234567890123, 1.5e2, -0.000001, 100]"));
	const ibValueArray* list = AsArray(value);
	ASSERT_NE(list, nullptr);
	const std::vector<ibValue>& n = list->Values();
	ASSERT_EQ(n.size(), 6u);

	EXPECT_TRUE(n[0].GetNumber() + n[1].GetNumber() == ibNumber(wxString(wxT("0.3"))))
		<< "0.1 + 0.2 is 0.3 - exactly, which is what a double cannot say";
	EXPECT_EQ(n[2].GetNumber().ToString(), wxString(wxT("12345678901234567890123")));
	EXPECT_TRUE(n[3].GetNumber() == ibNumber(150));
	EXPECT_TRUE(n[4].GetNumber() == ibNumber(wxString(wxT("-0.000001"))));
	EXPECT_TRUE(n[5].GetNumber() == ibNumber(100));
}

// 🛑 EVERY REAL HOST RUNS UNDER A LOCALE, and the parser writes that locale's decimal point into the number
// token it hands over. Under a comma locale `1518500.10` arrived as `1518500,10` and was read as 1518500 - the
// fraction of every amount gone, silently, on every Ukrainian, Russian or German desktop. This suite runs in
// the C locale, so nothing else here would ever see it.
TEST(JsonReader, ANumberKeepsItsFractionUnderACommaLocale) {
	const std::string before = std::setlocale(LC_NUMERIC, nullptr);
	bool comma = false;
	for (const char* name : { "de-DE", "de_DE.UTF-8", "uk-UA", "uk_UA.UTF-8", "ru-RU", "ru_RU.UTF-8" }) {
		if (std::setlocale(LC_NUMERIC, name) != nullptr && *std::localeconv()->decimal_point == ',') {
			comma = true;
			break;
		}
	}
	if (!comma) {
		std::setlocale(LC_NUMERIC, before.c_str());
		GTEST_SKIP() << "no comma-decimal locale is installed on this machine";
	}

	ibValue value;
	try {
		value = ReadWhole(wxT("[1518500.10, 1.5e2, -0.25]"));
	}
	catch (...) {
		std::setlocale(LC_NUMERIC, before.c_str());
		throw;
	}
	std::setlocale(LC_NUMERIC, before.c_str());

	const ibValueArray* list = AsArray(value);
	ASSERT_NE(list, nullptr);
	ASSERT_EQ(list->Values().size(), 3u);
	EXPECT_TRUE(list->Values()[0].GetNumber() == ibNumber(wxString(wxT("1518500.1"))));
	EXPECT_TRUE(list->Values()[1].GetNumber() == ibNumber(150));
	EXPECT_TRUE(list->Values()[2].GetNumber() == ibNumber(wxString(wxT("-0.25"))));
}

// …and the same asked of the token road DIRECTLY, with the comma written out - because a build agent may have no
// comma locale installed, the test above is then skipped, and this defect must not pass a suite anywhere.
TEST(JsonReader, ATokenWithTheLocalesPointIsTheSameNumber) {
	ibNumber number;
	wxString refusal;
	ASSERT_TRUE(ibJsonNumberFromToken("1518500,10", number, refusal)) << refusal.ToStdString();
	EXPECT_TRUE(number == ibNumber(wxString(wxT("1518500.1"))));
	ASSERT_TRUE(ibJsonNumberFromToken("1,5e2", number, refusal));
	EXPECT_TRUE(number == ibNumber(150));
	ASSERT_TRUE(ibJsonNumberFromToken("-0.25", number, refusal));
	EXPECT_TRUE(number == ibNumber(wxString(wxT("-0.25"))));
}

// What a token from outside is not allowed to cost. `1e-999999` is nine characters, is accepted exactly by
// everything below, and the first `qty + 1` on it aligns two numbers a million digits apart; a 300 KB `0.111…`
// is a finite double, and reading it digit by digit is tens of seconds. Both are refused - never rounded.
// (A MAGNITUDE past a double's range the parser refuses by itself: `1E+5000` never reaches this code.)
TEST(JsonReader, ANumberNobodyMeansIsRefused) {
	ibValueJsonReader reader;
	EXPECT_THROW(reader.SetText(wxT("[1e-999999]")), ibBackendException);
	EXPECT_THROW(reader.SetText(wxT("[1E+5000]")), ibBackendException) << "the parser's own refusal";
	EXPECT_THROW(reader.SetText(wxT("[0.") + wxString(wxT('1'), 5000) + wxT("]")), ibBackendException);
	EXPECT_NO_THROW(reader.SetText(wxT("[1e300, 2.5E-300, 0.") + wxString(wxT('1'), 900) + wxT("]")));

	ibNumber number;
	wxString refusal;
	EXPECT_FALSE(ibJsonNumberFromToken("1e-999999", number, refusal));
	EXPECT_FALSE(refusal.empty());
}

// A message from outside with every key listed twice. The collision check used to scan all the entries for
// each repeated key - quadratic, minutes for a 600 KB text; asked of the Structure's own lookup it is one probe.
TEST(JsonReader, RepeatedKeysCostOneLookupEach) {
	wxString text = wxT("{");
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < 4000; i++)
			text += wxString::Format(wxT("%s\"k%d\":%d"), text.length() > 1 ? wxT(",") : wxT(""), i, pass);
	text += wxT("}");

	const ibValue value = ReadWhole(text);
	const ibValueStructure* object = AsStructure(value);
	ASSERT_NE(object, nullptr);
	ASSERT_EQ(object->Entries().size(), 4000u);
	EXPECT_TRUE(object->Entries()[0].second.GetNumber() == ibNumber(1)) << "the LAST value of a repeated key";
}

TEST(JsonReader, TheTokensComeInTheOrderOfTheText) {
	ibValueJsonReader reader;
	reader.SetText(wxT("{\"a\":[1,true,null],\"b\":\"x\"}"));
	EXPECT_EQ(reader.CurrentType(), ibJsonValueType_None) << "nothing is read yet";

	const ibJsonValueType expected[] = {
		ibJsonValueType_ObjectStart, ibJsonValueType_PropertyName, ibJsonValueType_ArrayStart,
		ibJsonValueType_Number, ibJsonValueType_Boolean, ibJsonValueType_Null, ibJsonValueType_ArrayEnd,
		ibJsonValueType_PropertyName, ibJsonValueType_String, ibJsonValueType_ObjectEnd };
	for (const ibJsonValueType type : expected) {
		ASSERT_TRUE(reader.Read());
		EXPECT_EQ(reader.CurrentType(), type);
	}
	EXPECT_FALSE(reader.Read());
	EXPECT_EQ(reader.CurrentType(), ibJsonValueType_None) << "nothing is left";
	EXPECT_FALSE(reader.Read()) << "and asking again is still an answer, not an error";
}

// An object is a Structure: any string is a key, and the keys keep the order they came in.
TEST(JsonReader, AnObjectKeepsItsKeysAndTheirOrder) {
	const ibValue value = ReadWhole(wxT("{\"z\":1,\"odd key\":2,\"a\":3}"));
	const ibValueStructure* object = AsStructure(value);
	ASSERT_NE(object, nullptr);
	ASSERT_EQ(object->Entries().size(), 3u);
	EXPECT_EQ(object->Entries()[0].first.GetString(), wxString(wxT("z")));
	EXPECT_EQ(object->Entries()[1].first.GetString(), wxString(wxT("odd key")));
	EXPECT_EQ(object->Entries()[2].first.GetString(), wxString(wxT("a")));
}

TEST(JsonReader, ANameMetTwiceKeepsItsLastValue) {
	const ibValue value = ReadWhole(wxT("{\"a\":1,\"b\":2,\"a\":3}"));
	const ibValueStructure* object = AsStructure(value);
	ASSERT_NE(object, nullptr);
	ASSERT_EQ(object->Entries().size(), 2u);
	EXPECT_TRUE(object->Entries()[0].second.GetNumber() == ibNumber(3));
}

// 🛑 A Structure's keys FOLD CASE and JSON's do not. `ab12` and `AB12` are two members there and would be one
// here, the second silently overwriting the first. That is refused by both names - and the same object still
// reads token by token.
TEST(JsonReader, KeysThatDifferOnlyInCaseAreRefusedNotMerged) {
	const wxString text = wxT("{\"prices\":{\"ab12\":1,\"AB12\":2}}");
	try {
		ReadWhole(text);
		FAIL() << "two keys that differ only in case must not come out as one member";
	}
	catch (const ibBackendException& e) {
		const std::string what = e.what();
		EXPECT_NE(what.find("ab12"), std::string::npos) << what;
		EXPECT_NE(what.find("AB12"), std::string::npos) << what;
	}

	ibValueJsonReader walker;
	walker.SetText(text);
	int names = 0;
	while (walker.Read())
		if (walker.CurrentType() == ibJsonValueType_PropertyName) names++;
	EXPECT_EQ(names, 3);
}

TEST(JsonReader, NullIsNullAndTheScalarsAreThemselves) {
	EXPECT_TRUE(ReadWhole(wxT("null")).IsNull());
	EXPECT_EQ(ReadWhole(wxT("true")).GetType(), ibValueTypes::TYPE_BOOLEAN);
	EXPECT_EQ(ReadWhole(wxT("\"x\"")).GetString(), wxString(wxT("x")));
}

// Escapes, a pair of surrogates, and text that is simply not ASCII.
TEST(JsonReader, AStringIsDecoded) {
	const ibValue value = ReadWhole(U("\"\\u0416\\n\\\"\\\\ \\ud83d\\ude00 \xD0\x91\xD0\xBE\xD1\x80\xD1\x89\""));
	EXPECT_EQ(value.GetString(), U("\xD0\x96\n\"\\ \xF0\x9F\x98\x80 \xD0\x91\xD0\xBE\xD1\x80\xD1\x89"));
}

// A text that is not JSON is refused WHOLE, when it is given - not after a script has acted on its first half -
// and the refusal says where.
TEST(JsonReader, ATextThatIsNotJsonIsRefusedWhole) {
	for (const wxString& text : { wxString(wxT("{\"a\":")), wxString(wxT("[1,]")), wxString(wxT("{\"a\":1} tail")),
	                              wxString(), wxString(wxT("{'a':1}")), wxString(wxT("[1] [2]")) }) {
		ibValueJsonReader reader;
		EXPECT_THROW(reader.SetText(text), ibBackendException) << text.ToStdString();
		EXPECT_THROW(reader.Read(), ibBackendException) << "a refused text leaves nothing to read";
	}
	try {
		ibValueJsonReader reader;
		reader.SetText(wxT("{\n  \"a\": oops\n}"));
		FAIL();
	}
	catch (const ibBackendException& e) {
		EXPECT_NE(std::string(e.what()).find("line 2"), std::string::npos) << e.what();
	}

	// …and the refusal still says where when the parser stopped in the MIDDLE of a character: its message
	// quotes the bytes it stopped on, those are then not UTF-8, and decoded strictly the whole sentence - line
	// and column with it - came out empty.
	try {
		ibValueJsonReader reader;
		reader.SetText(U("{\"name\": \xD0\x91\xD0\xBE\xD1\x80\xD1\x89}"));
		FAIL();
	}
	catch (const ibBackendException& e) {
		EXPECT_NE(std::string(e.what()).find("line 1"), std::string::npos) << e.what();
	}
}

// Skip() takes a member whole: the next Read() stands on the member after it.
TEST(JsonReader, SkipStepsOverAMemberWhole) {
	ibValueJsonReader reader;
	reader.SetText(wxT("{\"skip\":{\"deep\":[1,2,{\"x\":1}]},\"keep\":5}"));
	ASSERT_TRUE(reader.Read());                                   // {
	ASSERT_TRUE(reader.Read());                                   // "skip"
	reader.Skip();
	ASSERT_TRUE(reader.Read());
	EXPECT_EQ(reader.CurrentType(), ibJsonValueType_PropertyName);
	EXPECT_EQ(reader.CurrentValue().GetString(), wxString(wxT("keep")));
}

// ReadValue() takes the value that FOLLOWS - after a name, that is the member's value - and leaves the reader
// at its end, so the walk goes on from there.
TEST(JsonReader, ReadValueTakesTheValueThatFollowsAndTheWalkGoesOn) {
	ibValueJsonReader reader;
	reader.SetText(wxT("{\"lines\":[{\"q\":1},{\"q\":2}],\"after\":true}"));
	ASSERT_TRUE(reader.Read());                                   // {
	ASSERT_TRUE(reader.Read());                                   // "lines"
	const ibValue lines = reader.ReadValue();
	const ibValueArray* list = AsArray(lines);
	ASSERT_NE(list, nullptr);
	EXPECT_EQ(list->Count(), 2u);

	ASSERT_TRUE(reader.Read());
	EXPECT_EQ(reader.CurrentValue().GetString(), wxString(wxT("after")));
}

// The elements of an array, one at a time, with no exception for a stop sign: ReadValue() hands over the value
// the reader STANDS ON. (It used to be always the value that follows - so standing on an element's `{` it looked
// at the name after it and refused, and the only way through an array was to call until it raised.)
TEST(JsonReader, TheElementsOfAnArrayAreTakenOneAtATime) {
	ibValueJsonReader reader;
	reader.SetText(wxT("[{\"q\":1},[2,3],\"x\",null,{\"q\":5}]"));
	ASSERT_TRUE(reader.Read());                                   // [
	std::vector<ibValue> items;
	while (reader.Read() && reader.CurrentType() != ibJsonValueType_ArrayEnd)
		items.push_back(reader.ReadValue());
	ASSERT_EQ(items.size(), 5u);
	EXPECT_NE(AsStructure(items[0]), nullptr);
	EXPECT_NE(AsArray(items[1]), nullptr);
	EXPECT_EQ(items[2].GetString(), wxString(wxT("x")));
	EXPECT_TRUE(items[3].IsNull());
	EXPECT_NE(AsStructure(items[4]), nullptr);
	EXPECT_FALSE(reader.Read()) << "the array's end was the last token";

	ibValueJsonReader empty;
	empty.SetText(wxT("[]"));
	ASSERT_TRUE(empty.Read());
	int seen = 0;
	while (empty.Read() && empty.CurrentType() != ibJsonValueType_ArrayEnd) seen++;
	EXPECT_EQ(seen, 0) << "an empty array is an empty loop, not an exception";
}

// Standing on a value that starts there, ReadValue() takes THAT value whole…
TEST(JsonReader, ReadValueTakesTheValueTheReaderStandsOn) {
	ibValueJsonReader reader;
	reader.SetText(wxT("{\"a\":1,\"b\":2}"));
	ASSERT_TRUE(reader.Read());                                   // {
	const ibValue whole = reader.ReadValue();
	ASSERT_NE(AsStructure(whole), nullptr);
	EXPECT_EQ(AsStructure(whole)->Entries().size(), 2u);
	EXPECT_EQ(reader.CurrentType(), ibJsonValueType_ObjectEnd);
}

TEST(JsonReader, WhatIsNotAValueIsNotReadAsOne) {
	ibValueJsonReader reader;
	reader.SetText(wxT("{\"a\":1,\"b\":2}"));
	ASSERT_TRUE(reader.Read());                                   // {
	ASSERT_TRUE(reader.Read());                                   // "a"
	EXPECT_TRUE(reader.ReadValue().GetNumber() == ibNumber(1));   // after a name: the value that follows
	// …which is now handed over, so what follows IT is asked for - and that is the name "b", not a value.
	EXPECT_THROW(reader.ReadValue(), ibBackendException);
	// A refusal leaves the reader where it stood: asked again it refuses again (it used to step first and
	// check after, so the same call repeated SUCCEEDED on the token it had been refused for).
	EXPECT_EQ(reader.CurrentType(), ibJsonValueType_Number);
	EXPECT_THROW(reader.ReadValue(), ibBackendException);
	EXPECT_EQ(reader.CurrentType(), ibJsonValueType_Number);
	ASSERT_TRUE(reader.Read());
	EXPECT_EQ(reader.CurrentValue().GetString(), wxString(wxT("b")));

	ibValueJsonReader done;
	done.SetText(wxT("1"));
	done.ReadValue();
	EXPECT_THROW(done.ReadValue(), ibBackendException) << "nothing is left";

	ibValueJsonReader none;
	EXPECT_THROW(none.Read(), ibBackendException) << "no text is set";
}

// A hundred thousand `[` is an attack on the stack of whoever builds the value, not a document. It parses -
// the parser keeps no stack of its own - it can be walked and skipped, and building it is refused.
TEST(JsonReader, AValueThatNestsTooDeepIsRefusedNotBuilt) {
	const wxString deep = wxString(wxT('['), 5000) + wxString(wxT(']'), 5000);
	ibValueJsonReader reader;
	reader.SetText(deep);
	EXPECT_THROW(reader.ReadValue(), ibBackendException);
	EXPECT_EQ(reader.CurrentType(), ibJsonValueType_None) << "refused for its depth, the walk is not left standing inside it";
	ASSERT_TRUE(reader.Read());
	EXPECT_EQ(reader.CurrentType(), ibJsonValueType_ArrayStart);

	ibValueJsonReader walker;
	walker.SetText(deep);
	ASSERT_TRUE(walker.Read());
	walker.Skip();
	EXPECT_EQ(walker.CurrentType(), ibJsonValueType_ArrayEnd);
	EXPECT_FALSE(walker.Read());
}

// ---------------------------------------------------------------------------------------------------------

TEST(JsonWriter, TheCompactTextToTheByte) {
	ibValueJsonWriter writer;
	writer.WriteStartObject();
	writer.WritePropertyName(wxT("n"));     writer.WriteValue(ibValue(wxString(wxT("A-1"))));
	writer.WritePropertyName(wxT("qty"));   writer.WriteValue(ibValue(ibNumber(wxString(wxT("1.5")))));
	writer.WritePropertyName(wxT("ok"));    writer.WriteValue(ibValue(true));
	writer.WritePropertyName(wxT("none"));  writer.WriteValue(ibValue());
	writer.WritePropertyName(wxT("lines")); writer.WriteStartArray();
	writer.WriteValue(ibValue(ibNumber(1))); writer.WriteValue(ibValue(ibNumber(2)));
	writer.WriteEndArray();
	writer.WritePropertyName(wxT("empty")); writer.WriteStartObject(); writer.WriteEndObject();
	writer.WriteEndObject();
	EXPECT_EQ(writer.Close(), wxString(wxT("{\"n\":\"A-1\",\"qty\":1.5,\"ok\":true,\"none\":null,\"lines\":[1,2],\"empty\":{}}")));
}

TEST(JsonWriter, TheIndentedTextToTheByte) {
	ibValueJsonWriter writer;
	writer.SetFormatting(ibJsonFormatting_Indented);
	writer.WriteStartObject();
	writer.WritePropertyName(wxT("a")); writer.WriteValue(ibValue(ibNumber(1)));
	writer.WritePropertyName(wxT("b")); writer.WriteStartArray();
	writer.WriteValue(ibValue(true)); writer.WriteStartArray(); writer.WriteEndArray();
	writer.WriteEndArray();
	writer.WriteEndObject();
	EXPECT_EQ(writer.Close(), wxString(wxT("{\n\t\"a\": 1,\n\t\"b\": [\n\t\ttrue,\n\t\t[]\n\t]\n}")));
}

// A number goes out as its own text: exact, a point whatever the locale, as long as it is.
TEST(JsonWriter, ANumberGoesOutExact) {
	ibValueJsonWriter writer;
	writer.WriteStartArray();
	writer.WriteValue(ibValue(ibNumber(wxString(wxT("0.1")))));
	writer.WriteValue(ibValue(ibNumber(wxString(wxT("12345678901234567890123")))));
	writer.WriteValue(ibValue(ibNumber(wxString(wxT("-1518500.07")))));
	writer.WriteEndArray();
	EXPECT_EQ(writer.Close(), wxString(wxT("[0.1,12345678901234567890123,-1518500.07]")));
}

TEST(JsonWriter, WhatMustBeEscapedIsAndTheRestGoesOutAsItself) {
	ibValueJsonWriter writer;
	writer.WriteValue(ibValue(U("q\" b\\ n\n t\t \x01 \xE2\x80\xA8 \xD0\x91\xD0\xBE\xD1\x80\xD1\x89")));
	EXPECT_EQ(writer.Close(), U("\"q\\\" b\\\\ n\\n t\\t \\u0001 \\u2028 \xD0\x91\xD0\xBE\xD1\x80\xD1\x89\""));
}

// HOW a string is spelled in JSON is said once for the backend (ibJsonText) - the configuration's JSON view and
// the script's writer go through it. Asked directly: the short forms, everything below U+0020, the two line
// separators, a NUL that is a character and not the end of the text, and the bytes of the UTF-8 form.
TEST(JsonText, OneSpellingForTheWholeBackend) {
	wxString out;
	ibJsonText::AppendQuoted(out, wxString(wxT("a\"b\\c\b\f\n\r\t")) + wxString(wxUniChar(1)) + wxString(wxUniChar(0x2029)));
	EXPECT_EQ(out, wxString(wxT("\"a\\\"b\\\\c\\b\\f\\n\\r\\t\\u0001\\u2029\"")));

	wxString withNul(wxT("a"));
	withNul += wxUniChar(0);
	withNul += wxT("b");
	ASSERT_EQ(withNul.length(), 3u);
	wxString nul;
	ibJsonText::AppendQuoted(nul, withNul);
	EXPECT_EQ(nul, wxString(wxT("\"a\\u0000b\""))) << "a NUL is a character, not the end of the text";

	EXPECT_EQ(ibJsonText::QuotedUtf8(U("\xD0\x91")), std::string("\"\xD0\x91\""));
	EXPECT_EQ(ibJsonText::QuotedUtf8(wxString()), std::string("\"\""));
}

TEST(JsonWriter, ADateIsIso8601) {
	ibValueJsonWriter writer;
	writer.WriteValue(ibValue(wxDateTime(20, wxDateTime::Sep, 2026, 21, 5, 39)));
	EXPECT_EQ(writer.Close(), wxString(wxT("\"2026-09-20T21:05:39\"")));
}

// An EMPTY date is `null`. The platform's empty date is a perfectly valid moment in the year 1, and sent as one
// the other system receives `0001-01-01T00:00:00` for "not filled".
TEST(JsonWriter, AnEmptyDateIsNull) {
	const ibValue empty(ibValueTypes::TYPE_DATE);
	ASSERT_TRUE(empty.IsEmpty());
	ibValueJsonWriter writer;
	writer.WriteValue(empty);
	EXPECT_EQ(writer.Close(), wxString(wxT("null")));
}

// The writer keeps the document well-formed by itself - every way of breaking it is a refusal.
TEST(JsonWriter, TheDocumentStaysWellFormed) {
	{ ibValueJsonWriter w; w.WriteStartObject(); EXPECT_THROW(w.WriteValue(ibValue(true)), ibBackendException) << "a value with no name"; }
	{ ibValueJsonWriter w; w.WriteStartObject(); EXPECT_THROW(w.WriteEndArray(), ibBackendException) << "the wrong end"; }
	{ ibValueJsonWriter w; EXPECT_THROW(w.WriteEndObject(), ibBackendException) << "an end with nothing open"; }
	{ ibValueJsonWriter w; EXPECT_THROW(w.WritePropertyName(wxT("a")), ibBackendException) << "a name outside an object"; }
	{ ibValueJsonWriter w; w.WriteStartArray(); EXPECT_THROW(w.WritePropertyName(wxT("a")), ibBackendException) << "a name inside an array"; }
	{ ibValueJsonWriter w; w.WriteStartObject(); w.WritePropertyName(wxT("a")); EXPECT_THROW(w.WritePropertyName(wxT("b")), ibBackendException) << "two names in a row"; }
	{ ibValueJsonWriter w; w.WriteStartObject(); w.WritePropertyName(wxT("a")); EXPECT_THROW(w.WriteEndObject(), ibBackendException) << "a name left without its value"; }
	{ ibValueJsonWriter w; w.WriteValue(ibValue(true)); EXPECT_THROW(w.WriteValue(ibValue(true)), ibBackendException) << "a second document"; }
	{ ibValueJsonWriter w; w.WriteStartArray(); EXPECT_THROW(w.Close(), ibBackendException) << "closed with an array open"; }
	{ ibValueJsonWriter w; EXPECT_THROW(w.Close(), ibBackendException) << "nothing written: an empty text does not parse either"; }

	// …and the formatting is a JSONFormatting value: a number or a string that "means" one wrote compact
	// without a word.
	for (const ibValue& wrong : { ibValue(ibNumber(2)), ibValue(wxString(wxT("Indented"))), ibValue(true) }) {
		ibValue argument = wrong;
		ibValue* params[] = { &argument };
		ibValueJsonWriter w;
		EXPECT_THROW(w.Init(params, 1), ibBackendException);
	}
}

// A Structure / Array goes out whole, to any depth...
TEST(JsonWriter, AStructureGoesOutWhole) {
	ibValueStructure* line = new ibValueStructure();
	const ibValue lineHolder(line);
	line->Insert(ibValue(wxString(wxT("item"))), ibValue(wxString(wxT("x"))));
	line->Insert(ibValue(wxString(wxT("qty"))), ibValue(ibNumber(2)));

	ibValueArray* lines = new ibValueArray();
	const ibValue linesHolder(lines);
	lines->Add(lineHolder);

	ibValueStructure* order = new ibValueStructure();
	const ibValue orderHolder(order);
	order->Insert(ibValue(wxString(wxT("number"))), ibValue(wxString(wxT("A-1"))));
	order->Insert(ibValue(wxString(wxT("lines"))), linesHolder);

	ibValueJsonWriter writer;
	writer.WriteValue(orderHolder);
	EXPECT_EQ(writer.Close(), wxString(wxT("{\"number\":\"A-1\",\"lines\":[{\"item\":\"x\",\"qty\":2}]}")));
}

// ...or not at all: a value with no JSON form three levels down is refused BY NAME, what was already written
// of it is taken back, and the writer stands where it stood.
TEST(JsonWriter, ARefusedValueLeavesNothingBehind) {
	ibValueStructure* bad = new ibValueStructure();
	const ibValue badHolder(bad);
	bad->Insert(ibValue(wxString(wxT("fine"))), ibValue(ibNumber(1)));
	bad->Insert(ibValue(wxString(wxT("reader"))), ibValue(new ibValueJsonReader()));   // no JSON form

	ibValueJsonWriter writer;
	writer.WriteStartArray();
	writer.WriteValue(ibValue(ibNumber(1)));
	try {
		writer.WriteValue(badHolder);
		FAIL() << "a value with no JSON form must be refused";
	}
	catch (const ibBackendException& e) {
		EXPECT_NE(std::string(e.what()).find("JSONReader"), std::string::npos) << e.what();
	}
	writer.WriteValue(ibValue(ibNumber(2)));
	writer.WriteEndArray();
	EXPECT_EQ(writer.Close(), wxString(wxT("[1,2]")));
}

TEST(JsonWriter, AContainerWithAKeyThatIsNotAStringIsRefused) {
	ibValueContainer* map = new ibValueContainer();
	const ibValue holder(map);
	map->Insert(ibValue(ibNumber(1)), ibValue(wxString(wxT("x"))));
	ibValueJsonWriter writer;
	EXPECT_THROW(writer.WriteValue(holder), ibBackendException);
	writer.WriteValue(ibValue(ibNumber(1)));   // nothing of the refused value was left: the document is still to be written
	EXPECT_EQ(writer.Close(), wxString(wxT("1")));
}

// What one writes the other reads: the round trip a script's exchange is made of.
TEST(JsonWriter, WhatIsWrittenIsWhatIsRead) {
	const wxString text = wxT("{\"number\":\"A-17\",\"total\":1518500.1,\"big\":12345678901234567890123,\"paid\":true,\"note\":null,\"lines\":[{\"item\":\"x\",\"qty\":2},[]]}");
	const ibValue value = ReadWhole(text);
	for (const ibJsonFormatting formatting : { ibJsonFormatting_Compact, ibJsonFormatting_Indented }) {
		ibValueJsonWriter writer;
		writer.SetFormatting(formatting);
		writer.WriteValue(value);
		const wxString written = writer.Close();
		if (formatting == ibJsonFormatting_Compact)
			EXPECT_EQ(written, text);

		ibValueJsonWriter again;
		again.WriteValue(ReadWhole(written));
		EXPECT_EQ(again.Close(), text) << "read back and written compact, it is the text it started as";
	}
}
