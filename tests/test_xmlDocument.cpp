// =============================================================================
// A whole document as a tree - what the workbook reader walks. Every expected name and text is written here as a
// literal, and the tree is walked by hand: the reference is not the reader.
// =============================================================================

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/exception.h"
#include "core/xml/xmlDocument.h"

namespace {

int CountChildren(const ibXmlNode* node)
{
	int count = 0;
	for (const ibXmlNode* child = node->GetChildren(); child != nullptr; child = child->GetNext())
		++count;
	return count;
}

} // namespace

TEST(XmlDocument, TheRootHoldsItsChildrenInOrderWithTheirParent)
{
	ibXmlDocument xml;
	xml.Load(ibT("<?xml version=\"1.0\"?><!-- before --><sst><si><t>one</t></si><si><t>two</t></si></sst><!-- after -->"));
	const ibXmlNode* root = xml.GetRoot();
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(root->GetName(), ibString(ibT("sst")));
	EXPECT_EQ(root->GetParent(), nullptr);
	EXPECT_EQ(root->GetNext(), nullptr);   // the comment after the root is not kept
	ASSERT_EQ(CountChildren(root), 2);

	const ibXmlNode* first = root->GetChildren();
	const ibXmlNode* second = first->GetNext();
	EXPECT_EQ(first->GetParent(), root);
	EXPECT_EQ(second->GetParent(), root);
	EXPECT_EQ(first->GetChildren()->GetNodeContent(), ibString(ibT("one")));
	EXPECT_EQ(second->GetChildren()->GetNodeContent(), ibString(ibT("two")));
	EXPECT_EQ(first->GetChildren()->GetParent(), first);
}

TEST(XmlDocument, AnAttributeIsFoundByItsNameAsWrittenOrAnswersTheAbsentValue)
{
	ibXmlDocument xml;
	xml.Load(ibT("<workbook xmlns:r=\"urn:r\"><sheet name=\"A &amp; B\" r:id=\"rId7\"/></workbook>"));
	const ibXmlNode* sheet = xml.GetRoot()->GetChildren();
	ASSERT_NE(sheet, nullptr);
	EXPECT_EQ(sheet->GetType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(sheet->GetName(), ibString(ibT("sheet")));
	EXPECT_EQ(sheet->GetChildren(), nullptr);
	EXPECT_EQ(sheet->GetAttribute(ibT("name")), ibString(ibT("A & B")));
	EXPECT_EQ(sheet->GetAttribute(ibT("r:id")), ibString(ibT("rId7")));
	EXPECT_EQ(sheet->GetAttribute(ibT("id"), ibT("none")), ibString(ibT("none")));
	EXPECT_TRUE(sheet->HasAttribute(ibT("name")));
	EXPECT_FALSE(sheet->HasAttribute(ibT("Name")));
	EXPECT_TRUE(xml.GetRoot()->HasAttribute(ibT("xmlns:r")));
	EXPECT_EQ(sheet->GetAttribute(ibT("missing")), ibString());
}

TEST(XmlDocument, APrefixNobodyDeclaredIsPartOfTheNameAsWritten)
{
	// The tree resolves no namespace (ibXmlParser::ibReceiver): `x:` is two letters of the name, not a refusal.
	ibXmlDocument xml;
	xml.Load(ibT("<x:a y:b=\"1\"/>"));
	ASSERT_NE(xml.GetRoot(), nullptr);
	EXPECT_EQ(xml.GetRoot()->GetName(), ibString(ibT("x:a")));
	EXPECT_EQ(xml.GetRoot()->GetAttribute(ibT("y:b")), ibString(ibT("1")));
}

TEST(XmlDocument, TextIsOneNodeAcrossReferencesAndASpaceOnlyTextIsKept)
{
	ibXmlDocument xml;
	xml.Load(ibT("<r><t>Total &amp; 2026 &#x0418;</t><t xml:space=\"preserve\"> </t><t><![CDATA[<raw>]]></t><t/></r>"));
	const ibXmlNode* t = xml.GetRoot()->GetChildren();
	ASSERT_NE(t, nullptr);
	EXPECT_EQ(CountChildren(t), 1);
	EXPECT_EQ(t->GetNodeContent(), ibString(ibT("Total & 2026 \x0418")));
	t = t->GetNext();
	EXPECT_EQ(t->GetNodeContent(), ibString(ibT(" ")));
	t = t->GetNext();
	EXPECT_EQ(t->GetNodeContent(), ibString(ibT("<raw>")));
	t = t->GetNext();
	EXPECT_EQ(t->GetNodeContent(), ibString());
	EXPECT_EQ(t->GetNext(), nullptr);
}

TEST(XmlDocument, AnElementsChildrenKeepTheirKindsAndItsTextIsTheFirstTextChild)
{
	ibXmlDocument xml;
	xml.Load(ibT("<c><!--note--><?pi data?><v>1</v>tail</c>"));
	const ibXmlNode* c = xml.GetRoot();
	const ibXmlNode* child = c->GetChildren();
	ASSERT_NE(child, nullptr);
	EXPECT_EQ(child->GetType(), ibXmlNodeType_Comment);
	EXPECT_EQ(child->GetContent(), ibString(ibT("note")));
	child = child->GetNext();
	EXPECT_EQ(child->GetType(), ibXmlNodeType_ProcessingInstruction);
	EXPECT_EQ(child->GetName(), ibString(ibT("pi")));
	child = child->GetNext();
	EXPECT_EQ(child->GetType(), ibXmlNodeType_StartElement);
	EXPECT_EQ(child->GetName(), ibString(ibT("v")));
	child = child->GetNext();
	EXPECT_EQ(child->GetType(), ibXmlNodeType_Text);
	EXPECT_EQ(child->GetNext(), nullptr);
	EXPECT_EQ(c->GetNodeContent(), ibString(ibT("tail")));   // the first TEXT child, not the element's
}

TEST(XmlDocument, ADocumentLongerThanAChunkAndWiderThanTheQueueIsReadWhole)
{
	// 5000 rows - more nodes than the parser queues before it pauses, and more bytes than one chunk.
	std::wstring text = L"<sheetData>";
	for (int row = 1; row <= 5000; ++row)
		text += L"<row r=\"" + std::to_wstring(row) + L"\"><c><v>" + std::to_wstring(row * 2) + L"</v></c></row>";
	text += L"</sheetData>";
	ASSERT_GT(text.size(), size_t(65536));

	ibXmlDocument xml;
	xml.Load(ibString(text.c_str()));
	int rows = 0;
	for (const ibXmlNode* row = xml.GetRoot()->GetChildren(); row != nullptr; row = row->GetNext()) {
		++rows;
		ASSERT_EQ(row->GetAttribute(ibT("r")), ibString(std::to_wstring(rows).c_str()));
		ASSERT_EQ(row->GetChildren()->GetChildren()->GetNodeContent(), ibString(std::to_wstring(rows * 2).c_str()));
	}
	EXPECT_EQ(rows, 5000);
}

TEST(XmlDocument, ATextThatIsNotWellFormedIsRefusedAndLeavesNoTree)
{
	ibXmlDocument xml;
	xml.Load(ibT("<a/>"));
	ASSERT_NE(xml.GetRoot(), nullptr);
	EXPECT_THROW(xml.Load(ibT("<a><b></a>")), ibCoreException);
	EXPECT_EQ(xml.GetRoot(), nullptr);
	EXPECT_THROW(xml.Load(ibT("")), ibCoreException);
	EXPECT_EQ(xml.GetRoot(), nullptr);
	EXPECT_THROW(xml.Load(ibT("<!DOCTYPE a SYSTEM \"file:///etc/passwd\"><a/>")), ibCoreException);
	EXPECT_EQ(xml.GetRoot(), nullptr);
}

TEST(XmlDocument, BytesAreReadInTheEncodingTheirDeclarationNames)
{
	// "При" in windows-1251: no string of the whole document is made, the parser decodes the bytes themselves.
	const std::string bytes = "<?xml version=\"1.0\" encoding=\"windows-1251\"?><a>\xCF\xF0\xE8</a>";
	ibXmlDocument xml;
	xml.Load(bytes.data(), bytes.size());
	ASSERT_NE(xml.GetRoot(), nullptr);
	EXPECT_EQ(xml.GetRoot()->GetNodeContent(), ibString(ibT("\x041F\x0440\x0438")));
}

TEST(XmlDocument, ANodeKnowsTheLineItBeginsOn)
{
	ibXmlDocument xml;
	xml.Load(ibT("<a>\n\n<b\nc=\"1\"/></a>"));
	ASSERT_NE(xml.GetRoot(), nullptr);
	EXPECT_EQ(xml.GetRoot()->GetLine(), 1ull);
	const ibXmlNode* b = xml.GetRoot()->GetChildren()->GetNext();
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(b->GetName(), ibString(ibT("b")));
	EXPECT_EQ(b->GetLine(), 3ull);
}

TEST(XmlDocument, ASecondLoadReplacesTheFirstTree)
{
	ibXmlDocument xml;
	xml.Load(ibT("<a><x/></a>"));
	xml.Load(ibT("<b/>"));
	ASSERT_NE(xml.GetRoot(), nullptr);
	EXPECT_EQ(xml.GetRoot()->GetName(), ibString(ibT("b")));
	EXPECT_EQ(xml.GetRoot()->GetChildren(), nullptr);
}

TEST(XmlDocument, ADocumentBrokenPastItsFirstChunkLeavesNoTree)
{
	// The root and thousands of rows are taken before the parser meets the end that is not there.
	std::wstring text = L"<sheetData>";
	for (int row = 1; row <= 5000; ++row)
		text += L"<row r=\"" + std::to_wstring(row) + L"\"/>";
	ASSERT_GT(text.size(), size_t(65536));

	ibXmlDocument xml;
	EXPECT_THROW(xml.Load(ibString(text.c_str())), ibCoreException);
	EXPECT_EQ(xml.GetRoot(), nullptr);
}

TEST(XmlDocument, ATextAcrossTheEdgeOfAChunkIsOneNode)
{
	// 100 000 characters cannot fit in one 65 536-byte chunk, so the parser meets this text in two pieces at least.
	const std::wstring long_(100000, L'x');
	ibXmlDocument xml;
	xml.Load(ibString((L"<v>" + long_ + L"\U0001F600</v>").c_str()));
	const ibXmlNode* v = xml.GetRoot();
	ASSERT_NE(v->GetChildren(), nullptr);
	EXPECT_EQ(v->GetChildren()->GetNext(), nullptr);
	EXPECT_EQ(v->GetNodeContent(), ibString((long_ + L"\U0001F600").c_str()));
}

TEST(XmlDocument, EveryAttributeOfAnElementIsFoundAndNoneOfItsNeighbours)
{
	ibXmlDocument xml;
	xml.Load(ibT("<a x=\"1\" y=\"2\" z=\"3\"><b y=\"20\"/><c/></a>"));
	const ibXmlNode* a = xml.GetRoot();
	ASSERT_NE(a, nullptr);
	EXPECT_EQ(a->GetAttribute(ibT("x")), ibString(ibT("1")));
	EXPECT_EQ(a->GetAttribute(ibT("y")), ibString(ibT("2")));
	EXPECT_EQ(a->GetAttribute(ibT("z")), ibString(ibT("3")));
	const ibXmlNode* b = a->GetChildren();
	EXPECT_EQ(b->GetAttribute(ibT("y")), ibString(ibT("20")));
	EXPECT_FALSE(b->HasAttribute(ibT("x")));   // the parent's attributes are not the child's
	EXPECT_FALSE(b->HasAttribute(ibT("z")));
	EXPECT_FALSE(b->GetNext()->HasAttribute(ibT("y")));
}

// =============================================================================
// The parser handing its events straight to a receiver (ibXmlParser::ibReceiver) - the road the tree is built on.
// What the receiver should be told is written here by hand.
// =============================================================================

namespace {

struct Recording : ibXmlParser::ibReceiver {
	std::vector<std::string> m_eventArray;

	virtual void Start(const char* name, const char* const* attributes, unsigned long long line) override
	{
		std::string event = "start " + std::string(name) + " @" + std::to_string(line);
		for (const char* const* at = attributes; at[0] != nullptr; at += 2)
			event += " " + std::string(at[0]) + "=" + at[1];
		m_eventArray.push_back(event);
	}
	virtual void End() override { m_eventArray.push_back("end"); }
	virtual void Content(ibXmlNodeType type, const char* name, const char* text, size_t size, unsigned long long line) override
	{
		const char* kind = type == ibXmlNodeType_Text ? "text" : type == ibXmlNodeType_Whitespace ? "space"
			: type == ibXmlNodeType_CDATASection ? "cdata" : type == ibXmlNodeType_Comment ? "comment"
			: type == ibXmlNodeType_ProcessingInstruction ? "pi" : "?";
		m_eventArray.push_back(std::string(kind) + (name != nullptr ? " " + std::string(name) : std::string())
			+ " [" + std::string(text, size) + "] @" + std::to_string(line));
	}
};

void Parse(ibXmlParser& parser, const std::string& text)
{
	parser.Feed(text.data(), text.size(), true, ibString());
}

} // namespace

TEST(XmlParserReceiver, EveryEventGoesToTheReceiverInOrderAndTheQueueStaysEmpty)
{
	Recording recording;
	ibXmlParser parser(ibT("XML"), ibXmlParser::kBytes, recording);
	Parse(parser, "<a x=\"1\" xmlns:p=\"u\">\n<!--c--><?go now?>t&amp;u<![CDATA[x<y]]> <p:b/></a>");
	EXPECT_TRUE(parser.Queue().empty());
	const std::vector<std::string> expected = {
		"start a @1 x=1 xmlns:p=u",   // names as written: a declaration is an attribute like any other
		"space [\n] @1",
		"comment [c] @2",
		"pi go [now] @2",
		"text [t&u] @2",              // one text across the reference
		"cdata [x<y] @2",
		"space [ ] @2",
		"start p:b @2",
		"end",
		"end",
	};
	EXPECT_EQ(recording.m_eventArray, expected);
}

TEST(XmlParserReceiver, AReceiverThatRaisesRefusesTheTextWhereItStands)
{
	struct Refusing : Recording {
		virtual void Start(const char* name, const char* const* attributes, unsigned long long line) override
		{
			if (std::string(name) == "bad")
				ibCoreException::Error(ibT("no bad elements"));
			Recording::Start(name, attributes, line);
		}
	} refusing;
	ibXmlParser parser(ibT("XML"), ibXmlParser::kBytes, refusing);
	try {
		Parse(parser, "<a>\n<bad/><c/></a>");
		FAIL() << "the receiver's refusal did not reach the caller";
	}
	catch (const ibCoreException& error) {
		const ibString what = error.GetErrorDescription();
		EXPECT_NE(what.Find(ibT("no bad elements")), wxNOT_FOUND) << what;
		EXPECT_NE(what.Find(ibT("line 2")), wxNOT_FOUND) << what;
	}
	EXPECT_EQ(refusing.m_eventArray, std::vector<std::string>({ "start a @1", "space [\n] @1" }));   // nothing after it
}

TEST(XmlParserQueue, ATextKeepsWhereItBeganWhileItIsHeldAsBytes)
{
	// The queue's road (XMLReader's): a text is held as bytes until it ends, and its node still says where it began.
	ibXmlParser parser(ibT("XML"), ibXmlParser::kBytes);
	Parse(parser, "<a>\n<b/>  tail</a>");
	const ibXmlParser::ibNode* tail = nullptr;
	for (const ibXmlParser::ibNode& node : parser.Queue())
		if (node.m_type == ibXmlNodeType_Text)
			tail = &node;
	ASSERT_NE(tail, nullptr);
	EXPECT_EQ(tail->m_value, ibString(ibT("  tail")));
	EXPECT_EQ(tail->m_line, 2ull);
	EXPECT_EQ(tail->m_column, 5ull);   // right after `<b/>`, counted from 1
	EXPECT_EQ(tail->m_depth, 1);
}
