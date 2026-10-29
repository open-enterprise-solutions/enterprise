////////////////////////////////////////////////////////////////////////////
//	Description : ibXmlProvider Write -> Read tests. Unlike the JSON view the XML form loses nothing, so these pin down
//	              that every kind comes back as itself — a date a date, a string with a NUL a string — and that what is
//	              not the layout is refused with its line. Pure structure layer — no DB, no metadata bring-up.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "core/serialize/provider/xmlProvider.h"
#include "core/exception.h"

namespace {

// A registry of two, both ways: type 1 is a Catalog, type 2 an Attribute; any other has no name.
ibXmlProvider Provider()
{
	ibXmlProvider provider;
	provider.SetTypeResolver([](ibClassID clsid) {
		return clsid == 1 ? ibString(ibT("Catalog")) : clsid == 2 ? ibString(ibT("Attribute")) : ibString();
	});
	provider.SetTypeLookup([](const ibString& name) {
		return name == ibT("Catalog") ? ibClassID(1) : name == ibT("Attribute") ? ibClassID(2) : ibClassID(0);
	});
	return provider;
}

ibString Written(const ibDataNode& node)
{
	ibString text;
	EXPECT_TRUE(Provider().Write(node, text));
	return text;
}

ibDataNode ReadBack(const ibString& text)
{
	ibDataNode node;
	EXPECT_TRUE(Provider().Read(text, node));
	return node;
}

} // namespace

TEST(XmlProvider, WriteRead_AllKinds_ComeBackAsThemselves)
{
	ibMemoryBuffer bytes;
	bytes.AppendData("\0\1\xff", 3);
	ibDataNode original(0xfedcba9876543210ULL, -42);
	original.AddField("same", ibDataValue());
	original.AddField("same", ibDataValue::Bool(true));
	original.SetValue("same", ibString("a string"));
	original.SetValue("nul", ibString::FromUTF8("a\0b", 3));
	original.SetValue("Module", ibString::FromUTF8("Unicode: \xc3\xa9 \xf0\x9f\x98\x80\r\nnext line\n"));
	original.SetValue("blank", ibString("  "));
	original.SetValue("empty", ibString());
	original.SetValue("number", ibNumber(ibString("123456789012345678901234567890.1234567890123456789")));
	original.SetValue("date", ibDateTime(2026, 3, 29, 2, 30, 59, 123));
	original.SetValue("nodate", ibDateTime());
	original.SetValue("Picture", bytes);
	original.SetRawData(bytes);
	original.AddField("array", ibDataValue::Array({ ibDataValue::Int(-2), ibDataValue(), ibDataValue::String(ibString(" ")) }));
	original.Child("nested").AddField("value", ibDataValue::UInt(18446744073709551615ULL));
	original.AddChild(1, 2).SetValue("Name", ibString("Goods"));

	const ibString text = Written(original);
	const ibDataNode loaded = ReadBack(text);

	EXPECT_EQ(loaded.GetClsid(), original.GetClsid());
	EXPECT_EQ(loaded.GetMetaId(), original.GetMetaId());
	std::vector<ibDataKind> same;
	for (const auto& [name, value] : loaded.Fields()) {
		if (name == ibT("same"))
			same.push_back(value.Kind());
	}
	EXPECT_EQ(same, (std::vector<ibDataKind>{ ibDataKind::Empty, ibDataKind::Bool, ibDataKind::String }));
	EXPECT_EQ(loaded.GetValue<ibString>("nul"), original.GetValue<ibString>("nul"));
	EXPECT_EQ(loaded.GetValue<ibString>("Module"), original.GetValue<ibString>("Module"));   // \r\n kept, not \n
	EXPECT_EQ(loaded.GetValue<ibString>("blank"), ibString("  "));
	EXPECT_EQ(loaded.FindField("empty")->Kind(), ibDataKind::String);
	EXPECT_EQ(loaded.GetValue<ibNumber>("number").ToString(), original.GetValue<ibNumber>("number").ToString());
	// A date says so and reads back as one, to the millisecond — not a string that looks like one.
	EXPECT_EQ(loaded.FindField("date")->Kind(), ibDataKind::Date);
	EXPECT_EQ(loaded.GetValue<ibDateTime>("date").GetValue(), ibDateTime(2026, 3, 29, 2, 30, 59, 123).GetValue());
	EXPECT_TRUE(loaded.GetValue<ibDateTime>("nodate").IsEmpty());
	const ibMemoryBuffer picture = loaded.FindField("Picture")->AsBinary();
	ASSERT_EQ(picture.GetDataLen(), 3u);
	EXPECT_EQ(std::memcmp(picture.GetData(), bytes.GetData(), 3), 0);
	ASSERT_EQ(loaded.RawData().GetDataLen(), 3u);
	const auto array = loaded.FindField("array");
	ASSERT_TRUE(array && array->Kind() == ibDataKind::Array && array->AsArray().size() == 3u);
	EXPECT_EQ(array->AsArray()[0].AsInt(), -2);
	EXPECT_EQ(array->AsArray()[1].Kind(), ibDataKind::Empty);
	EXPECT_EQ(array->AsArray()[2].AsString(), ibString(" "));
	EXPECT_EQ(loaded.FindChild("nested")->FindField("value")->AsUInt(), 18446744073709551615ULL);
	ASSERT_EQ(loaded.Children().size(), 1u);
	EXPECT_EQ(loaded.Children()[0].GetClsid(), ibClassID(1));
	EXPECT_EQ(loaded.Children()[0].GetMetaId(), 2);
	EXPECT_EQ(loaded.Children()[0].GetValue<ibString>("Name"), ibString("Goods"));

	// …and what was read writes the same text again.
	EXPECT_EQ(Written(loaded), text);
}

TEST(XmlProvider, Write_TheLayout)
{
	ibDataNode root(1, 5);
	root.SetValue("Name", ibString("Goods"));
	root.SetValue("Count", ibNumber(5));
	root.SetValue("a:b", ibString("x"));
	root.SetValue("nul", ibString::FromUTF8("a\0b", 3));
	root.AddChild(7, 1);
	const ibString text = Written(root);
	EXPECT_TRUE(text.StartsWith(ibT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>")));
	EXPECT_TRUE(text.Contains(ibT("<Node Type=\"Catalog\" Id=\"5\">")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"Name\">Goods</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"Count\" Kind=\"Number\">5</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"a:b\">x</Field>")));   // a name is a value, not an element's name
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"nul\" Kind=\"String\" Encoding=\"base64\">")));
	EXPECT_TRUE(text.Contains(ibT("<Node Type=\"7\" Id=\"1\"/>")));      // a type with no name — by its number
}

TEST(XmlProvider, Write_Bytes_ReadBackTheSame)
{
	ibDataNode root(2, 3);
	root.SetValue("Text", ibString::FromUTF8("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82"));
	ibWriterMemory writer;
	ASSERT_TRUE(Provider().Write(root, writer));
	ibReaderMemory reader(writer.buffer());
	ibDataNode loaded;
	ASSERT_TRUE(Provider().Read(reader, loaded));
	EXPECT_EQ(loaded.GetClsid(), ibClassID(2));
	EXPECT_EQ(loaded.GetValue<ibString>("Text"), root.GetValue<ibString>("Text"));
}

TEST(XmlProvider, Read_TypeNobodyKnows_StaysAWord)
{
	const ibDataNode node = ReadBack(ibT("<Node Type=\"Unknown\"><Field Name=\"x\">1</Field></Node>"));
	EXPECT_EQ(node.GetClsid(), ibClassID(0));
	EXPECT_EQ(node.GetValue<ibString>("NodeType"), ibString("Unknown"));
	EXPECT_EQ(node.GetValue<ibString>("x"), ibString("1"));
}

TEST(XmlProvider, Read_CommentsAndIndentation_AreNotData)
{
	const ibDataNode node = ReadBack(ibT("<?xml version=\"1.0\"?>\n<!-- a note -->\n<Node>\n\t<Field Name=\"x\" Kind=\"Array\">\n")
		ibT("\t\t<Item Kind=\"Number\">1</Item>\n\t</Field>\n\t<Children>\n\t\t<Node Type=\"Catalog\"/>\n\t</Children>\n</Node>\n"));
	const auto x = node.FindField("x");
	ASSERT_TRUE(x && x->Kind() == ibDataKind::Array && x->AsArray().size() == 1u);
	EXPECT_EQ(x->AsArray()[0].AsInt(), 1);
	ASSERT_EQ(node.Children().size(), 1u);
	EXPECT_EQ(node.Children()[0].GetClsid(), ibClassID(1));
}

TEST(XmlProvider, Read_WhatIsNotTheLayout_Refuses)
{
	ibDataNode node;
	const ibXmlProvider provider = Provider();
	EXPECT_THROW(provider.Read(ibString(ibT("<Node><Field Name=\"x\"")), node), ibCoreException);           // malformed
	EXPECT_THROW(provider.Read(ibString(ibT("<Root/>")), node), ibCoreException);                            // not a Node
	EXPECT_THROW(provider.Read(ibString(ibT("<Node><Other/></Node>")), node), ibCoreException);              // unknown element
	EXPECT_THROW(provider.Read(ibString(ibT("<Node>text</Node>")), node), ibCoreException);                   // text in a node
	EXPECT_THROW(provider.Read(ibString(ibT("<Node><Field>x</Field></Node>")), node), ibCoreException);       // no Name
	EXPECT_THROW(provider.Read(ibString(ibT("<Node><Field Name=\"x\" Kind=\"Number\">five</Field></Node>")), node), ibCoreException);
	EXPECT_THROW(provider.Read(ibString(ibT("<Node><Field Name=\"x\" Kind=\"Bool\">yes</Field></Node>")), node), ibCoreException);
	EXPECT_THROW(provider.Read(ibString(ibT("<Node><Field Name=\"x\" Kind=\"Binary\">*</Field></Node>")), node), ibCoreException);
	EXPECT_THROW(provider.Read(ibString(ibT("<Node><Field Name=\"x\" Kind=\"Other\"/></Node>")), node), ibCoreException);
	EXPECT_THROW(provider.Read(ibString(ibT("<Node Id=\"one\"/>")), node), ibCoreException);
}

TEST(XmlProvider, Read_NestingPastTheCeiling_Refuses)
{
	ibString text;
	for (int i = 0; i < ibXmlProvider::kMaxNesting + 1; ++i)
		text += ibT("<Node><Children>");
	for (int i = 0; i < ibXmlProvider::kMaxNesting + 1; ++i)
		text += ibT("</Children></Node>");
	ibDataNode node;
	EXPECT_THROW(Provider().Read(text, node), ibCoreException);
}
