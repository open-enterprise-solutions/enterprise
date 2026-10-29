// =============================================================================
// A whole document as a tree - what the workbook reader walks. Every expected name and text is written here as a
// literal, and the tree is walked by hand: the reference is not the reader.
// =============================================================================

#include <gtest/gtest.h>

#include <string>

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
	// The tree resolves no namespace (ibXmlParser::kAsWritten): `x:` is two letters of the name, not a refusal.
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
	EXPECT_EQ(xml.GetRoot()->GetNodeContent(), ibString(L"При"));
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
