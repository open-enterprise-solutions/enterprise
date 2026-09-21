// JSON AS A PROPERTY, not as a list of examples - the shape tests/test_queryRoundTrip.cpp gave the query
// language, given to JSONReader / JSONWriter.
//
// tests/test_valueJson.cpp pins the cases somebody thought of. Every defect three review passes found in this
// pair was a case nobody had: a number whose token carried the locale's comma, two keys that differ only in
// case, `1e-999999`, a 300 KB number, a key check that was quadratic, a refusal that moved the reader, a Close()
// that handed over an empty text. All of them were found by READING the code. This file is what finds the next
// one by RUNNING it.
//
// A deterministic generator builds documents - as a tree of its OWN, not as engine values - and five things are
// asked of every one:
//
//   1. what the writer hands over is the text this file computes for the tree, by rules written here;
//   2. what the reader builds from that text is the tree again: the same kinds, the same keys in the same order,
//      every string character for character, every number DIGIT FOR DIGIT;
//   3. damaged text - a character cut, doubled, replaced, a slice repeated, the end torn off - is either read or
//      refused with a sentence, by ibBackendException and nothing else; the parser library itself, asked the
//      same text, agrees on which it is;
//   4. a whole value that is REFUSED - two keys a Structure cannot hold apart, planted into the tree - leaves the
//      reader where it stood, and asked again is refused again;
//   5. documents shaped to be expensive stay cheap.
//
// ⭐ THE TRUTH IS COMPUTED OUTSIDE THE ENGINE (development.md, section 5). The expected text comes from this
// file's own spelling rules and the expected numbers are the generator's own digits - a test that built its
// answer with JSONWriter or ibNumber would confirm them to themselves.
//
// ⚠ SEEDED, NOT RANDOM, for the reason the neighbour gives: a failure has to be reproducible and its input
// printable. The generator is the same LCG with the same finalizer, and every failure names its seed and its text.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "3rdparty/nlohmann/json.hpp"

#include "backend/backend_exception.h"
#include "backend/system/value/valueJson.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueMap.h"

namespace {

// ---------------------------------------------------------------------------
//  The tree the generator makes - its own, so that nothing of the engine's is in the expected answer
// ---------------------------------------------------------------------------

struct ibJsonNode {
	enum Kind { Null, Boolean, Number, String, Object, Array } m_kind = Null;
	bool        m_flag = false;
	std::string m_digits;                                     // a number AS TEXT, already in its shortest spelling
	wxString    m_text;
	std::vector<std::pair<wxString, ibJsonNode>> m_members;   // in the order they are written
	std::vector<ibJsonNode> m_items;
};

// What the generator really produced - asked by TheGeneratorReallyProducesTheAwkwardShapes, because a generator
// that quietly produces less than it claims makes a green run mean less than it looks.
struct ibJsonCensus {
	int m_deepest = 0, m_emptyObjects = 0, m_emptyArrays = 0, m_escaped = 0, m_surrogates = 0, m_nonAscii = 0,
		m_fractions = 0, m_longIntegers = 0, m_negatives = 0, m_oddKeys = 0, m_constantKeys = 0, m_nulls = 0,
		m_emptyStrings = 0;
};

class ibJsonGen
{
public:
	explicit ibJsonGen(unsigned int seed) : m_state(seed ? seed : 1u) {}

	// The neighbour's LCG and finalizer (test_queryRoundTrip.cpp says why the low bits are mixed down).
	unsigned int Next()
	{
		m_state = m_state * 1664525u + 1013904223u;
		unsigned int x = m_state;
		x ^= x >> 16;
		x *= 2246822519u;
		x ^= x >> 13;
		return x;
	}
	unsigned int Below(unsigned int n) { return n ? Next() % n : 0u; }
	bool Chance(unsigned int oneIn) { return Below(oneIn) == 0u; }

	ibJsonCensus m_census;

	// A string out of the pieces that have each broken a JSON writer somewhere: what must be escaped, what must
	// NOT be, text that is not ASCII, a character outside the basic plane, and nothing at all.
	wxString Text()
	{
		if (Chance(8)) { m_census.m_emptyStrings++; return wxString(); }
		wxString text;
		const unsigned int pieces = 1u + Below(5);
		for (unsigned int i = 0; i < pieces; i++) {
			switch (Below(9)) {
			case 0: text += wxT("plain"); break;
			case 1: text += wxT(" a b "); break;
			case 2: text += wxT('"'); m_census.m_escaped++; break;
			case 3: text += wxT('\\'); m_census.m_escaped++; break;
			case 4: text += wxString(wxT("\n\r\t\b\f")).Mid(Below(5), 1); m_census.m_escaped++; break;
			case 5: text += wxString(wxUniChar(1 + static_cast<int>(Below(0x1F)))); m_census.m_escaped++; break;
			case 6: text += wxString(wxUniChar(Chance(2) ? 0x2028 : 0x2029)); m_census.m_escaped++; break;
			case 7: text += wxString::FromUTF8("\xD0\x91\xD0\xBE\xD1\x80\xD1\x89 \xD1\x97"); m_census.m_nonAscii++; break;
			default: text += wxString::FromUTF8("\xF0\x9F\x98\x80"); m_census.m_surrogates++; break;
			}
		}
		return text;
	}

	// A number in its shortest spelling - no leading zero, no trailing zero of a fraction, no minus zero - which is
	// the text a number's own ToString is expected to give back, so the digits here ARE the expected answer.
	std::string Digits()
	{
		std::string whole;
		const unsigned int length = Chance(5) ? 20u + Below(12) : 1u + Below(9);
		if (length >= 20u) m_census.m_longIntegers++;
		for (unsigned int i = 0; i < length; i++)
			whole += static_cast<char>('0' + (i == 0 && length > 1 ? 1 + Below(9) : Below(10)));

		std::string fraction;
		if (Chance(2)) {
			const unsigned int places = 1u + Below(12);
			for (unsigned int i = 0; i < places; i++)
				fraction += static_cast<char>('0' + Below(10));
			while (!fraction.empty() && fraction.back() == '0') fraction.pop_back();
			if (!fraction.empty()) m_census.m_fractions++;
		}

		std::string digits = whole;
		if (!fraction.empty()) digits += "." + fraction;
		const bool zero = digits.find_first_not_of("0.") == std::string::npos;
		if (!zero && Chance(3)) { digits.insert(0, "-"); m_census.m_negatives++; }
		return zero ? std::string("0") : digits;
	}

	ibJsonNode Value(int depth)
	{
		if (depth > m_census.m_deepest) m_census.m_deepest = depth;
		ibJsonNode node;
		const unsigned int kind = depth >= 5 ? Below(4) : Below(7);
		switch (kind) {
		case 0: node.m_kind = ibJsonNode::Null; m_census.m_nulls++; break;
		case 1: node.m_kind = ibJsonNode::Boolean; node.m_flag = Chance(2); break;
		case 2: node.m_kind = ibJsonNode::Number; node.m_digits = Digits(); break;
		case 3: node.m_kind = ibJsonNode::String; node.m_text = Text(); break;
		case 4: case 5: {
			node.m_kind = ibJsonNode::Object;
			const unsigned int count = Chance(6) ? 0u : 1u + Below(5);
			if (count == 0) m_census.m_emptyObjects++;
			for (unsigned int i = 0; i < count; i++)
				node.m_members.emplace_back(Key(i), Value(depth + 1));
			break;
		}
		default: {
			node.m_kind = ibJsonNode::Array;
			const unsigned int count = Chance(6) ? 0u : 1u + Below(5);
			if (count == 0) m_census.m_emptyArrays++;
			for (unsigned int i = 0; i < count; i++)
				node.m_items.push_back(Value(depth + 1));
		}
		}
		return node;
	}

private:
	// Keys of one object never differ only in case - a Structure folds case, and what becomes of two such keys is
	// asked on its own (test_valueJson.cpp). The position in front keeps them apart whatever follows it: a plain
	// name, a name with a space, text that is not ASCII, a word the language reads as a constant.
	wxString Key(unsigned int position)
	{
		wxString key = wxString::Format(wxT("k%u"), position);
		switch (Below(5)) {
		case 0: break;
		case 1: key += wxT(" odd key"); m_census.m_oddKeys++; break;
		case 2: key += wxString::FromUTF8("\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87"); m_census.m_oddKeys++; break;
		case 3: key += wxT("\"q\\"); m_census.m_oddKeys++; break;
		default: key = wxString(Chance(2) ? wxT("null") : wxT("true")) + key; m_census.m_constantKeys++; break;
		}
		return key;
	}

	unsigned int m_state;
};

// ---------------------------------------------------------------------------
//  The expected text - this file's own spelling of JSON, compact
// ---------------------------------------------------------------------------

void AppendExpectedString(wxString& out, const wxString& text)
{
	out += wxT('"');
	for (size_t i = 0; i < text.length(); i++) {
		const wxUniChar::value_type c = text[i].GetValue();
		if      (c == '"')  out += wxT("\\\"");
		else if (c == '\\') out += wxT("\\\\");
		else if (c == '\b') out += wxT("\\b");
		else if (c == '\f') out += wxT("\\f");
		else if (c == '\n') out += wxT("\\n");
		else if (c == '\r') out += wxT("\\r");
		else if (c == '\t') out += wxT("\\t");
		else if (c < 0x20 || c == 0x2028 || c == 0x2029)
			out += wxString::Format(wxT("\\u%04x"), static_cast<unsigned>(c));
		else
			out += text[i];
	}
	out += wxT('"');
}

void AppendExpected(wxString& out, const ibJsonNode& node)
{
	switch (node.m_kind) {
	case ibJsonNode::Null:    out += wxT("null"); break;
	case ibJsonNode::Boolean: out += node.m_flag ? wxT("true") : wxT("false"); break;
	case ibJsonNode::Number:  out += wxString::FromAscii(node.m_digits.c_str()); break;
	case ibJsonNode::String:  AppendExpectedString(out, node.m_text); break;
	case ibJsonNode::Object:
		out += wxT('{');
		for (size_t i = 0; i < node.m_members.size(); i++) {
			if (i > 0) out += wxT(',');
			AppendExpectedString(out, node.m_members[i].first);
			out += wxT(':');
			AppendExpected(out, node.m_members[i].second);
		}
		out += wxT('}');
		break;
	case ibJsonNode::Array:
		out += wxT('[');
		for (size_t i = 0; i < node.m_items.size(); i++) {
			if (i > 0) out += wxT(',');
			AppendExpected(out, node.m_items[i]);
		}
		out += wxT(']');
		break;
	}
}

// ---------------------------------------------------------------------------
//  The tree as engine values, and an engine value held against the tree
// ---------------------------------------------------------------------------

ibValue Build(const ibJsonNode& node)
{
	switch (node.m_kind) {
	case ibJsonNode::Null:    return ibValue(ibValueTypes::TYPE_NULL);
	case ibJsonNode::Boolean: return ibValue(node.m_flag);
	case ibJsonNode::Number:  return ibValue(ibNumber(wxString::FromAscii(node.m_digits.c_str())));
	case ibJsonNode::String:  return ibValue(node.m_text);
	case ibJsonNode::Object: {
		const ibValuePtr<ibValueStructure> object(new ibValueStructure());   // born held
		for (const auto& member : node.m_members)
			object->Insert(ibValue(member.first), Build(member.second));
		return object;
	}
	default: {
		const ibValuePtr<ibValueArray> list(new ibValueArray());
		for (const ibJsonNode& item : node.m_items)
			list->Add(Build(item));
		return list;
	}
	}
}

// Empty when the value IS the tree; otherwise where it is not, as a path a person can follow.
wxString Differs(const ibValue& value, const ibJsonNode& node, const wxString& path)
{
	switch (node.m_kind) {
	case ibJsonNode::Null:
		return value.IsNull() ? wxString() : path + wxT(": not Null");
	case ibJsonNode::Boolean:
		return value.GetType() == ibValueTypes::TYPE_BOOLEAN && value.GetBoolean() == node.m_flag
			? wxString() : path + wxT(": not the Boolean");
	case ibJsonNode::Number: {
		if (value.GetType() != ibValueTypes::TYPE_NUMBER) return path + wxT(": not a Number");
		const wxString read = value.GetNumber().ToString();
		return read == wxString::FromAscii(node.m_digits.c_str()) ? wxString()
			: path + wxT(": the number reads ") + read + wxT(", written ") + wxString::FromAscii(node.m_digits.c_str());
	}
	case ibJsonNode::String:
		if (value.GetType() != ibValueTypes::TYPE_STRING) return path + wxT(": not a String");
		return value.GetString() == node.m_text ? wxString() : path + wxT(": the string differs");
	case ibJsonNode::Object: {
		const ibValueStructure* object = dynamic_cast<const ibValueStructure*>(value.GetRef());
		if (object == nullptr) return path + wxT(": not a Structure");
		if (object->Entries().size() != node.m_members.size())
			return path + wxString::Format(wxT(": %u members, written %u"),
				static_cast<unsigned>(object->Entries().size()), static_cast<unsigned>(node.m_members.size()));
		for (size_t i = 0; i < node.m_members.size(); i++) {
			if (object->Entries()[i].first.GetString() != node.m_members[i].first)
				return path + wxString::Format(wxT(": member %u is not the key written there"), static_cast<unsigned>(i));
			const wxString inner = Differs(object->Entries()[i].second, node.m_members[i].second,
				path + wxT(".") + node.m_members[i].first);
			if (!inner.empty()) return inner;
		}
		return wxString();
	}
	default: {
		const ibValueArray* list = dynamic_cast<const ibValueArray*>(value.GetRef());
		if (list == nullptr) return path + wxT(": not an Array");
		if (list->Values().size() != node.m_items.size()) return path + wxT(": the array's length differs");
		for (size_t i = 0; i < node.m_items.size(); i++) {
			const wxString inner = Differs(list->Values()[i], node.m_items[i], path + wxString::Format(wxT("[%u]"), static_cast<unsigned>(i)));
			if (!inner.empty()) return inner;
		}
		return wxString();
	}
	}
}

std::string Shown(const wxString& text)
{
	const wxScopedCharBuffer utf8 = text.Left(600).utf8_str();
	return std::string(utf8.data(), utf8.length());
}

wxString Written(const ibValue& value, ibJsonFormatting formatting)
{
	ibValueJsonWriter writer;
	writer.SetFormatting(formatting);
	writer.WriteValue(value);
	return writer.Close();
}

constexpr unsigned int kSeeds = 300u;

} // namespace

// 1 + 2. Out and back. The writer's text is the text computed here, and the reader's value is the tree - to the
// digit, which is the reason this reader exists in the shape it has.
TEST(JsonRoundTrip, WhatIsWrittenIsTheExpectedTextAndReadsBackAsTheTree)
{
	for (unsigned int seed = 1; seed <= kSeeds; seed++) {
		ibJsonGen gen(seed);
		const ibJsonNode tree = gen.Value(0);
		wxString expected;
		AppendExpected(expected, tree);

		const wxString written = Written(Build(tree), ibJsonFormatting_Compact);
		ASSERT_EQ(written, expected) << "seed " << seed << "\nexpected: " << Shown(expected) << "\nwritten:  " << Shown(written);

		// The indented text is the same document: read back, it is the same tree.
		for (const wxString& text : { written, Written(Build(tree), ibJsonFormatting_Indented) }) {
			ibValueJsonReader reader;
			reader.SetText(text);
			const wxString differs = Differs(reader.ReadValue(), tree, wxT("$"));
			ASSERT_TRUE(differs.empty()) << "seed " << seed << ": " << Shown(differs) << "\ntext: " << Shown(text);
			ASSERT_FALSE(reader.Read()) << "seed " << seed << ": the value was the whole document";
		}
	}
}

// 3. Damaged text. Either it is read or it is refused with a sentence - by ibBackendException and nothing else -
// and the parser library, asked the same text, agrees on which. What is read is walked: every start has its end,
// Skip() lands on it, and the whole value is built and is stable. (A whole value that is REFUSED is the next
// test's: damage almost never produces one, and a property hardly asked is a property not held.)
TEST(JsonRoundTrip, DamagedTextIsReadOrRefusedAndNeverAnythingElse)
{
	const wxString structural = wxT("{}[],:\"\\ntf0-.eE ");
	int accepted = 0, refused = 0;

	for (unsigned int seed = 1; seed <= kSeeds; seed++) {
		ibJsonGen gen(seed * 7919u);
		wxString clean;
		AppendExpected(clean, gen.Value(0));

		for (int round = 0; round < 12; round++) {
			wxString text = clean;
			const unsigned int damages = 1u + gen.Below(3);
			for (unsigned int d = 0; d < damages && !text.empty(); d++) {
				const size_t at = gen.Below(static_cast<unsigned int>(text.length()));
				switch (gen.Below(5)) {
				case 0: text.erase(at, 1); break;                                                     // a character cut
				case 1: text.insert(at, wxString(text[at])); break;                                   // doubled
				case 2: text[at] = structural[gen.Below(static_cast<unsigned int>(structural.length()))]; break;
				case 3: text.insert(at, text.Mid(at, 1 + gen.Below(12))); break;                       // a slice repeated
				default: text.Truncate(at); break;                                                   // the end torn off
				}
			}

			const wxScopedCharBuffer utf8 = text.utf8_str();
			const bool libraryAccepts = nlohmann::json::accept(utf8.data(), utf8.data() + utf8.length());

			ibValueJsonReader reader;
			bool readerAccepts = false;
			wxString why;
			try {
				reader.SetText(text);
				readerAccepts = true;
			}
			catch (const ibBackendException& refusal) {
				why = refusal.GetErrorDescription();
			}
			catch (const std::exception& other) {
				FAIL() << "seed " << seed << ": a refusal that is not the backend's own - " << other.what() << "\ntext: " << Shown(text);
			}

			if (!readerAccepts) {
				refused++;
				ASSERT_FALSE(why.empty()) << "seed " << seed << ": refused without a sentence\ntext: " << Shown(text);
				// Refused by the reader and accepted by the library is legitimate for ONE reason: a limit this
				// reader declares on a number. Anything else is the two disagreeing about what JSON is.
				if (libraryAccepts)
					ASSERT_TRUE(why.Contains(wxT("number"))) << "seed " << seed << ": " << Shown(why) << "\ntext: " << Shown(text);
				continue;
			}
			accepted++;
			ASSERT_TRUE(libraryAccepts) << "seed " << seed << ": read, and the parser library refuses it\ntext: " << Shown(text);

			// The walk: kinds in order, every start matched by its own end.
			std::vector<ibJsonValueType> kinds;
			while (reader.Read()) kinds.push_back(reader.CurrentType());
			ASSERT_FALSE(kinds.empty());
			std::vector<size_t> open, closes(kinds.size(), 0);
			for (size_t i = 0; i < kinds.size(); i++) {
				if (kinds[i] == ibJsonValueType_ObjectStart || kinds[i] == ibJsonValueType_ArrayStart)
					open.push_back(i);
				else if (kinds[i] == ibJsonValueType_ObjectEnd || kinds[i] == ibJsonValueType_ArrayEnd) {
					ASSERT_FALSE(open.empty()) << "seed " << seed << ": an end with nothing open\ntext: " << Shown(text);
					ASSERT_EQ(kinds[open.back()] == ibJsonValueType_ObjectStart, kinds[i] == ibJsonValueType_ObjectEnd);
					closes[open.back()] = i;
					open.pop_back();
				}
			}
			ASSERT_TRUE(open.empty()) << "seed " << seed << ": read, with something left open\ntext: " << Shown(text);

			// Skip() from a start lands on ITS end: what is left to read afterwards says so.
			for (size_t i = 0; i < kinds.size(); i++) {
				if (kinds[i] != ibJsonValueType_ObjectStart && kinds[i] != ibJsonValueType_ArrayStart) continue;
				ibValueJsonReader skipper;
				skipper.SetText(text);
				for (size_t step = 0; step <= i; step++) ASSERT_TRUE(skipper.Read());
				skipper.Skip();
				size_t left = 0;
				while (skipper.Read()) left++;
				ASSERT_EQ(left, kinds.size() - closes[i] - 1) << "seed " << seed << ": Skip() from token " << i << "\ntext: " << Shown(text);
				break;   // the first container of a document is enough per text; the seeds vary which it is
			}

			// The whole value: built, or refused with a sentence (damage can make two keys differ only in case).
			ibValueJsonReader whole;
			whole.SetText(text);
			ibValue value;
			bool built = false;
			try {
				value = whole.ReadValue();
				built = true;
			}
			catch (const ibBackendException& refusal) {
				ASSERT_FALSE(refusal.GetErrorDescription().empty()) << "seed " << seed << "\ntext: " << Shown(text);
			}
			catch (const std::exception& other) {
				FAIL() << "seed " << seed << ": ReadValue() left by something that is not the backend's own - " << other.what() << "\ntext: " << Shown(text);
			}
			if (!built) continue;

			// …and what was built is stable: written and read again it writes the same text. Apart from the try
			// above, so that a writer handing over a text the reader refuses is reported as THAT.
			try {
				const wxString once = Written(value, ibJsonFormatting_Compact);
				ibValueJsonReader again;
				again.SetText(once);
				ASSERT_EQ(Written(again.ReadValue(), ibJsonFormatting_Compact), once) << "seed " << seed << "\ntext: " << Shown(text);
			}
			catch (const std::exception& error) {
				FAIL() << "seed " << seed << ": what was read could not be written and read again - " << error.what() << "\ntext: " << Shown(text);
			}
		}
	}

	// Both sides of the property have to have been exercised, or it proved nothing.
	EXPECT_GT(accepted, 100) << "the damage never leaves a text readable - the walk above was hardly asked";
	EXPECT_GT(refused, 1000) << "the damage hardly ever breaks a text - the refusals above were hardly asked";
}

// 4. A whole value that is refused. The one refusal ReadValue() has for a text that parses is two keys that
// differ only in case, so one is planted: the first object with a member gets its first key again, in the other
// case, as its LAST member - the refusal then comes with the walk deep inside the value, which is where a reader
// that moved would be found. It used to move: refused once and asked again, the same call succeeded.
TEST(JsonRoundTrip, ARefusedWholeValueLeavesTheReaderWhereItStood)
{
	struct ibTwin {
		static bool Plant(ibJsonNode& node, wxString& first, wxString& twin)
		{
			if (node.m_kind == ibJsonNode::Object && !node.m_members.empty()) {
				first = node.m_members.front().first;
				twin = first.Upper();
				node.m_members.emplace_back(twin, ibJsonNode());
				return true;
			}
			for (auto& member : node.m_members)
				if (Plant(member.second, first, twin)) return true;
			for (auto& item : node.m_items)
				if (Plant(item, first, twin)) return true;
			return false;
		}
	};

	int asked = 0;
	for (unsigned int seed = 1; seed <= kSeeds; seed++) {
		ibJsonGen gen(seed * 104729u);
		// More than half of what the generator makes is a scalar, or containers with nothing in them: draw until
		// there is an object with a member to plant into.
		ibJsonNode tree;
		wxString first, twin;
		bool planted = false;
		for (int draw = 0; draw < 20 && !planted; draw++) {
			tree = gen.Value(0);
			planted = ibTwin::Plant(tree, first, twin);
		}
		if (!planted) continue;
		ASSERT_NE(first, twin) << "seed " << seed << ": the generator's keys all carry a letter";
		asked++;

		wxString text;
		AppendExpected(text, tree);

		ibValueJsonReader reader;
		reader.SetText(text);                                // the TEXT is sound: token by token it reads
		for (int attempt = 0; attempt < 2; attempt++) {
			try {
				reader.ReadValue();
				FAIL() << "seed " << seed << ", attempt " << attempt << ": two keys that differ only in case were read into one Structure\ntext: " << Shown(text);
			}
			catch (const ibBackendException& refusal) {
				const wxString why = refusal.GetErrorDescription();
				ASSERT_TRUE(why.Contains(first) && why.Contains(twin)) << "seed " << seed << ": the refusal names both keys - " << Shown(why);
			}
			ASSERT_EQ(reader.CurrentType(), ibJsonValueType_None) << "seed " << seed << ", attempt " << attempt << ": a refused ReadValue() moved the reader\ntext: " << Shown(text);
		}

		// The same, standing ON the value rather than before it: the root's start is read, the value is refused,
		// and the reader still stands on that start.
		ASSERT_TRUE(reader.Read());
		const ibJsonValueType root = reader.CurrentType();
		ASSERT_TRUE(root == ibJsonValueType_ObjectStart || root == ibJsonValueType_ArrayStart)
			<< "seed " << seed << ": a refused ReadValue() moved the reader - the first Read() after it is not the document's start\ntext: " << Shown(text);
		try {
			reader.ReadValue();
			FAIL() << "seed " << seed << ": read whole from its start\ntext: " << Shown(text);
		}
		catch (const ibBackendException&) {}
		ASSERT_EQ(reader.CurrentType(), root) << "seed " << seed << ": a refused ReadValue() moved the reader off the start it stood on\ntext: " << Shown(text);

		// …and the walk it was left in is the whole walk.
		size_t starts = 1, ends = 0;
		while (reader.Read()) {
			const ibJsonValueType kind = reader.CurrentType();
			if (kind == ibJsonValueType_ObjectStart || kind == ibJsonValueType_ArrayStart) starts++;
			if (kind == ibJsonValueType_ObjectEnd || kind == ibJsonValueType_ArrayEnd) ends++;
		}
		ASSERT_GT(starts, 0u);
		ASSERT_EQ(starts, ends) << "seed " << seed << "\ntext: " << Shown(text);
	}
	EXPECT_GT(asked, 250) << "hardly a tree had an object with a member - the property above was hardly asked";
}

// 5. Documents shaped to be expensive. Each is linear work; every one of these shapes has been quadratic - or
// worse - in some reader, and one of them in this one (every key listed twice, before the Structure's own lookup
// was asked which entry matched). The bound is an order of magnitude above what the linear road takes in a
// checked build and an order below what the quadratic one took.
TEST(JsonRoundTrip, ExpensiveShapesStayCheap)
{
	const auto seconds = [](const wxString& text) {
		const auto started = std::chrono::steady_clock::now();
		ibValueJsonReader reader;
		reader.SetText(text);
		const ibValue value = reader.ReadValue();
		const wxString written = Written(value, ibJsonFormatting_Compact);
		EXPECT_FALSE(written.empty());
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	};

	wxString twice = wxT("{");
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < 12000; i++)
			twice += wxString::Format(wxT("%s\"key%d\":%d"), twice.length() > 1 ? wxT(",") : wxT(""), i, pass);
	twice += wxT("}");
	EXPECT_LT(seconds(twice), 10.0) << "every key listed twice";

	wxString numbers = wxT("[");
	for (int i = 0; i < 40000; i++)
		numbers += wxString::Format(wxT("%s%d.%02d"), i > 0 ? wxT(",") : wxT(""), i, i % 100);
	numbers += wxT("]");
	EXPECT_LT(seconds(numbers), 10.0) << "forty thousand amounts";

	wxString strings = wxT("[");
	for (int i = 0; i < 20000; i++)
		strings += wxString(i > 0 ? wxT(",") : wxT("")) + wxT("\"line\\n\\t\\\"quoted\\\" \\u0416\"");
	strings += wxT("]");
	EXPECT_LT(seconds(strings), 10.0) << "twenty thousand strings, each with escapes";

	wxString nested;   // an object in an array a hundred times over: two hundred levels, inside the bound declared
	for (int i = 0; i < 100; i++) nested += wxT("{\"a\":[");
	nested += wxT("1");
	for (int i = 0; i < 100; i++) nested += wxT("]}");
	EXPECT_LT(seconds(nested), 10.0) << "two hundred levels";
}

// The generator is asked what it made - a green run above means what it looks like only if the awkward shapes
// were really among the documents.
TEST(JsonRoundTrip, TheGeneratorReallyProducesTheAwkwardShapes)
{
	ibJsonCensus total;
	for (unsigned int seed = 1; seed <= kSeeds; seed++) {
		ibJsonGen gen(seed);
		gen.Value(0);
		const ibJsonCensus& c = gen.m_census;
		if (c.m_deepest > total.m_deepest) total.m_deepest = c.m_deepest;
		total.m_emptyObjects += c.m_emptyObjects;   total.m_emptyArrays += c.m_emptyArrays;
		total.m_escaped += c.m_escaped;             total.m_surrogates += c.m_surrogates;
		total.m_nonAscii += c.m_nonAscii;           total.m_fractions += c.m_fractions;
		total.m_longIntegers += c.m_longIntegers;   total.m_negatives += c.m_negatives;
		total.m_oddKeys += c.m_oddKeys;             total.m_constantKeys += c.m_constantKeys;
		total.m_nulls += c.m_nulls;                 total.m_emptyStrings += c.m_emptyStrings;
	}
	EXPECT_GE(total.m_deepest, 5) << "nesting";
	EXPECT_GT(total.m_emptyObjects, 10);
	EXPECT_GT(total.m_emptyArrays, 10);
	EXPECT_GT(total.m_escaped, 100) << "characters that must be escaped";
	EXPECT_GT(total.m_surrogates, 20) << "characters outside the basic plane";
	EXPECT_GT(total.m_nonAscii, 20);
	EXPECT_GT(total.m_fractions, 50);
	EXPECT_GT(total.m_longIntegers, 20) << "integers past 64 bits";
	EXPECT_GT(total.m_negatives, 20);
	EXPECT_GT(total.m_oddKeys, 50) << "keys that are not names";
	EXPECT_GT(total.m_constantKeys, 20) << "keys spelled like the language's constants";
	EXPECT_GT(total.m_nulls, 20);
	EXPECT_GT(total.m_emptyStrings, 10);
}
