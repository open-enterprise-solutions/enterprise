// =============================================================================
// XMLReader / XMLWriter - the value types a script uses to read and write XML text: the XMLNodeType
// enumeration an XMLReader stands its node kind on, then the reader itself.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <wx/ffile.h>
#include <wx/filename.h>

#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/system/systemManagerEnum.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueXml.h"
#include "core/xml/xmlText.h"

namespace {

// The script's order and spelling. Both the numbers and the names are written here as
// literals, independently of ibXmlNodeType and of the mechanism under test, so a shifted or gapped
// enum - or two swapped AddEnumeration lines - fails against this table rather than agreeing with itself.
struct ExpectedMember {
	int number;
	const ibChar* name;
};

const ExpectedMember kExpected[] = {
	{ 1,  ibT("None") },
	{ 2,  ibT("StartElement") },
	{ 3,  ibT("EndElement") },
	{ 4,  ibT("Text") },
	{ 5,  ibT("Whitespace") },
	{ 6,  ibT("CDATASection") },
	{ 7,  ibT("Comment") },
	{ 8,  ibT("ProcessingInstruction") },
	{ 9,  ibT("XMLDeclaration") },
	{ 10, ibT("DocumentTypeDefinition") },
	{ 11, ibT("Attribute") },
	{ 12, ibT("EntityReference") },
	{ 13, ibT("Entity") },
	{ 14, ibT("EndEntity") },
	{ 15, ibT("Notation") },
};

} // namespace

// All fifteen, in their order, under their names - so that code written against the list (and the reader's own
// dispatch by name) matches it member for member.
TEST(XmlNodeType, TheFifteenKindsKeepTheirNamesAndNumbers) {
	ibValueEnumXmlNodeType owner;
	owner.CreateEnumeration();

	ASSERT_EQ(std::size(kExpected), owner.GetEnumCount());

	for (unsigned int i = 0; i < owner.GetEnumCount(); ++i) {
		EXPECT_EQ(kExpected[i].number, static_cast<int>(owner.GetEnumValue(i))) << "member #" << i;
		EXPECT_EQ(ibString(kExpected[i].name), owner.GetEnumName(i)) << "member #" << i;
	}

	// A member also builds on its own from the registered type, the door a script's `New` reaches.
	const ibValue started = ibValue::CreateEnumObject<ibValueEnumXmlNodeType>(ibXmlNodeType_StartElement);
	EXPECT_EQ(ibString(ibT("XMLNodeType")), started.GetClassName());
	EXPECT_FALSE(started.IsEmpty());

	// And the owner resolves a member BY NAME through the same public door a picker searches by
	// (FindValue matches on the identifier, not the caption) - "StartElement" is not a substring of
	// any other member's name, so a single, exact hit is expected; two swapped AddEnumeration lines
	// would hand back the wrong member number here too.
	std::vector<ibValue> byName;
	ASSERT_TRUE(owner.FindValue(ibT("StartElement"), byName));
	ASSERT_EQ(1u, byName.size());
	EXPECT_EQ(static_cast<int>(ibXmlNodeType_StartElement), byName.front().GetInteger());
}

// =============================================================================
// XMLReader - node by node, on expat.
// =============================================================================

namespace {

struct ibNode { ibXmlNodeType type; ibString name; ibString value; };

std::vector<ibNode> Walk(ibValueXmlReader& reader) {
	std::vector<ibNode> seen;
	while (reader.Read())
		seen.push_back({ reader.NodeType(), reader.Name(), reader.Value() });
	return seen;
}

std::vector<ibNode> WalkText(const ibString& text, bool ignoreWhitespace = true) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetIgnoreWhitespace(ignoreWhitespace);
	reader->SetText(text);
	return Walk(*reader);
}

void ExpectNodes(const std::vector<ibNode>& seen, const std::vector<ibNode>& want) {
	ASSERT_EQ(seen.size(), want.size());
	for (size_t i = 0; i < want.size(); ++i) {
		EXPECT_EQ(seen[i].type, want[i].type) << i;
		EXPECT_EQ(seen[i].name, want[i].name) << i;
		EXPECT_EQ(seen[i].value, want[i].value) << i;
	}
}

// Characters by their code points - the expected text is spelled here, not taken from any decoder.
ibString FromCodePoints(std::initializer_list<unsigned int> points) {
	ibString text;
	for (const unsigned int point : points)
		text.AppendCodepoint(point);
	return text;
}

struct XmlTempFile {
	ibString path = wxFileName::CreateTempFileName(wxT("oes"));
	~XmlTempFile() { if (!path.empty()) wxRemoveFile(path); }

	void Put(const std::string& bytes) const {
		wxFFile f(path, wxT("wb"));
		ASSERT_TRUE(f.IsOpened());
		f.Write(bytes.data(), bytes.size());
	}
};

// What the reader raises, as text: the tests look at the words a person would read.
template <typename F>
ibString RefusalOf(F&& body) {
	try {
		body();
	}
	catch (const ibCoreException& error) {
		return error.GetErrorDescription();
	}
	return ibString();
}

} // namespace

TEST(XmlReader, EveryKindOfNodeArrivesInOrderAndAnEmptyElementIsAPair) {
	ExpectNodes(WalkText(ibT("<a x=\"1\"><b/>text<!--c--><?pi d?><![CDATA[<z>]]></a>")), {
		{ ibXmlNodeType_StartElement, ibT("a"), ibT("") },
		{ ibXmlNodeType_StartElement, ibT("b"), ibT("") },
		{ ibXmlNodeType_EndElement, ibT("b"), ibT("") },
		{ ibXmlNodeType_Text, ibT(""), ibT("text") },
		{ ibXmlNodeType_Comment, ibT(""), ibT("c") },
		{ ibXmlNodeType_ProcessingInstruction, ibT("pi"), ibT("d") },
		{ ibXmlNodeType_CDATASection, ibT(""), ibT("<z>") },
		{ ibXmlNodeType_EndElement, ibT("a"), ibT("") },
	});
}

// <b/> and <b></b> are the same pair of nodes; only IsEmptyElement tells them apart,
// and it says so on both nodes of the pair. Depth counts from the root at 0; the line is the node's own.
TEST(XmlReader, AnEmptyElementSaysSoOnBothNodesAndDepthAndLineAreTheNodes) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<a>\n<b/><c></c></a>"));
	struct Seen { ibXmlNodeType type; ibString name; bool empty; int depth; unsigned long long line, column; };
	std::vector<Seen> seen;
	while (reader->Read())
		seen.push_back({ reader->NodeType(), reader->Name(), reader->Current().m_emptyElement,
			reader->Current().m_depth, reader->Current().m_line, reader->Current().m_column });
	// Line 2 is `<b/><c></c></a>`: <b/> at column 1, <c> at 5, </c> at 8, </a> at 12. Both halves of <b/>
	// stand where the one tag does.
	const std::vector<Seen> want = {
		{ ibXmlNodeType_StartElement, ibT("a"), false, 0, 1, 1 },
		{ ibXmlNodeType_StartElement, ibT("b"), true, 1, 2, 1 },
		{ ibXmlNodeType_EndElement, ibT("b"), true, 1, 2, 1 },
		{ ibXmlNodeType_StartElement, ibT("c"), false, 1, 2, 5 },
		{ ibXmlNodeType_EndElement, ibT("c"), false, 1, 2, 8 },
		{ ibXmlNodeType_EndElement, ibT("a"), false, 0, 2, 12 },
	};
	ASSERT_EQ(seen.size(), want.size());
	for (size_t i = 0; i < want.size(); ++i) {
		EXPECT_EQ(seen[i].type, want[i].type) << i;
		EXPECT_EQ(seen[i].name, want[i].name) << i;
		EXPECT_EQ(seen[i].empty, want[i].empty) << i;
		EXPECT_EQ(seen[i].depth, want[i].depth) << i;
		EXPECT_EQ(seen[i].line, want[i].line) << i;
		EXPECT_EQ(seen[i].column, want[i].column) << i;
	}
}

TEST(XmlReader, TheDeclarationIsNotANodeButItsWordsAreRead) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><a/>"));
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(reader->XMLVersion(), ibString(ibT("1.0")));
	EXPECT_EQ(reader->XMLEncoding(), ibString(ibT("UTF-8")));
	EXPECT_TRUE(reader->Standalone());

	reader->SetText(ibT("<?xml version=\"1.0\" encoding=\"UTF-8\"?><a/>"));
	ASSERT_TRUE(reader->Read());
	EXPECT_FALSE(reader->Standalone());
}

TEST(XmlReader, WhitespaceBetweenElementsIsDroppedUnlessAskedFor) {
	const ibString text = ibT("<a>\n  <b>x y</b>\n</a>");
	ExpectNodes(WalkText(text), {
		{ ibXmlNodeType_StartElement, ibT("a"), ibT("") },
		{ ibXmlNodeType_StartElement, ibT("b"), ibT("") },
		{ ibXmlNodeType_Text, ibT(""), ibT("x y") },
		{ ibXmlNodeType_EndElement, ibT("b"), ibT("") },
		{ ibXmlNodeType_EndElement, ibT("a"), ibT("") },
	});
	ExpectNodes(WalkText(text, false), {
		{ ibXmlNodeType_StartElement, ibT("a"), ibT("") },
		{ ibXmlNodeType_Whitespace, ibT(""), ibT("\n  ") },
		{ ibXmlNodeType_StartElement, ibT("b"), ibT("") },
		{ ibXmlNodeType_Text, ibT(""), ibT("x y") },
		{ ibXmlNodeType_EndElement, ibT("b"), ibT("") },
		{ ibXmlNodeType_Whitespace, ibT(""), ibT("\n") },
		{ ibXmlNodeType_EndElement, ibT("a"), ibT("") },
	});
}

// 200 000 two-byte characters behind a three-byte tag: the 64 KB chunks cut the text many times, and in the
// middle of a character too. It is still ONE text node - from a string and from a file alike.
TEST(XmlReader, ATextCutByChunkBoundariesIsOneNode) {
	const ibString body(static_cast<ibChar>(0x0404), 200000);   // U+0404, two bytes in UTF-8
	const ibString text = ibT("<a>") + body + ibT("</a>");

	ExpectNodes(WalkText(text), {
		{ ibXmlNodeType_StartElement, ibT("a"), ibT("") },
		{ ibXmlNodeType_Text, ibT(""), body },
		{ ibXmlNodeType_EndElement, ibT("a"), ibT("") },
	});

	XmlTempFile f;
	f.Put(text.ToUtf8());
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->OpenFile(f.path);
	ExpectNodes(Walk(*reader), {
		{ ibXmlNodeType_StartElement, ibT("a"), ibT("") },
		{ ibXmlNodeType_Text, ibT(""), body },
		{ ibXmlNodeType_EndElement, ibT("a"), ibT("") },
	});
}

// The bytes are written out by Python, not by the engine's tables:
//   python3 -c "t='<?xml version=\"1.0\" encoding=\"windows-1251\"?><a>\u0417\u0432\u0456\u0442 \u0414\u041f\u0421: \u0404\u0434\u0438\u043d\u0438\u0439 \u043f\u043e\u0434\u0430\u0442\u043e\u043a</a>'; print(', '.join('0x%02X'%b for b in t.encode('cp1251')))"
//   python3 -c "t='<?xml version=\"1.0\" encoding=\"cp866\"?><a>\u0404\u0434\u0438\u043d\u0438\u0439 \u043f\u043e\u0434\u0430\u0442\u043e\u043a</a>'; print(', '.join('0x%02X'%b for b in t.encode('cp866')))"
// (cp866 has U+0404, at 0xF2, but not the Ukrainian U+0456 - so the second text is one without it.)
TEST(XmlReader, Windows1251AndCp866FilesReadIntoTheSameCharacters) {
	const unsigned char w1251[] = {
		0x3C, 0x3F, 0x78, 0x6D, 0x6C, 0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x3D, 0x22, 0x31, 0x2E, 0x30, 0x22, 0x20,
		0x65, 0x6E, 0x63, 0x6F, 0x64, 0x69, 0x6E, 0x67, 0x3D, 0x22, 0x77, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x2D, 0x31, 0x32,
		0x35, 0x31, 0x22, 0x3F, 0x3E, 0x3C, 0x61, 0x3E, 0xC7, 0xE2, 0xB3, 0xF2, 0x20, 0xC4, 0xCF, 0xD1, 0x3A, 0x20, 0xAA, 0xE4,
		0xE8, 0xED, 0xE8, 0xE9, 0x20, 0xEF, 0xEE, 0xE4, 0xE0, 0xF2, 0xEE, 0xEA, 0x3C, 0x2F, 0x61, 0x3E,
	};
	const unsigned char cp866[] = {
		0x3C, 0x3F, 0x78, 0x6D, 0x6C, 0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x3D, 0x22, 0x31, 0x2E, 0x30, 0x22, 0x20,
		0x65, 0x6E, 0x63, 0x6F, 0x64, 0x69, 0x6E, 0x67, 0x3D, 0x22, 0x63, 0x70, 0x38, 0x36, 0x36, 0x22, 0x3F, 0x3E, 0x3C, 0x61,
		0x3E, 0xF2, 0xA4, 0xA8, 0xAD, 0xA8, 0xA9, 0x20, 0xAF, 0xAE, 0xA4, 0xA0, 0xE2, 0xAE, 0xAA, 0x3C, 0x2F, 0x61, 0x3E,
	};
	// The two texts above, by code point.
	const ibString ukrainian = FromCodePoints({ 0x0417, 0x0432, 0x0456, 0x0442, 0x20, 0x0414, 0x041F, 0x0421, 0x3A, 0x20,
		0x0404, 0x0434, 0x0438, 0x043D, 0x0438, 0x0439, 0x20, 0x043F, 0x043E, 0x0434, 0x0430, 0x0442, 0x043E, 0x043A });
	const ibString withoutI = FromCodePoints({ 0x0404, 0x0434, 0x0438, 0x043D, 0x0438, 0x0439, 0x20, 0x043F, 0x043E, 0x0434,
		0x0430, 0x0442, 0x043E, 0x043A });

	const struct { std::string bytes; ibString text; const ibChar* encoding; } cases[] = {
		{ std::string(reinterpret_cast<const char*>(w1251), sizeof(w1251)), ukrainian, ibT("windows-1251") },
		{ std::string(reinterpret_cast<const char*>(cp866), sizeof(cp866)), withoutI, ibT("cp866") },
	};
	for (const auto& one : cases) {
		XmlTempFile f;
		f.Put(one.bytes);
		ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
		reader->OpenFile(f.path);
		ExpectNodes(Walk(*reader), {
			{ ibXmlNodeType_StartElement, ibT("a"), ibT("") },
			{ ibXmlNodeType_Text, ibT(""), one.text },
			{ ibXmlNodeType_EndElement, ibT("a"), ibT("") },
		});
		EXPECT_EQ(reader->XMLEncoding(), ibString(one.encoding));
	}
}

// A string is characters already: the windows-1251 its declaration names is what the text once WAS, and
// reading its UTF-8 bytes as 1251 again would turn one character into two.
TEST(XmlReader, AStringsDeclaredEncodingIsNotAppliedToIt) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<?xml version=\"1.0\" encoding=\"windows-1251\"?><a>") + FromCodePoints({ 0x0404 }) + ibT("</a>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_Text);
	EXPECT_EQ(reader->Value(), FromCodePoints({ 0x0404 }));
	EXPECT_EQ(reader->XMLEncoding(), ibString(ibT("windows-1251")));   // the declaration's word is still read
}

TEST(XmlReader, BadXmlIsRefusedWithItsLineAndColumn) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<a>\n<b></a>"));
	ASSERT_TRUE(reader->Read());   // a
	ASSERT_TRUE(reader->Read());   // b - the nodes before the fault are handed over first
	const ibString refusal = RefusalOf([&] { reader->Read(); });
	EXPECT_TRUE(refusal.StartsWith(ibT("XMLReader: "))) << refusal;
	EXPECT_NE(refusal.Find(ibT("at line 2, column 6")), wxNOT_FOUND) << refusal;   // the wrong name, `a` of `<b></a>`
	// ...and asked again it refuses again, rather than walking on past the fault.
	EXPECT_EQ(RefusalOf([&] { reader->Read(); }), refusal);
}

// A document may not reach outside itself: an external entity is refused where it is used - not read, and not
// quietly left out either. Nor is an entity whose declaration would have come from an external DTD.
TEST(XmlReader, AnExternalEntityIsRefusedNotReadAndNotSkipped) {
	const ibString refusal = RefusalOf([] {
		WalkText(ibT("<!DOCTYPE a [<!ENTITY e SYSTEM \"file:///c:/windows/win.ini\">]><a>&e;</a>"));
	});
	EXPECT_NE(refusal.Find(ibT("external entity")), wxNOT_FOUND) << refusal;
	EXPECT_NE(refusal.Find(ibT("file:///c:/windows/win.ini")), wxNOT_FOUND) << refusal;
	EXPECT_NE(refusal.Find(ibT("at line 1")), wxNOT_FOUND) << refusal;

	// A document that depends on declarations outside itself is refused whole - an entity it cannot resolve
	// would otherwise vanish: in content through the skipped-entity road, in an ATTRIBUTE with no handler at
	// all (`v="1&e;2"` read as "12").
	for (const ibChar* text : {
		ibT("<!DOCTYPE a SYSTEM \"http://example.com/a.dtd\"><a>&e;</a>"),
		ibT("<!DOCTYPE a SYSTEM \"x.dtd\"><a v=\"1&e;2\"/>"),
		ibT("<!DOCTYPE a [<!ENTITY % p SYSTEM \"x.ent\"> %p;]><a v=\"1&e;2\"/>"),
	}) {
		const ibString outside = RefusalOf([&] { WalkText(text); });
		EXPECT_NE(outside.Find(ibT("outside itself")), wxNOT_FOUND) << ibString(text) << " -> " << outside;
	}
	// ...and one that says it stands alone while naming an external DTD: the unknown entity is an error.
	EXPECT_FALSE(RefusalOf([] {
		WalkText(ibT("<?xml version=\"1.0\" standalone=\"yes\"?><!DOCTYPE a SYSTEM \"x.dtd\"><a v=\"1&e;2\"/>"));
	}).empty());

	// A document type with nothing outside the document is read as ever.
	ExpectNodes(WalkText(ibT("<!DOCTYPE a><a/>")), {
		{ ibXmlNodeType_StartElement, ibT("a"), ibT("") },
		{ ibXmlNodeType_EndElement, ibT("a"), ibT("") },
	});

	// The entities of XML itself and those declared inside the document are read as ever.
	ExpectNodes(WalkText(ibT("<!DOCTYPE a [<!ENTITY e \"inner\">]><a>&amp;&e;&#1028;</a>")), {
		{ ibXmlNodeType_StartElement, ibT("a"), ibT("") },
		{ ibXmlNodeType_Text, ibT(""), ibT("&inner") + FromCodePoints({ 0x0404 }) },
		{ ibXmlNodeType_EndElement, ibT("a"), ibT("") },
	});
}

TEST(XmlReader, AnUnknownEncodingIsRefusedWithTheListOfKnownOnes) {
	XmlTempFile f;
	f.Put("<?xml version=\"1.0\" encoding=\"koi8-u\"?><a/>");
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->OpenFile(f.path);
	const ibString refusal = RefusalOf([&] { reader->Read(); });
	EXPECT_NE(refusal.Find(ibT("koi8-u")), wxNOT_FOUND) << refusal;
	EXPECT_NE(refusal.Find(ibXmlCodePage::Supported()), wxNOT_FOUND) << refusal;
}

// The encoding's other names are the engine's (xmlEncoding.h), not expat's: in a declaration only the name expat
// knows is read, and the refusal says which.
TEST(XmlReader, AnEncodingAliasInAFileIsRefusedWithTheNameToWrite) {
	XmlTempFile f;
	f.Put("<?xml version=\"1.0\" encoding=\"utf8\"?><a/>");
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->OpenFile(f.path);
	const ibString refusal = RefusalOf([&] { reader->Read(); });
	EXPECT_NE(refusal.Find(ibT("write it 'UTF-8'")), wxNOT_FOUND) << refusal;
}

TEST(XmlReader, WithoutADeclarationVersionAndEncodingAreEmpty) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<a/>"));
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->XMLVersion(), ibString());
	EXPECT_EQ(reader->XMLEncoding(), ibString());
	EXPECT_FALSE(reader->Standalone());
}

// ⚠ An entity bomb: 300-odd bytes that expand to 100 000 elements, all of them events of ONE chunk. The parser
// pauses while the script has not read what is waiting - the queue stays small, and every node still arrives.
TEST(XmlReader, AnEntityBombIsNotExpandedIntoMemoryWhole) {
	const ibString ten = ibT("<x/><x/><x/><x/><x/><x/><x/><x/><x/><x/>");
	const ibString bomb = ibT("<!DOCTYPE r [")
		ibT("<!ENTITY a \"") + ten + ibT("\">")
		ibT("<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\">")
		ibT("<!ENTITY c \"&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;\">")
		ibT("<!ENTITY d \"&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;\">")
		ibT("<!ENTITY e \"&d;&d;&d;&d;&d;&d;&d;&d;&d;&d;\">")
		ibT("]><r>&e;</r>");

	// The parser pauses at 4096 waiting nodes; the handler that crossed it and the end of an empty element
	// that paused add two more at most.
	const size_t bound = 4096 + 2;

	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(bomb);
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->Name(), ibString(ibT("r")));
	EXPECT_LE(reader->QueuedNodes(), bound);

	size_t nodes = 1, most = reader->QueuedNodes();
	while (reader->Read()) {
		++nodes;
		most = std::max(most, reader->QueuedNodes());
	}
	EXPECT_LE(most, bound);
	EXPECT_EQ(nodes, 2u + 2u * 100000u);   // r's pair and 100 000 pairs of x
}

// The parser pauses between the halves of an empty element as readily as anywhere: 5000 of them in the
// document itself (so that expat can tell `<x/>` from `<x></x>`) cross the pause - and every one keeps its mark,
// and its end the attributes of its own start, not of a neighbour's.
TEST(XmlReader, APauseBetweenTheHalvesOfAnEmptyElementKeepsBothMarked) {
	ibString text = ibT("<r>");
	for (int i = 0; i < 5000; ++i)
		text += ibString::Format(ibT("<x i=\"%d\"/>"), i);
	text += ibT("</r>");

	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(text);
	size_t nodes = 0, empties = 0, most = 0, ends = 0;
	ibString started;
	while (reader->Read()) {
		++nodes;
		most = std::max(most, reader->QueuedNodes());
		if (reader->Current().m_emptyElement)
			++empties;
		if (reader->Name() != ibT("x"))
			continue;
		const ibString i = reader->AttributeValue(ibValue(ibString(ibT("i")))).GetString();
		if (reader->NodeType() == ibXmlNodeType_StartElement) {
			started = i;
		}
		else {
			EXPECT_EQ(i, started) << nodes;
			EXPECT_EQ(i, ibString::Format(ibT("%d"), static_cast<int>(ends))) << nodes;
			++ends;
		}
	}
	EXPECT_EQ(nodes, 2u + 2u * 5000u);
	EXPECT_EQ(empties, 2u * 5000u);
	EXPECT_EQ(ends, 5000u);
	EXPECT_LE(most, 4096u + 2u);
}

TEST(XmlReader, AMissingFileAndNothingOpenAreRefusals) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	const ibString path = wxFileName::GetTempDir() + wxFileName::GetPathSeparator() + ibT("oes-no-such-file.xml");
	const ibString refusal = RefusalOf([&] { reader->OpenFile(path); });
	EXPECT_NE(refusal.Find(path), wxNOT_FOUND) << refusal;

	EXPECT_EQ(RefusalOf([&] { reader->Read(); }), ibString(ibT("XMLReader: no text or file is open")));

	reader->SetText(ibT("<a/>"));
	reader->Close();
	EXPECT_EQ(RefusalOf([&] { reader->Read(); }), ibString(ibT("XMLReader: no text or file is open")));
}

TEST(XmlReader, WhatEachKindOfNodeHas) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetIgnoreWhitespace(false);
	reader->SetText(ibT("<a>t<![CDATA[c]]> <!--m--><?p v?></a>"));
	struct Has { ibXmlNodeType type; bool value, name, characters, whitespace; };
	const std::vector<Has> want = {
		{ ibXmlNodeType_StartElement,          false, true,  false, false },
		{ ibXmlNodeType_Text,                  true,  false, true,  false },
		{ ibXmlNodeType_CDATASection,          true,  false, true,  false },
		{ ibXmlNodeType_Whitespace,            true,  false, true,  true  },
		{ ibXmlNodeType_Comment,               true,  false, false, false },
		{ ibXmlNodeType_ProcessingInstruction, true,  true,  false, false },
		{ ibXmlNodeType_EndElement,            false, true,  false, false },
	};
	for (size_t i = 0; i < want.size(); ++i) {
		ASSERT_TRUE(reader->Read()) << i;
		EXPECT_EQ(reader->NodeType(), want[i].type) << i;
		EXPECT_EQ(reader->HasValue(), want[i].value) << i;
		EXPECT_EQ(reader->HasName(), want[i].name) << i;
		EXPECT_EQ(reader->IsCharacters(), want[i].characters) << i;
		EXPECT_EQ(reader->IsWhitespace(), want[i].whitespace) << i;
	}
	EXPECT_FALSE(reader->Read());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_None);

	// InputEncoding answers what XMLEncoding does.
	reader->SetText(ibT("<?xml version=\"1.0\" encoding=\"UTF-8\"?><a/>"));
	ASSERT_TRUE(reader->Read());
	ibValue input, declared;
	ASSERT_TRUE(reader->GetPropVal(reader->FindProp(ibT("InputEncoding")), input));
	ASSERT_TRUE(reader->GetPropVal(reader->FindProp(ibT("XMLEncoding")), declared));
	EXPECT_EQ(input.GetString(), ibString(ibT("UTF-8")));
	EXPECT_EQ(declared.GetString(), ibString(ibT("UTF-8")));
}

// =============================================================================
// XMLReader - attributes, Skip, MoveToContent; through the doors a script reaches.
// =============================================================================

namespace {

// A method the way a script calls it: by its registered name, the arguments as values.
ibValue CallXml(ibValue& reader, const ibChar* method, std::initializer_list<ibValue> args = {}) {
	std::vector<ibValue> held(args);
	std::vector<ibValue*> params;
	for (ibValue& arg : held)
		params.push_back(&arg);
	ibValue result;
	const long number = reader.FindMethod(method);
	EXPECT_NE(number, wxNOT_FOUND) << ibString(method);
	EXPECT_TRUE(reader.CallAsFunc(number, result, params.data(), static_cast<long>(params.size()))) << ibString(method);
	return result;
}

ibValue PropXml(ibValue& reader, const ibChar* name) {
	ibValue value;
	EXPECT_TRUE(reader.GetPropVal(reader.FindProp(name), value)) << ibString(name);
	return value;
}

bool IsUndefined(const ibValue& value) {
	return value.GetType() == ibValueTypes::TYPE_EMPTY;
}

const ibChar* const kXmlnsNamespace = ibT("http://www.w3.org/2000/xmlns/");

} // namespace

// The declarations are attributes - first, since expat does not say where they stood (here the example has them
// first anyway).
TEST(XmlReader, NamespaceDeclarationsAreAttributesToo) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r xmlns:d=\"urn:d\" a=\"1\" d:b=\"2\"/>"));
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->AttributeCount(), 3);
	EXPECT_EQ(CallXml(reader, ibT("AttributeCount")).GetInteger(), 3);

	const struct { const ibChar* name; const ibChar* local; const ibChar* prefix; const ibChar* uri; const ibChar* value; } want[] = {
		{ ibT("xmlns:d"), ibT("d"), ibT("xmlns"), kXmlnsNamespace, ibT("urn:d") },
		{ ibT("a"),       ibT("a"), ibT(""),      ibT(""),         ibT("1") },
		{ ibT("d:b"),     ibT("b"), ibT("d"),     ibT("urn:d"),    ibT("2") },
	};
	for (int i = 0; i < 3; ++i) {
		EXPECT_EQ(CallXml(reader, ibT("AttributeName"), { ibValue(i) }).GetString(), ibString(want[i].name)) << i;
		EXPECT_EQ(CallXml(reader, ibT("AttributeLocalName"), { ibValue(i) }).GetString(), ibString(want[i].local)) << i;
		EXPECT_EQ(CallXml(reader, ibT("AttributePrefix"), { ibValue(i) }).GetString(), ibString(want[i].prefix)) << i;
		EXPECT_EQ(CallXml(reader, ibT("AttributeNamespaceURI"), { ibValue(i) }).GetString(), ibString(want[i].uri)) << i;
		EXPECT_EQ(CallXml(reader, ibT("AttributeValue"), { ibValue(i) }).GetString(), ibString(want[i].value)) << i;
	}
	EXPECT_EQ(reader->AttributeValue(ibValue(1)).GetString(), ibString(ibT("1")));
	EXPECT_EQ(reader->AttributeValue(ibValue(ibString(ibT("b"))), ibT("urn:d")).GetString(), ibString(ibT("2")));
	EXPECT_EQ(CallXml(reader, ibT("AttributeValue"), { ibValue(ibString(ibT("b"))), ibValue(ibString(ibT("urn:d"))) }).GetString(),
		ibString(ibT("2")));
	EXPECT_EQ(CallXml(reader, ibT("AttributePrefix"), { ibValue(2) }).GetString(), ibString(ibT("d")));

	// The default namespace: the local name is `xmlns` itself and there is no prefix.
	reader->SetText(ibT("<r xmlns=\"urn:x\" a=\"1\"/>"));
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->AttributeCount(), 2);
	EXPECT_EQ(CallXml(reader, ibT("AttributeName"), { ibValue(0) }).GetString(), ibString(ibT("xmlns")));
	EXPECT_EQ(CallXml(reader, ibT("AttributeLocalName"), { ibValue(0) }).GetString(), ibString(ibT("xmlns")));
	EXPECT_EQ(CallXml(reader, ibT("AttributePrefix"), { ibValue(0) }).GetString(), ibString());
	EXPECT_EQ(CallXml(reader, ibT("AttributeNamespaceURI"), { ibValue(0) }).GetString(), ibString(kXmlnsNamespace));
	EXPECT_EQ(CallXml(reader, ibT("AttributeValue"), { ibValue(0) }).GetString(), ibString(ibT("urn:x")));
	// ...and an unprefixed attribute is in NO namespace, the default one notwithstanding.
	EXPECT_EQ(CallXml(reader, ibT("AttributeNamespaceURI"), { ibValue(1) }).GetString(), ibString());
}

// By name without a URI the name is the qualified one as written; with a URI, the local name in that namespace.
// What is not there is Undefined - not an empty string, which an attribute `a=""` would be.
TEST(XmlReader, AMissingAttributeIsUndefinedByNameOrByNumber) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r xmlns:d=\"urn:d\" a=\"1\" d:b=\"2\" e=\"\"/>"));
	ASSERT_TRUE(reader->Read());

	for (const ibChar* method : { ibT("AttributeValue"), ibT("GetAttribute") }) {
		const auto value = [&](std::initializer_list<ibValue> args) { return CallXml(reader, method, args); };
		EXPECT_TRUE(IsUndefined(value({ ibValue(FromCodePoints({ 0x043D, 0x0435, 0x0442 })) }))) << ibString(method);
		EXPECT_TRUE(IsUndefined(value({ ibValue(5) }))) << ibString(method);
		EXPECT_TRUE(IsUndefined(value({ ibValue(4) }))) << ibString(method);       // one past the last
		EXPECT_TRUE(IsUndefined(value({ ibValue(-1) }))) << ibString(method);
		EXPECT_TRUE(IsUndefined(value({ ibValue(0.5) }))) << ibString(method);
		EXPECT_TRUE(IsUndefined(value({ ibValue(ibString(ibT("b"))) }))) << ibString(method);   // its name is d:b
		EXPECT_TRUE(IsUndefined(value({ ibValue(ibString(ibT("b"))), ibValue(ibString(ibT("urn:x"))) }))) << ibString(method);
		EXPECT_TRUE(IsUndefined(value({ ibValue(ibString(ibT("A"))) }))) << ibString(method);   // XML names are case-sensitive
		EXPECT_EQ(value({ ibValue(ibString(ibT("d:b"))) }).GetString(), ibString(ibT("2"))) << ibString(method);
		EXPECT_EQ(value({ ibValue(ibString(ibT("a"))) }).GetString(), ibString(ibT("1"))) << ibString(method);
		EXPECT_EQ(value({ ibValue(ibString(ibT("a"))), ibValue(ibString()) }).GetString(), ibString(ibT("1"))) << ibString(method);
		EXPECT_EQ(value({ ibValue(3.0) }).GetString(), ibString()) << ibString(method);   // e="": there, and empty
		EXPECT_FALSE(IsUndefined(value({ ibValue(ibString(ibT("e"))) }))) << ibString(method);
	}
	EXPECT_TRUE(IsUndefined(reader->AttributeValue(ibValue(ibString(ibT("zz"))))));
	// The names of an attribute that is not there are an EMPTY STRING - a script compares `AttributeName(i) = ""`.
	for (const ibChar* method : { ibT("AttributeName"), ibT("AttributeLocalName"), ibT("AttributePrefix"), ibT("AttributeNamespaceURI") }) {
		for (const ibValue& index : { ibValue(4), ibValue(1.5), ibValue(-1) }) {
			const ibValue name = CallXml(reader, method, { index });
			EXPECT_EQ(name.GetType(), ibValueTypes::TYPE_STRING) << ibString(method);
			EXPECT_EQ(name.GetString(), ibString()) << ibString(method);
		}
	}
}

// A number or a name, nothing else: a Boolean or a forgotten argument is a mistake said, not a quiet Undefined.
TEST(XmlReader, AnAttributeIsAskedForByNumberOrNameAndNothingElse) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r a=\"1\"/>"));
	ASSERT_TRUE(reader->Read());
	for (const ibValue& wrong : { ibValue(true), ibValue() }) {
		const ibString refusal = RefusalOf([&] { reader->AttributeValue(wrong); });
		EXPECT_TRUE(refusal.StartsWith(ibT("XMLReader: "))) << refusal;
		EXPECT_NE(refusal.Find(ibT("number or its name")), wxNOT_FOUND) << refusal;
	}
	const ibString byName = RefusalOf([&] { CallXml(reader, ibT("AttributeName"), { ibValue(ibString(ibT("a"))) }); });
	EXPECT_TRUE(byName.StartsWith(ibT("XMLReader: "))) << byName;
	EXPECT_NE(byName.Find(ibT("by its number")), wxNOT_FOUND) << byName;
}

// `while (r.ReadAttribute())` walks them all - the first call gives the first - and the next Read goes on after
// the element, not to its attributes' neighbours.
TEST(XmlReader, ReadAttributeWalksTheAttributesAndReadGoesOnAfterTheElement) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r a=\"1\" p:b=\"2\" xmlns:p=\"urn:p\"><c/></r>"));
	ASSERT_TRUE(reader->Read());
	std::vector<ibNode> seen;
	while (CallXml(reader, ibT("ReadAttribute")).GetBoolean()) {
		seen.push_back({ reader->NodeType(), PropXml(reader, ibT("Name")).GetString(), PropXml(reader, ibT("Value")).GetString() });
		EXPECT_TRUE(PropXml(reader, ibT("HasName")).GetBoolean());
		EXPECT_TRUE(PropXml(reader, ibT("HasValue")).GetBoolean());
		EXPECT_EQ(PropXml(reader, ibT("NodeType")).GetInteger(), 11);   // Attribute, by its number
	}
	ExpectNodes(seen, {
		{ ibXmlNodeType_Attribute, ibT("xmlns:p"), ibT("urn:p") },
		{ ibXmlNodeType_Attribute, ibT("a"), ibT("1") },
		{ ibXmlNodeType_Attribute, ibT("p:b"), ibT("2") },
	});
	// Past the last it answers False and stays where it was.
	EXPECT_FALSE(reader->ReadAttribute());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_Attribute);
	EXPECT_EQ(PropXml(reader, ibT("LocalName")).GetString(), ibString(ibT("b")));
	EXPECT_EQ(PropXml(reader, ibT("Prefix")).GetString(), ibString(ibT("p")));
	EXPECT_EQ(PropXml(reader, ibT("NamespaceURI")).GetString(), ibString(ibT("urn:p")));
	EXPECT_EQ(reader->AttributeCount(), 3);   // still the element's

	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("c")));
	EXPECT_EQ(reader->AttributeCount(), 0);
}

// FirstAttribute starts over; MoveToElement steps back to the element. On an attribute the
// element's IsEmptyElement, Depth and line still answer.
TEST(XmlReader, TheCursorGoesBackToTheElementWhoseExtensionsStillAnswer) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<a>\n<b x=\"1\" y=\"2\"/></a>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(CallXml(reader, ibT("FirstAttribute")).GetBoolean());
	EXPECT_EQ(reader->Name(), ibString(ibT("x")));
	ASSERT_TRUE(CallXml(reader, ibT("NextAttribute")).GetBoolean());
	EXPECT_EQ(reader->Name(), ibString(ibT("y")));
	EXPECT_TRUE(PropXml(reader, ibT("IsEmptyElement")).GetBoolean());
	EXPECT_EQ(PropXml(reader, ibT("Depth")).GetInteger(), 1);
	EXPECT_EQ(PropXml(reader, ibT("LineNumber")).GetInteger(), 2);
	ASSERT_TRUE(reader->FirstAttribute());
	EXPECT_EQ(reader->Name(), ibString(ibT("x")));

	EXPECT_TRUE(CallXml(reader, ibT("MoveToElement")).GetBoolean());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(PropXml(reader, ibT("Name")).GetString(), ibString(ibT("b")));
	EXPECT_EQ(PropXml(reader, ibT("Value")).GetString(), ibString());
	EXPECT_FALSE(reader->MoveToElement());   // on the element already
	ASSERT_TRUE(reader->NextAttribute());      // from the element: the first
	EXPECT_EQ(reader->Name(), ibString(ibT("x")));
}

// Skip stops ON the end of what it skipped, and the next Read goes on after it.
TEST(XmlReader, SkipStopsOnTheEndElementOfWhatItSkipped) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r><x><y/>t</x><z/></r>"));
	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->Read());   // x
	reader->Skip();
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("x")));
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("z")));

	// <z/> is a pair: Skip moves onto its end...
	CallXml(reader, ibT("Skip"));
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("z")));
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("r")));

	// ...on a node that is not an element it is Read...
	reader->SetText(ibT("<r>t<q/></r>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->Read());
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_Text);
	reader->Skip();
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("q")));

	// ...and on an attribute it skips the element the attribute belongs to.
	reader->SetText(ibT("<r><x a=\"1\"><y/></x><z/></r>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->ReadAttribute());
	reader->Skip();
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("x")));
}

// The script's numbers (the table at the top of the file): None 1, StartElement 2, Text 4.
TEST(XmlReader, MoveToContentPassesCommentsAndSaysWhereItStopped) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<!--c-->  <a/>"));
	ibValue stopped = CallXml(reader, ibT("MoveToContent"));   // before the first Read
	EXPECT_EQ(stopped.GetClassName(), ibString(ibT("XMLNodeType")));
	EXPECT_EQ(stopped.GetInteger(), 2);
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("a")));

	reader->SetText(ibT("<!--c-->  <a/>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_Comment);
	EXPECT_EQ(CallXml(reader, ibT("MoveToContent")).GetInteger(), 2);
	EXPECT_EQ(reader->Name(), ibString(ibT("a")));

	// Whitespace kept as nodes and an instruction are passed too; on content it does not move.
	reader->SetIgnoreWhitespace(false);
	reader->SetText(ibT("<r><!--c--> <?p v?>t</r>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(CallXml(reader, ibT("MoveToContent")).GetInteger(), 4);
	EXPECT_EQ(reader->Value(), ibString(ibT("t")));
	EXPECT_EQ(reader->MoveToContent(), ibXmlNodeType_Text);
	EXPECT_EQ(reader->Value(), ibString(ibT("t")));

	// On an attribute: back to its element. At the end: None.
	reader->SetText(ibT("<r a=\"1\"/>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->ReadAttribute());
	EXPECT_EQ(reader->MoveToContent(), ibXmlNodeType_StartElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("r")));
	ASSERT_TRUE(reader->Read());
	EXPECT_FALSE(reader->Read());
	EXPECT_EQ(CallXml(reader, ibT("MoveToContent")).GetInteger(), 1);
}

// Through the script's properties: IsEmptyElement on both nodes of <b/> and on neither of
// <b></b>; Depth from the root at 0; LineNumber the node's own line.
TEST(XmlReader, IsEmptyElementDepthAndLineAnswerThroughTheScriptsProperties) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<a>\n<b/>\n<b></b></a>"));
	struct Seen { ibXmlNodeType type; bool empty; int depth; int line; };
	const std::vector<Seen> want = {
		{ ibXmlNodeType_StartElement, false, 0, 1 },
		{ ibXmlNodeType_StartElement, true,  1, 2 },
		{ ibXmlNodeType_EndElement,   true,  1, 2 },
		{ ibXmlNodeType_StartElement, false, 1, 3 },
		{ ibXmlNodeType_EndElement,   false, 1, 3 },
		{ ibXmlNodeType_EndElement,   false, 0, 3 },
	};
	for (size_t i = 0; i < want.size(); ++i) {
		ASSERT_TRUE(reader->Read()) << i;
		EXPECT_EQ(reader->NodeType(), want[i].type) << i;
		EXPECT_EQ(reader->IsEmptyElement(), want[i].empty) << i;
		EXPECT_EQ(PropXml(reader, ibT("IsEmptyElement")).GetBoolean(), want[i].empty) << i;
		EXPECT_EQ(reader->Depth(), want[i].depth) << i;
		EXPECT_EQ(PropXml(reader, ibT("Depth")).GetInteger(), want[i].depth) << i;
		EXPECT_EQ(PropXml(reader, ibT("LineNumber")).GetInteger(), want[i].line) << i;
	}
}

// Text, an end, the time before the first Read: there are no attributes, and the cursor has nowhere to go.
TEST(XmlReader, OnANodeThatIsNotAStartElementThereAreNoAttributes) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r a=\"1\">t</r>"));
	EXPECT_EQ(reader->AttributeCount(), 0);
	EXPECT_FALSE(reader->FirstAttribute());

	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->Read());   // t
	for (int step = 0; step < 2; ++step) {
		EXPECT_EQ(CallXml(reader, ibT("AttributeCount")).GetInteger(), 0) << step;
		EXPECT_TRUE(IsUndefined(CallXml(reader, ibT("AttributeValue"), { ibValue(0) }))) << step;
		EXPECT_TRUE(IsUndefined(CallXml(reader, ibT("AttributeValue"), { ibValue(ibString(ibT("a"))) }))) << step;
		const ibValue name = CallXml(reader, ibT("AttributeName"), { ibValue(0) });
		EXPECT_EQ(name.GetType(), ibValueTypes::TYPE_STRING) << step;
		EXPECT_EQ(name.GetString(), ibString()) << step;
		EXPECT_FALSE(CallXml(reader, ibT("FirstAttribute")).GetBoolean()) << step;
		EXPECT_FALSE(CallXml(reader, ibT("ReadAttribute")).GetBoolean()) << step;
		EXPECT_FALSE(CallXml(reader, ibT("MoveToElement")).GetBoolean()) << step;
		if (step == 0) {
			ASSERT_TRUE(reader->Read());   // then </r>
		}
	}
}

// Close, and a new text, leave the attribute the cursor stood on: the node it belonged to is gone.
TEST(XmlReader, CloseAndANewTextLeaveTheAttributeCursorBehind) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r a=\"1\" b=\"2\"/>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->ReadAttribute());
	ASSERT_TRUE(reader->ReadAttribute());   // on b, the second
	reader->Close();
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_None);   // first: a cursor left behind would read past the end
	EXPECT_EQ(reader->Name(), ibString());
	EXPECT_FALSE(reader->ReadAttribute());
	EXPECT_FALSE(reader->MoveToElement());

	reader->SetText(ibT("<r a=\"1\" b=\"2\"/>"));
	ASSERT_TRUE(reader->Read());
	ASSERT_TRUE(reader->ReadAttribute());
	ASSERT_TRUE(reader->ReadAttribute());
	reader->SetText(ibT("<q/>"));
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_None);
	EXPECT_EQ(reader->Name(), ibString());
	EXPECT_FALSE(reader->ReadAttribute());
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->Name(), ibString(ibT("q")));
}

// The end Skip stops on is its element's own, by depth - not the first end of the same name inside it.
TEST(XmlReader, SkipStopsOnItsOwnEndPastNestedElementsOfTheSameName) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r><x><x/><x>t</x></x><z/></r>"));
	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->Read());   // the outer x
	reader->Skip();
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("x")));
	EXPECT_EQ(reader->Depth(), 1);
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("z")));
}

// 5000 children cross the parser's pause at 4096 waiting nodes: Skip reads through it, and still does not
// gather the element into memory.
TEST(XmlReader, SkipReadsThroughThePauseWithoutGatheringTheElement) {
	ibString text = ibT("<r><big>");
	for (int i = 0; i < 5000; ++i)
		text += ibT("<c/>");
	text += ibT("</big><z/></r>");

	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(text);
	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->Read());   // big
	reader->Skip();
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("big")));
	EXPECT_LE(reader->QueuedNodes(), 4096u + 2u);
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(reader->Name(), ibString(ibT("z")));
}

// The end of `<a x="1"/>` is the same tag, and has the same attributes - to
// count, to ask for, to walk. The end of `<b z="3"></b>` is a tag of its own, and has none.
TEST(XmlReader, TheEndOfAnEmptyElementHasItsAttributes) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r><a x=\"1\" y=\"2\"/><b z=\"3\"></b></r>"));
	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->Read());   // a
	ASSERT_TRUE(reader->Read());   // the end of a
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	EXPECT_EQ(reader->AttributeCount(), 2);
	EXPECT_EQ(CallXml(reader, ibT("AttributeValue"), { ibValue(ibString(ibT("x"))) }).GetString(), ibString(ibT("1")));
	EXPECT_EQ(CallXml(reader, ibT("AttributeName"), { ibValue(1) }).GetString(), ibString(ibT("y")));
	std::vector<ibNode> seen;
	while (reader->ReadAttribute())
		seen.push_back({ reader->NodeType(), reader->Name(), reader->Value() });
	ExpectNodes(seen, {
		{ ibXmlNodeType_Attribute, ibT("x"), ibT("1") },
		{ ibXmlNodeType_Attribute, ibT("y"), ibT("2") },
	});
	EXPECT_TRUE(reader->MoveToElement());
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);

	ASSERT_TRUE(reader->Read());   // b
	EXPECT_EQ(reader->AttributeCount(), 1);
	ASSERT_TRUE(reader->Read());   // </b>
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	EXPECT_EQ(reader->AttributeCount(), 0);
	EXPECT_TRUE(IsUndefined(reader->AttributeValue(ibValue(ibString(ibT("z"))))));
	EXPECT_FALSE(reader->ReadAttribute());
}

// =============================================================================
// XMLNamespaceContext - the scope at a node, as a snapshot.
// =============================================================================

namespace {

const ibChar* const kXmlNamespace = ibT("http://www.w3.org/XML/1998/namespace");

ibValueXmlNamespaceContext& AsContext(const ibValue& value) {
	ibValueXmlNamespaceContext* context = dynamic_cast<ibValueXmlNamespaceContext*>(value.GetRef());
	if (context == nullptr)
		throw std::runtime_error("not an XMLNamespaceContext: " + value.GetClassName().ToStdString());
	return *context;
}

ibValue Text(const ibChar* text) {
	return ibValue(ibString(text));
}

// An Array's items, as strings.
std::vector<ibString> Items(const ibValue& array) {
	ibValueArray* items = dynamic_cast<ibValueArray*>(array.GetRef());
	if (items == nullptr)
		throw std::runtime_error("not an Array: " + array.GetClassName().ToStdString());
	std::vector<ibString> seen;
	for (unsigned int i = 0; i < items->Count(); ++i) {
		ibValue item;
		items->GetAt(ibValue(static_cast<int>(i)), item);
		seen.push_back(item.GetString());
	}
	return seen;
}

// What the context answers for the prefix, through the script's door: the URI, or "(undefined)".
ibString Lookup(ibValue& context, const ibChar* prefix) {
	const ibValue uri = CallXml(context, ibT("LookupNamespaceURI"), { Text(prefix) });
	return IsUndefined(uri) ? ibString(ibT("(undefined)")) : uri.GetString();
}

} // namespace

// On p:x the default namespace and p are both in scope - and `xml`,
// which is bound in every document, declared or not.
TEST(XmlNamespaceContext, OnAPrefixedChildEveryDeclarationAboveIsInScope) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r xmlns=\"urn:a\" xmlns:p=\"urn:p\"><p:x/></r>"));
	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->Read());   // p:x
	ASSERT_EQ(reader->Name(), ibString(ibT("p:x")));

	ibValue context = PropXml(reader, ibT("NamespaceContext"));
	EXPECT_EQ(context.GetClassName(), ibString(ibT("XMLNamespaceContext")));
	EXPECT_EQ(PropXml(context, ibT("DefaultNamespace")).GetString(), ibString(ibT("urn:a")));
	EXPECT_EQ(PropXml(context, ibT("Depth")).GetInteger(), 2);
	EXPECT_EQ(Lookup(context, ibT("p")), ibString(ibT("urn:p")));
	EXPECT_EQ(Lookup(context, ibT("")), ibString(ibT("urn:a")));
	EXPECT_EQ(Lookup(context, ibT("zz")), ibString(ibT("(undefined)")));
	EXPECT_EQ(Lookup(context, ibT("xml")), ibString(kXmlNamespace));
	EXPECT_EQ(CallXml(context, ibT("LookupPrefix"), { Text(ibT("urn:p")) }).GetString(), ibString(ibT("p")));
	EXPECT_EQ(CallXml(context, ibT("LookupPrefix"), { Text(ibT("urn:a")) }).GetString(), ibString());
	EXPECT_EQ(CallXml(context, ibT("LookupPrefix"), { Text(kXmlNamespace) }).GetString(), ibString(ibT("xml")));
	EXPECT_TRUE(IsUndefined(CallXml(context, ibT("LookupPrefix"), { Text(ibT("urn:zz")) })));

	EXPECT_EQ(Items(CallXml(context, ibT("Prefixes"), { Text(ibT("urn:p")) })), std::vector<ibString>({ ibT("p") }));
	EXPECT_EQ(Items(CallXml(context, ibT("Prefixes"), { Text(ibT("urn:zz")) })), std::vector<ibString>());
	std::vector<ibString> uris = Items(CallXml(context, ibT("NamespaceURIs")));
	std::sort(uris.begin(), uris.end());
	EXPECT_EQ(uris, std::vector<ibString>({ kXmlNamespace, ibT("urn:a"), ibT("urn:p") }));

	// A Container, prefix to URI - the default namespace under "".
	const ibValue mappings = CallXml(context, ibT("NamespaceMappings"));
	ibValueContainer* map = dynamic_cast<ibValueContainer*>(mappings.GetRef());
	ASSERT_NE(map, nullptr) << mappings.GetClassName();
	EXPECT_EQ(mappings.GetClassName(), ibString(ibT("Container")));
	EXPECT_EQ(map->Count(), 3u);
	const struct { const ibChar* prefix; const ibChar* uri; } want[] = {
		{ ibT(""), ibT("urn:a") }, { ibT("p"), ibT("urn:p") }, { ibT("xml"), kXmlNamespace },
	};
	for (const auto& pair : want) {
		ibValue uri;
		EXPECT_TRUE(map->Property(Text(pair.prefix), uri)) << ibString(pair.prefix);
		EXPECT_EQ(uri.GetString(), ibString(pair.uri)) << ibString(pair.prefix);
	}
}

// ⭐ A SNAPSHOT: taken on p:x, it answers the same after the reader went on - past the element, past the end of
// the document, after Close.
TEST(XmlNamespaceContext, AContextTakenEarlierAnswersAsItDidAfterTheReaderMovesOn) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r xmlns=\"urn:a\" xmlns:p=\"urn:p\"><p:x/><q xmlns:p=\"urn:other\"/></r>"));
	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->Read());   // p:x
	ibValue context = PropXml(reader, ibT("NamespaceContext"));

	const auto answersAsOnX = [&](const char* where) {
		EXPECT_EQ(PropXml(context, ibT("DefaultNamespace")).GetString(), ibString(ibT("urn:a"))) << ibString(where);
		EXPECT_EQ(PropXml(context, ibT("Depth")).GetInteger(), 2) << ibString(where);
		EXPECT_EQ(Lookup(context, ibT("p")), ibString(ibT("urn:p"))) << ibString(where);
		EXPECT_EQ(CallXml(context, ibT("LookupPrefix"), { Text(ibT("urn:p")) }).GetString(), ibString(ibT("p"))) << ibString(where);
	};
	ASSERT_TRUE(reader->Read());   // the end of p:x
	ASSERT_TRUE(reader->Read());   // q, where p is another URI
	ASSERT_EQ(reader->Name(), ibString(ibT("q")));
	answersAsOnX("on q");
	ASSERT_TRUE(reader->Read());   // the end of q
	ASSERT_TRUE(reader->Read());   // </r>
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_EndElement);
	answersAsOnX("on </r>");
	EXPECT_FALSE(reader->Read());
	answersAsOnX("past the end");
	reader->Close();
	answersAsOnX("after Close");
}

// A prefix declared again further in stands for the new URI inside - the outer one is not to be found by it - and
// for the old one again once the inner element is over. Its EndElement is still inside; the node after it is not.
TEST(XmlNamespaceContext, ADeclarationFurtherInShadowsTheOuterOneUntilItsElementEnds) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r xmlns:p=\"urn:1\"><c xmlns:p=\"urn:2\"><p:y/></c><p:z/></r>"));
	struct Seen { ibXmlNodeType type; const ibChar* name; const ibChar* p; int depth; bool outerFound; };
	const std::vector<Seen> want = {
		{ ibXmlNodeType_StartElement, ibT("r"),   ibT("urn:1"), 1, true  },
		{ ibXmlNodeType_StartElement, ibT("c"),   ibT("urn:2"), 2, false },
		{ ibXmlNodeType_StartElement, ibT("p:y"), ibT("urn:2"), 3, false },
		{ ibXmlNodeType_EndElement,   ibT("p:y"), ibT("urn:2"), 2, false },
		{ ibXmlNodeType_EndElement,   ibT("c"),   ibT("urn:2"), 1, false },
		{ ibXmlNodeType_StartElement, ibT("p:z"), ibT("urn:1"), 2, true  },
		{ ibXmlNodeType_EndElement,   ibT("p:z"), ibT("urn:1"), 1, true  },
		{ ibXmlNodeType_EndElement,   ibT("r"),   ibT("urn:1"), 0, true  },
	};
	std::vector<ibValue> taken;
	for (size_t i = 0; i < want.size(); ++i) {
		ASSERT_TRUE(reader->Read()) << i;
		ASSERT_EQ(reader->NodeType(), want[i].type) << i;
		ASSERT_EQ(reader->Name(), ibString(want[i].name)) << i;
		taken.push_back(PropXml(reader, ibT("NamespaceContext")));
	}
	// Asked only now, all of them: each answers for its own node.
	for (size_t i = 0; i < want.size(); ++i) {
		EXPECT_EQ(Lookup(taken[i], ibT("p")), ibString(want[i].p)) << i;
		EXPECT_EQ(PropXml(taken[i], ibT("Depth")).GetInteger(), want[i].depth) << i;
		const ibValue outer = CallXml(taken[i], ibT("LookupPrefix"), { Text(ibT("urn:1")) });
		EXPECT_EQ(!IsUndefined(outer), want[i].outerFound) << i;
		EXPECT_EQ(Items(CallXml(taken[i], ibT("Prefixes"), { Text(ibT("urn:1")) })).size(), want[i].outerFound ? 1u : 0u) << i;
	}
}

// `xmlns=""` takes the default namespace back: inside, there is none - not one named "".
TEST(XmlNamespaceContext, AnEmptyDefaultDeclarationLeavesNoDefaultNamespace) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r xmlns=\"urn:a\"><c xmlns=\"\"><d/></c><e/></r>"));
	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->Read());   // c
	ASSERT_TRUE(reader->Read());   // d
	ibValue inside = PropXml(reader, ibT("NamespaceContext"));
	EXPECT_EQ(PropXml(inside, ibT("DefaultNamespace")).GetString(), ibString());
	EXPECT_EQ(Lookup(inside, ibT("")), ibString(ibT("(undefined)")));
	EXPECT_EQ(Items(CallXml(inside, ibT("NamespaceURIs"))), std::vector<ibString>({ kXmlNamespace }));

	ASSERT_TRUE(reader->Read());   // the end of d
	ASSERT_TRUE(reader->Read());   // </c>
	ASSERT_TRUE(reader->Read());   // e
	ASSERT_EQ(reader->Name(), ibString(ibT("e")));
	ibValue after = PropXml(reader, ibT("NamespaceContext"));
	EXPECT_EQ(PropXml(after, ibT("DefaultNamespace")).GetString(), ibString(ibT("urn:a")));
}

// A text node begun before the next element's declarations is in the scope it began in; on an attribute the
// context is the element's; before the first Read only `xml` is bound (at depth 1, counted before the root); with
// nothing open there is no context.
TEST(XmlNamespaceContext, TextAttributesAndTheEdgesOfTheDocument) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	EXPECT_TRUE(IsUndefined(PropXml(reader, ibT("NamespaceContext"))));

	reader->SetText(ibT("<r a=\"1\" xmlns:p=\"urn:p\">t<c xmlns:q=\"urn:q\"/></r>"));
	ibValue before = PropXml(reader, ibT("NamespaceContext"));
	EXPECT_EQ(PropXml(before, ibT("Depth")).GetInteger(), 1);
	EXPECT_EQ(Lookup(before, ibT("xml")), ibString(kXmlNamespace));
	EXPECT_EQ(AsContext(before).Namespaces().size(), 1u);

	ASSERT_TRUE(reader->Read());   // r
	ASSERT_TRUE(reader->ReadAttribute());
	ASSERT_TRUE(reader->ReadAttribute());
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_Attribute);
	ibValue onAttribute = PropXml(reader, ibT("NamespaceContext"));
	EXPECT_EQ(Lookup(onAttribute, ibT("p")), ibString(ibT("urn:p")));
	EXPECT_EQ(PropXml(onAttribute, ibT("Depth")).GetInteger(), 2);

	ASSERT_TRUE(reader->Read());   // t
	ASSERT_EQ(reader->NodeType(), ibXmlNodeType_Text);
	ibValue onText = PropXml(reader, ibT("NamespaceContext"));
	EXPECT_EQ(PropXml(onText, ibT("Depth")).GetInteger(), 2);
	EXPECT_EQ(Lookup(onText, ibT("p")), ibString(ibT("urn:p")));
	EXPECT_EQ(Lookup(onText, ibT("q")), ibString(ibT("(undefined)")));

	ASSERT_TRUE(reader->Read());   // c
	ibValue onChild = PropXml(reader, ibT("NamespaceContext"));
	EXPECT_EQ(Lookup(onChild, ibT("q")), ibString(ibT("urn:q")));

	reader->Close();
	EXPECT_TRUE(IsUndefined(PropXml(reader, ibT("NamespaceContext"))));
}

// The context's Depth is the reader's Depth plus one, except on an EndElement - an empty element's included - where
// it is the reader's, and on an attribute, one more than on its element's start. The reader's own Depth stays as
// it is: an element's two nodes agree.
TEST(XmlNamespaceContext, DepthCountsAsTheReferenceSystemsContextDoes) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r a=\"1\">t<e/><c>u</c></r>"));
	struct Seen { ibXmlNodeType type; const ibChar* name; int depth; int readerDepth; };
	const std::vector<Seen> want = {
		{ ibXmlNodeType_StartElement, ibT("r"), 1, 0 },
		{ ibXmlNodeType_Text,         ibT(""),  2, 1 },
		{ ibXmlNodeType_StartElement, ibT("e"), 2, 1 },
		{ ibXmlNodeType_EndElement,   ibT("e"), 1, 1 },
		{ ibXmlNodeType_StartElement, ibT("c"), 2, 1 },
		{ ibXmlNodeType_Text,         ibT(""),  3, 2 },
		{ ibXmlNodeType_EndElement,   ibT("c"), 1, 1 },
		{ ibXmlNodeType_EndElement,   ibT("r"), 0, 0 },
	};
	for (size_t i = 0; i < want.size(); ++i) {
		ASSERT_TRUE(reader->Read()) << i;
		ASSERT_EQ(reader->NodeType(), want[i].type) << i;
		ASSERT_EQ(reader->Name(), ibString(want[i].name)) << i;
		ibValue context = PropXml(reader, ibT("NamespaceContext"));
		EXPECT_EQ(PropXml(context, ibT("Depth")).GetInteger(), want[i].depth) << i;
		EXPECT_EQ(reader->Depth(), want[i].readerDepth) << i;
		if (i == 0) {   // the root's attribute
			ASSERT_TRUE(reader->ReadAttribute());
			ASSERT_EQ(reader->NodeType(), ibXmlNodeType_Attribute);
			ibValue onAttribute = PropXml(reader, ibT("NamespaceContext"));
			EXPECT_EQ(PropXml(onAttribute, ibT("Depth")).GetInteger(), 2);
			EXPECT_EQ(reader->Depth(), 0);
		}
	}
}

// A child that declares namespaces does not cost its parent a new snapshot: the nodes after it share the very one
// the nodes before it had, and the child's own is its declarations on top of that same one.
TEST(XmlNamespaceContext, AfterADeclaringChildTheParentsSnapshotIsTheSameOne) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<r xmlns:a=\"urn:a\">t<x xmlns:xs=\"urn:xs\" xmlns:xsi=\"urn:xsi\">u</x>v<y xmlns:p=\"urn:p\"/>w</r>"));
	std::vector<std::shared_ptr<const ibXmlParser::ibScope>> atText;
	std::shared_ptr<const ibXmlParser::ibScope> atStart, atX, atEnd;
	while (reader->Read()) {
		const ibValueXmlReader::ibNode& node = reader->Current();
		if (node.m_type == ibXmlNodeType_Text && node.m_depth == 1)
			atText.push_back(node.m_namespaces);
		else if (node.m_name == ibT("r"))
			(node.m_type == ibXmlNodeType_StartElement ? atStart : atEnd) = node.m_namespaces;
		else if (node.m_name == ibT("x") && node.m_type == ibXmlNodeType_StartElement)
			atX = node.m_namespaces;
	}
	ASSERT_EQ(atText.size(), 3u);
	ASSERT_NE(atStart, nullptr);
	EXPECT_EQ(atText[0].get(), atStart.get());
	EXPECT_EQ(atText[1].get(), atStart.get());
	EXPECT_EQ(atText[2].get(), atStart.get());
	EXPECT_EQ(atEnd.get(), atStart.get());
	ASSERT_NE(atX, nullptr);
	ASSERT_NE(atX->m_outer, nullptr);
	EXPECT_EQ(atX->m_outer->m_outer.get(), atStart.get());   // xsi on xs on r's
	EXPECT_EQ(ibXmlParser::Resolve(atX.get())->size(), 4u);   // xml, a, xs, xsi
}

// As deep as the document is nested, the chain of declarations is let go of link by link: 100 000 nested elements
// each declaring, closed while the reader stands at the bottom. (Let go of recursively, it overflows the stack.)
TEST(XmlNamespaceContext, ADeeplyNestedDocumentsScopeIsReleasedWithoutRecursion) {
	const int depth = 100000;
	ibString text;
	for (int i = 0; i < depth; ++i)
		text += ibT("<a xmlns:p=\"urn:p\">");
	for (int i = 0; i < depth; ++i)
		text += ibT("</a>");

	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(text);
	for (int i = 0; i < depth; ++i)
		ASSERT_TRUE(reader->Read()) << i;
	EXPECT_EQ(reader->Depth(), depth - 1);
	reader->Close();   // the parser and every node holding the innermost link go: the whole chain at once
	EXPECT_EQ(reader->NodeType(), ibXmlNodeType_None);
}

// Every element of 5000 declares its own p - the scope an element's two nodes see survives the parser's pause
// between them, and never leaks into the next element.
TEST(XmlNamespaceContext, EachElementsScopeCrossesThePauseIntact) {
	ibString text = ibT("<r>");
	for (int i = 0; i < 5000; ++i)
		text += ibString::Format(ibT("<x xmlns:p=\"urn:%d\"/>"), i);
	text += ibT("</r>");

	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(text);
	ASSERT_TRUE(reader->Read());   // r
	for (int i = 0; i < 5000; ++i) {
		const ibString want = ibString::Format(ibT("urn:%d"), i);
		ASSERT_TRUE(reader->Read());
		ASSERT_EQ(reader->NodeType(), ibXmlNodeType_StartElement) << i;
		ASSERT_EQ(AsContext(reader->NamespaceContext()).LookupNamespaceURI(ibT("p")).GetString(), want) << i;
		ASSERT_TRUE(reader->Read());
		ASSERT_EQ(reader->NodeType(), ibXmlNodeType_EndElement) << i;
		ASSERT_EQ(AsContext(reader->NamespaceContext()).LookupNamespaceURI(ibT("p")).GetString(), want) << i;
	}
	ASSERT_TRUE(reader->Read());   // </r>
	EXPECT_TRUE(IsUndefined(AsContext(reader->NamespaceContext()).LookupNamespaceURI(ibT("p"))));
}

// Vended, not created: the compiler emits `New` only for a type registered as a value, and this one is not.
// Built empty through the registry (as a caller asking what a type offers does), it is the scope of nothing:
// `xml` alone.
TEST(XmlNamespaceContext, NewIsRefusedAndTheArgumentsAreText) {
	{
		ibCompileCode refused(ibT("test"), ibT("memory"), false);
		const ibString refusal = RefusalOf([&] { refused.Compile(ibT("x = New XMLNamespaceContext();")); });
		EXPECT_NE(refusal.Find(ibT("XMLNamespaceContext")), wxNOT_FOUND) << refusal;
		ibCompileCode allowed(ibT("test"), ibT("memory"), false);
		EXPECT_TRUE(allowed.Compile(ibT("x = New XMLReader();")));
	}
	EXPECT_FALSE(ibValue::IsRegisterCtor(ibT("XMLNamespaceContext"), ibCtorObjectType::ibCtorObjectType_object_value));
	EXPECT_TRUE(ibValue::IsRegisterCtor(ibT("XMLNamespaceContext"), ibCtorObjectType::ibCtorObjectType_object_system));
	EXPECT_TRUE(ibValue::IsRegisterCtor(ibT("XMLReader"), ibCtorObjectType::ibCtorObjectType_object_value));

	ibValue empty = ibValue::CreateObject(ibT("XMLNamespaceContext"));
	EXPECT_EQ(PropXml(empty, ibT("Depth")).GetInteger(), 0);
	EXPECT_EQ(Lookup(empty, ibT("xml")), ibString(kXmlNamespace));

	const ibString refusal = RefusalOf([&] {
		ibValue number(1);
		ibValue* params[] = { &number };
		ibValue result;
		empty.CallAsFunc(empty.FindMethod(ibT("LookupNamespaceURI")), result, params, 1);
	});
	EXPECT_EQ(refusal, ibString(ibT("XMLNamespaceContext: a prefix is a string, not a value of the type 'Number'")));
}

// =============================================================================
// XMLWriter
// =============================================================================

namespace {

ibString WriteToString(const std::function<void(ibXmlWriter&)>& body, bool indent = true) {
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->SetText(ibString());
	writer->Target().SetIndent(indent);
	body(writer->Target());
	return writer->Close();
}

std::string FileBytes(const ibString& path) {
	wxFFile f(path, wxT("rb"));
	std::string bytes(static_cast<size_t>(f.Length()), '\0');
	if (!bytes.empty())
		f.Read(&bytes[0], bytes.size());
	return bytes;
}

} // namespace

// The tag an element ends with is `/>` when nothing was written inside it; the rest is escaped where it has to be:
// `&`, `<`, `>` in text, also `"`, the tab and the line breaks in an attribute (a reader turns those into spaces).
TEST(XmlWriter, WhatIsWrittenIsEscapedWhereItMustBe) {
	const ibString text = WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString());
		w.WriteAttribute(ibT("v"), ibString(), ibT("x\"y\t1\n2\r<&>"));
		w.WriteStartElement(ibT("b"), ibString());
		w.WriteEndElement();
		w.WriteStartElement(ibT("c"), ibString());
		w.WriteText(ibT("1 < 2 & 3 > \"q\"\r"));
		w.WriteEndElement();
		w.WriteEndElement();
	}, false);
	EXPECT_EQ(text, ibString(ibT("<a v=\"x&quot;y&#9;1&#10;2&#13;&lt;&amp;&gt;\"><b/><c>1 &lt; 2 &amp; 3 &gt; \"q\"&#13;</c></a>")));

	// ...and reads back as it was given.
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(text);
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->AttributeValue(ibValue(ibString(ibT("v")))).GetString(), ibString(ibT("x\"y\t1\n2\r<&>")));
	ASSERT_TRUE(reader->Read());   // <b/>
	ASSERT_TRUE(reader->Read());   // </b>
	ASSERT_TRUE(reader->Read());   // <c>
	ASSERT_TRUE(reader->Read());
	EXPECT_EQ(reader->Value(), ibString(ibT("1 < 2 & 3 > \"q\"\r")));
}

// A line and a tab a level before every element - but not inside one that holds text: there the line would be
// part of the text. The end of an element that holds elements stands on its own line.
TEST(XmlWriter, IndentPutsAnElementOnItsLineButNotInsideText) {
	const ibString text = WriteToString([](ibXmlWriter& w) {
		w.WriteXmlDeclaration(ibT("UTF-8"));
		w.WriteStartElement(ibT("a"), ibString());
		w.WriteStartElement(ibT("b"), ibString());
		w.WriteText(ibT("t"));
		w.WriteEndElement();
		w.WriteStartElement(ibT("m"), ibString());
		w.WriteText(ibT("x"));
		w.WriteStartElement(ibT("i"), ibString());
		w.WriteEndElement();
		w.WriteText(ibT("y"));
		w.WriteEndElement();
		w.WriteComment(ibT(" c "));
		w.WriteEndElement();
	});
	EXPECT_EQ(text, ibString(ibT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<a>\n\t<b>t</b>\n\t<m>x<i/>y</m>\n\t<!-- c -->\n</a>")));
}

// `]]>` would end the section early: it is split across two, and reads back whole as two CDATA nodes.
TEST(XmlWriter, ACdataSectionHoldingItsEndIsSplitInTwo) {
	const ibString text = WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString());
		w.WriteCDATASection(ibT("x]]>y"));
		w.WriteEndElement();
	}, false);
	EXPECT_EQ(text, ibString(ibT("<a><![CDATA[x]]]]><![CDATA[>y]]></a>")));
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(text);
	ibString joined;
	while (reader->Read())
		if (reader->NodeType() == ibXmlNodeType_CDATASection)
			joined += reader->Value();
	EXPECT_EQ(joined, ibString(ibT("x]]>y")));
}

// A namespace URI not bound in scope is declared where it is used; one bound to a prefix makes the prefix; an
// attribute is never in the default namespace, so it gets a made-up prefix.
TEST(XmlWriter, ANamespaceIsDeclaredWhereItIsFirstUsed) {
	ibString lookedUp;
	ibString text = WriteToString([&](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibT("urn:d"));
		w.WriteNamespaceMapping(ibT("p"), ibT("urn:p"));
		w.WriteStartElement(ibT("b"), ibT("urn:p"));          // the URI is p's: the element is p:b
		w.WriteAttribute(ibT("q"), ibT("urn:q"), ibT("1"));   // nobody's: d2p1
		w.WriteAttribute(ibT("p:r"), ibT("urn:p"), ibT("2"));
		w.WriteAttribute(ibT("s"), ibT("urn:d"), ibT("3"));   // the default's URI: an attribute still needs a prefix
		EXPECT_TRUE(w.LookupPrefix(ibT("urn:q"), lookedUp));
		w.WriteEndElement();
		w.WriteStartElement(ibT("x:c"), ibT("urn:x"));
		w.WriteEndElement();
		w.WriteStartElement(ibT("e"), ibString());            // no URI: the default namespace in scope
		w.WriteEndElement();
		w.WriteEndElement();
	}, false);
	EXPECT_EQ(lookedUp, ibString(ibT("d2p1")));
	EXPECT_EQ(text, ibString(ibT("<a xmlns=\"urn:d\" xmlns:p=\"urn:p\"><p:b xmlns:d2p1=\"urn:q\" d2p1:q=\"1\" p:r=\"2\" xmlns:d2p2=\"urn:d\" d2p2:s=\"3\"/>"
		"<x:c xmlns:x=\"urn:x\"/><e/></a>")));

	// ...and the reader agrees on every name's namespace.
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(text);
	std::vector<ibString> uris;
	while (reader->Read())
		if (reader->NodeType() == ibXmlNodeType_StartElement)
			uris.push_back(reader->NamespaceURI());
	EXPECT_EQ(uris, (std::vector<ibString>{ ibT("urn:d"), ibT("urn:p"), ibT("urn:x"), ibT("urn:d") }));
}

// The writer refuses what would not be XML, and says what was wrong.
TEST(XmlWriter, WhatWouldNotBeXmlIsRefused) {
	const auto refusal = [](const std::function<void(ibXmlWriter&)>& body) {
		return RefusalOf([&] { WriteToString(body); });
	};
	EXPECT_NE(refusal([](ibXmlWriter& w) { w.WriteAttribute(ibT("a"), ibString(), ibT("1")); }).Find(ibT("right after its element's start")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString()); w.WriteText(ibT("t")); w.WriteAttribute(ibT("b"), ibString(), ibT("1"));
	}).Find(ibT("right after its element's start")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) { w.WriteEndElement(); }).Find(ibT("no element is open")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) { w.WriteStartElement(ibT("a"), ibString()); }).Find(ibT("'a' is still open")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString()); w.WriteEndElement(); w.WriteXmlDeclaration(ibT("UTF-8"));
	}).Find(ibT("first thing")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) { w.WriteComment(ibT("a--b")); }).Find(ibT("'--'")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) { w.WriteProcessingInstruction(ibT("XML"), ibString()); }).Find(ibT("declaration")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) { w.WriteStartElement(ibT("a b"), ibString()); }).Find(ibT("is not a name")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) { w.WriteStartElement(ibT("p:a"), ibString()); }).Find(ibT("not bound")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString()); w.WriteText(ibString(ibT("x")) + ibString(static_cast<ibChar>(0x01)));
	}).Find(ibT("U+0001 is not allowed")), wxNOT_FOUND);
}

// The format a tax return is filed in: windows-1251, the bytes computed OUTSIDE the engine (Python's
// str.encode('cp1251')) and compared byte for byte. A character the page has no byte for becomes a reference in
// text - and is refused in a name.
TEST(XmlWriter, AWindows1251FileIsTheBytesTheCodecMakes) {
	XmlTempFile f;
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->OpenFile(f.path, ibT("windows-1251"), true);
	ibXmlWriter& w = writer->Target();
	w.WriteXmlDeclaration(ibT("windows-1251"));
	w.WriteStartElement(ibT("DECLAR"), ibString());
	w.WriteStartElement(ibT("DECLARHEAD"), ibString());
	w.WriteStartElement(ibT("TIN"), ibString());
	w.WriteText(ibT("1234567890"));
	w.WriteEndElement();
	w.WriteEndElement();
	w.WriteStartElement(ibT("DECLARBODY"), ibString());
	w.WriteStartElement(ibT("HNAME"), ibString());
	const ibString name = FromCodePoints({ 0x422, 0x41E, 0x412, ' ', '"', 0x420, 0x435, 0x441, 0x442, 0x43E, 0x440, 0x430,
		0x43D, ' ', 0x407, 0x436, 0x430, 0x43A, '"', ' ', '&', ' ', 0x100 });
	w.WriteText(name);
	w.WriteEndElement();
	w.WriteEndElement();
	w.WriteEndElement();
	EXPECT_EQ(writer->Close(), ibString());

	const std::string want = "<?xml version=\x22" "1.0\x22 encoding=\x22windows-1251\x22?>\x0A<DECLAR>\x0A\x09<DECLARHEAD>\x0A\x09\x09<TIN>1234567890</TIN>\x0A\x09</DECLARHEAD>\x0A\x09<DECLARBODY>\x0A\x09\x09<HNAME>\xD2\xCE\xC2 \x22\xD0\xE5\xF1\xF2\xEE\xF0\xE0\xED \xAF\xE6\xE0\xEA\x22 &amp; &#256;</HNAME>\x0A\x09</DECLARBODY>\x0A</DECLAR>";
	EXPECT_EQ(FileBytes(f.path), want);

	// Read back: the same text, the reference included.
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->OpenFile(f.path);
	ibString read;
	while (reader->Read())
		if (reader->NodeType() == ibXmlNodeType_Text && reader->Current().m_depth == 3 && reader->Value() != ibT("1234567890"))
			read = reader->Value();
	EXPECT_EQ(read, name);

	// In a name there is no reference to fall back on.
	ibValuePtr<ibValueXmlWriter> named(new ibValueXmlWriter());
	named->OpenFile(f.path, ibT("cp1251"), true);
	EXPECT_NE(RefusalOf([&] { named->Target().WriteStartElement(FromCodePoints({ 'a', 0x100 }), ibString()); })
		.Find(ibT("U+0100 in an element has no byte in windows-1251")), wxNOT_FOUND);
}

// The byte-order mark: UTF-8 has one unless told not to, UTF-16 always (little-endian), a single-byte page never.
TEST(XmlWriter, TheByteOrderMarkIsTheEncodingsAndTheScripts) {
	XmlTempFile f;
	const auto write = [&](const ibChar* encoding, bool bom) {
		ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
		writer->OpenFile(f.path, encoding, bom);
		writer->Target().WriteStartElement(ibT("a"), ibString());
		writer->Target().WriteEndElement();
		writer->Close();
		return FileBytes(f.path);
	};
	EXPECT_EQ(write(ibT("UTF-8"), true), std::string("\xEF\xBB\xBF<a/>"));
	EXPECT_EQ(write(ibT("utf-8"), false), std::string("<a/>"));
	EXPECT_EQ(write(ibT("UTF-16"), false), std::string("\xFF\xFE<\0a\0/\0>\0", 10));
	EXPECT_EQ(write(ibT("cp866"), true), std::string("<a/>"));
	EXPECT_NE(RefusalOf([&] { write(ibT("koi8-u"), true); }).Find(ibT("not supported")), wxNOT_FOUND);
}

// A document bigger than one piece passes to the file a piece at a time - and is whole.
TEST(XmlWriter, ALargeDocumentReachesTheFileWhole) {
	XmlTempFile f;
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->OpenFile(f.path, ibT("UTF-8"), false);
	writer->Target().SetIndent(false);
	writer->Target().WriteStartElement(ibT("r"), ibString());
	for (int i = 0; i < 20000; ++i) {
		ibValue* params[1];
		ibValue name(ibString(ibT("x")));
		params[0] = &name;
		ibValue result;
		writer->CallAsFunc(writer->FindMethod(ibT("WriteStartElement")), result, params, 1);
		writer->CallAsFunc(writer->FindMethod(ibT("WriteEndElement")), result, nullptr, 0);
	}
	writer->Target().WriteEndElement();
	writer->Close();

	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->OpenFile(f.path);
	size_t starts = 0;
	while (reader->Read())
		if (reader->NodeType() == ibXmlNodeType_StartElement)
			++starts;
	EXPECT_EQ(starts, 20001u);
	EXPECT_EQ(FileBytes(f.path).size(), 3u + 20000u * 4u + 4u);
}

// WriteCurrent writes the node the reader stands on, not its subtree - so a loop over a reader copies the document.
TEST(XmlWriter, WriteCurrentInALoopCopiesTheDocument) {
	const ibString source = ibT("<a xmlns:p=\"urn:p\" v=\"1\"><p:b/><!--c--><?pi d?>t<![CDATA[x]]><e>y</e></a>");
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(source);
	reader->SetIgnoreWhitespace(false);
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->SetText(ibString());
	writer->Target().SetIndent(false);
	while (reader->Read())
		writer->WriteCurrent(*reader);
	EXPECT_EQ(writer->Close(), source);
}

// A refused write changes nothing: the half value, the half tag, the element begun are taken back, and the writer
// goes on into a document that parses.
TEST(XmlWriter, ARefusedWriteLeavesNothingBehind) {
	const ibString text = WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString());
		EXPECT_NE(RefusalOf([&] { w.WriteAttribute(ibT("v"), ibString(), ibString(ibT("x")) + ibString(static_cast<ibChar>(0x01))); })
			.Find(ibT("U+0001")), wxNOT_FOUND);
		EXPECT_NE(RefusalOf([&] { w.WriteStartElement(ibT("p:b"), ibString()); }).Find(ibT("not bound")), wxNOT_FOUND);
		EXPECT_NE(RefusalOf([&] { w.WriteStartElement(ibT("xmlns:b"), ibT("urn:b")); }).Find(ibT("reserved")), wxNOT_FOUND);
		w.WriteAttribute(ibT("ok"), ibString(), ibT("1"));     // the tag is still open, and clean
		EXPECT_NE(RefusalOf([&] { w.WriteText(ibString(ibT("t&")) + ibString(static_cast<ibChar>(0x02))); }).Find(ibT("U+0002")), wxNOT_FOUND);
		w.WriteText(ibT("t"));
		w.WriteEndElement();
	}, false);
	EXPECT_EQ(text, ibString(ibT("<a ok=\"1\">t</a>")));
}

// `xmlns:p` and `xmlns` written as attributes are declarations, and the writer knows them: a name under the prefix
// afterwards is bound, the URI finds its prefix, and nothing is declared twice.
TEST(XmlWriter, ADeclarationWrittenAsAnAttributeIsKnownAsOne) {
	const ibString xsi = ibT("http://www.w3.org/2001/XMLSchema-instance");
	const ibString text = WriteToString([&](ibXmlWriter& w) {
		w.WriteStartElement(ibT("DECLAR"), ibString());
		w.WriteAttribute(ibT("xmlns:xsi"), ibString(), xsi);
		w.WriteAttribute(ibT("xsi:noNamespaceSchemaLocation"), ibString(), ibT("J0100117.xsd"));
		w.WriteAttribute(ibT("type"), xsi, ibT("t"));               // the URI is xsi's: the prefix is found
		w.WriteEndElement();
	}, false);
	EXPECT_EQ(text, ibString(ibT("<DECLAR xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		"xsi:noNamespaceSchemaLocation=\"J0100117.xsd\" xsi:type=\"t\"/>")));
}

// The same attribute twice - by the same name, or by two prefixes of one namespace - would not parse.
TEST(XmlWriter, AnAttributeIsWrittenOnce) {
	const auto refusal = [](const std::function<void(ibXmlWriter&)>& body) {
		return RefusalOf([&] { WriteToString(body); });
	};
	EXPECT_NE(refusal([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString());
		w.WriteAttribute(ibT("v"), ibString(), ibT("1"));
		w.WriteAttribute(ibT("v"), ibString(), ibT("2"));
	}).Find(ibT("written on this element already")), wxNOT_FOUND);
	EXPECT_NE(refusal([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString());
		w.WriteNamespaceMapping(ibT("p"), ibT("urn:x"));
		w.WriteNamespaceMapping(ibT("q"), ibT("urn:x"));
		w.WriteAttribute(ibT("p:v"), ibString(), ibT("1"));
		w.WriteAttribute(ibT("q:v"), ibString(), ibT("2"));
	}).Find(ibT("written on this element already")), wxNOT_FOUND);
	// A name of punctuation would break the tag; a prefix is one name, without a colon.
	for (const ibChar* name : { ibT("a#b"), ibT("a$"), ibT("a{b}"), ibT("a*"), ibT("a\\b"), ibT("a@b"), ibT("p:1x"), ibT("p:-x") })
		EXPECT_NE(refusal([&](ibXmlWriter& w) { w.WriteStartElement(name, ibString()); }).Find(ibT("is not a name")), wxNOT_FOUND)
			<< ibString(name);
	EXPECT_NE(refusal([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("a"), ibString());
		w.WriteNamespaceMapping(ibT("a:b"), ibT("urn:x"));
	}).Find(ibT("is not a name")), wxNOT_FOUND);
}

// Walking an element's attributes with the cursor and writing each: the declarations among them are declarations.
TEST(XmlWriter, WriteCurrentOnTheAttributesKeepsTheDeclarations) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<a xmlns=\"urn:d\" xmlns:p=\"urn:p\" p:v=\"1\"/>"));
	ASSERT_TRUE(reader->Read());
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->SetText(ibString());
	writer->Target().SetIndent(false);
	writer->Target().WriteStartElement(reader->Name(), ibString());
	while (reader->ReadAttribute())
		writer->WriteCurrent(*reader);
	writer->Target().WriteEndElement();
	EXPECT_EQ(writer->Close(), ibString(ibT("<a xmlns=\"urn:d\" xmlns:p=\"urn:p\" p:v=\"1\"/>")));
}

// What was written reaches its file even when the script never closes it - opened again, or destroyed.
TEST(XmlWriter, AWriterNeverClosedStillFillsItsFile) {
	XmlTempFile first, second;
	{
		ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
		writer->OpenFile(first.path, ibT("UTF-8"), false);
		writer->Target().WriteStartElement(ibT("a"), ibString());
		writer->Target().WriteEndElement();
		writer->OpenFile(second.path, ibT("UTF-8"), false);
		writer->Target().WriteStartElement(ibT("b"), ibString());
		writer->Target().WriteEndElement();
	}
	EXPECT_EQ(FileBytes(first.path), std::string("<a/>"));
	EXPECT_EQ(FileBytes(second.path), std::string("<b/>"));
}

// A declaration made after a name on the tag already stands under its prefix would move that name into another
// namespace without a word - it is refused instead.
TEST(XmlWriter, APrefixInUseOnTheTagIsNotDeclaredAgain) {
	EXPECT_NE(RefusalOf([] { WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("r"), ibString());
		w.WriteNamespaceMapping(ibT("p"), ibT("urn:x"));
		w.WriteStartElement(ibT("p:e"), ibT("urn:x"));
		w.WriteAttribute(ibT("p:a"), ibT("urn:y"), ibT("v"));
	}); }).Find(ibT("already stands for 'urn:x'")), wxNOT_FOUND);
	EXPECT_NE(RefusalOf([] { WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("r"), ibT("urn:x"));
		w.WriteStartElement(ibT("e"), ibT("urn:x"));
		w.WriteNamespaceMapping(ibString(), ibT("urn:z"));
	}); }).Find(ibT("already stands for")), wxNOT_FOUND);
	// The xml and xmlns namespaces are bound by definition: no other prefix takes them.
	EXPECT_NE(RefusalOf([] { WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("r"), ibString());
		w.WriteNamespaceMapping(ibT("p"), ibT("http://www.w3.org/XML/1998/namespace"));
	}); }).Find(ibT("is reserved")), wxNOT_FOUND);
	EXPECT_NE(RefusalOf([] { WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("r"), ibString());
		w.WriteNamespaceMapping(ibT("p"), ibT("http://www.w3.org/2000/xmlns/"));
	}); }).Find(ibT("is reserved")), wxNOT_FOUND);
}

// A declaration a refused attribute had waiting is forgotten with it: the next attribute does not write it.
TEST(XmlWriter, ARefusedAttributeTakesItsDeclarationWithIt) {
	const ibString text = WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("r"), ibString());
		w.WriteAttribute(ibT("v"), ibT("urn:z"), ibT("1"));           // d1p1:v
		EXPECT_NE(RefusalOf([&] { w.WriteAttribute(ibT("s:v"), ibT("urn:z"), ibT("2")); })
			.Find(ibT("written on this element already")), wxNOT_FOUND);
		w.WriteAttribute(ibT("w"), ibString(), ibT("3"));
		w.WriteEndElement();
	}, false);
	EXPECT_EQ(text, ibString(ibT("<r xmlns:d1p1=\"urn:z\" d1p1:v=\"1\" w=\"3\"/>")));
}

// Raw text is not escaped and not checked for markup - only for characters XML has and the file can hold, and
// that before anything of it is written.
TEST(XmlWriter, RawTextGoesAsItIsButOnlyInCharactersTheFileHas) {
	EXPECT_EQ(WriteToString([](ibXmlWriter& w) {
		w.WriteStartElement(ibT("r"), ibString());
		w.WriteRaw(ibT("<x a='1'>&amp;</x>"));
		w.WriteEndElement();
	}, false), ibString(ibT("<r><x a='1'>&amp;</x></r>")));

	XmlTempFile f;
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->OpenFile(f.path, ibT("windows-1251"), true);
	writer->Target().SetIndent(false);
	writer->Target().WriteStartElement(ibT("r"), ibString());
	EXPECT_NE(RefusalOf([&] { writer->Target().WriteRaw(FromCodePoints({ 'a', 0x100 })); })
		.Find(ibT("U+0100 has no byte in windows-1251")), wxNOT_FOUND);
	EXPECT_NE(RefusalOf([&] { writer->Target().WriteRaw(FromCodePoints({ 'a', 0xD800 })); })
		.Find(ibT("U+D800 is not allowed")), wxNOT_FOUND);
	writer->Target().WriteEndElement();
	writer->Close();
	EXPECT_EQ(FileBytes(f.path), std::string("<r/>"));
}

// Inside an entity's replacement text no event has bytes of its own: an element there that is NOT empty is still
// not taken for one, and a copy keeps its end tag.
TEST(XmlWriter, ACopyKeepsTheEndsOfElementsFromAnEntity) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibT("<!DOCTYPE r [<!ENTITY e \"<a>x</a>\">]><r>&e;<c/></r>"));
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->SetText(ibString());
	writer->Target().SetIndent(false);
	while (reader->Read()) {
		if (reader->NodeType() == ibXmlNodeType_EndElement && reader->Name() == ibT("a"))
			EXPECT_FALSE(reader->IsEmptyElement());
		writer->WriteCurrent(*reader);
	}
	EXPECT_EQ(writer->Close(), ibString(ibT("<r><a>x</a><c/></r>")));
}

// An element copied with its attributes is one write: an attribute the file cannot hold, and the element is not
// begun either.
TEST(XmlWriter, AnElementCopiedWithItsAttributesIsOneWrite) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibString(ibT("<a x=\"1\" ")) + FromCodePoints({ 0x101 }) + ibString(ibT("=\"2\"/>")));
	ASSERT_TRUE(reader->Read());
	XmlTempFile f;
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->OpenFile(f.path, ibT("windows-1251"), true);
	EXPECT_NE(RefusalOf([&] { writer->WriteCurrent(*reader); }).Find(ibT("no byte in windows-1251")), wxNOT_FOUND);
	EXPECT_EQ(writer->Close(), ibString());   // nothing is open: the start was not written
	EXPECT_EQ(FileBytes(f.path), std::string());
}

// The same file opened again starts it over - it is not written by two handles at once.
TEST(XmlWriter, TheSameFileOpenedAgainStartsOver) {
	XmlTempFile f;
	ibValuePtr<ibValueXmlWriter> writer(new ibValueXmlWriter());
	writer->OpenFile(f.path, ibT("UTF-8"), false);
	writer->Target().SetIndent(false);
	writer->Target().WriteStartElement(ibT("a-long-first-document"), ibString());
	writer->OpenFile(f.path, ibT("UTF-8"), false);
	writer->Target().WriteStartElement(ibT("b"), ibString());
	writer->Target().WriteEndElement();
	writer->Close();
	EXPECT_EQ(FileBytes(f.path), std::string("<b/>"));
}

// A string cannot hold what is not a character: a lone surrogate is refused where it stands, as malformed text is.
TEST(XmlReader, ALoneSurrogateInAStringIsRefusedWhereItStands) {
	ibValuePtr<ibValueXmlReader> reader(new ibValueXmlReader());
	reader->SetText(ibString(ibT("<a>x")) + FromCodePoints({ 0xD800 }) + ibString(ibT("</a>")));
	const ibString refusal = RefusalOf([&] { while (reader->Read()) {} });
	EXPECT_NE(refusal.Find(ibT("not well-formed (invalid token) at line 1, column 5")), wxNOT_FOUND) << refusal.ToStdString();
}
