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
#include "core/core.h"            // kValueFieldClsid / kValueFieldData, the primitives' class ids

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
	original.AddField("key", ibDataValue::Guid(ibGuid(ibString("0f8fad5b-d9cb-469f-a165-70867728950e"))));
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
	// …and a guid, as its 16 bytes again.
	EXPECT_EQ(loaded.FindField("key")->Kind(), ibDataKind::Guid);
	EXPECT_EQ(loaded.FindField("key")->AsGuid(), original.FindField("key")->AsGuid());
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

// The text each kind is written as - a number with a point and every digit, a date to the millisecond with four digits
// of a year, a yes-or-no, a guid as its text, bytes as base64 - written here as literals, not produced by the writer
// under test.
TEST(XmlProvider, Write_TheFormsOfAValue)
{
	ibMemoryBuffer bytes;
	bytes.AppendData("OES", 3);
	ibDataNode root(1, 5);
	root.SetValue("big", ibNumber(ibString("1518500.10")));
	root.SetValue("long", ibNumber(ibString("-123456789012345678901234567890.0000000001")));
	root.SetValue("yes", true);
	root.SetValue("no", false);
	root.SetValue("date", ibDateTime(2026, 3, 29, 2, 30, 59, 7));
	root.SetValue("year", ibDateTime(1, 1, 2, 0, 0, 0, 0));
	root.SetValue("nodate", ibDateTime());
	root.AddField("key", ibDataValue::Guid(ibGuid(ibString("0f8fad5b-d9cb-469f-a165-70867728950e"))));
	root.SetValue("bytes", bytes);
	root.SetValue("nobytes", ibMemoryBuffer());
	const ibString text = Written(root);
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"big\" Kind=\"Number\">1518500.1</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"long\" Kind=\"Number\">-123456789012345678901234567890.0000000001</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"yes\" Kind=\"Bool\">true</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"no\" Kind=\"Bool\">false</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"date\" Kind=\"Date\">2026-03-29 02:30:59.007</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"year\" Kind=\"Date\">0001-01-02 00:00:00.000</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"nodate\" Kind=\"Date\"/>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"key\" Kind=\"Guid\">0f8fad5b-d9cb-469f-a165-70867728950e</Field>")));
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"bytes\" Kind=\"Binary\">T0VT</Field>")));   // base64 of "OES"
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"nobytes\" Kind=\"Binary\"></Field>")));
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
	EXPECT_THROW(provider.Read(ibString(ibT("<Node><Field Name=\"x\" Kind=\"Guid\">five</Field></Node>")), node), ibCoreException);
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

// =============================================================================
// A value's node says its type by XML Schema's own means - xsi:type, xsi:nil - and a primitive is its text. What a
// value writes is imitated here by hand (its type first, as field `t`, then its payload `v`); the expected text is a
// literal.
// =============================================================================

namespace {

ibDataNode& Valued(ibDataNode& node, ibClassID clsid)
{
	node.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid)));
	return node;
}

// A Catalog (type 1, as the registry above names it) holding one of every form.
ibDataNode AValueOfEveryForm()
{
	ibDataNode list;
	Valued(list, 1).SetValue("n", (std::int32_t)10);
	Valued(list.AddChild(g_valueNumberCLSID, 0), g_valueNumberCLSID).SetValue(kValueFieldData, ibNumber(ibString("1518500.1")));
	Valued(list.AddChild(g_valueNullCLSID, 0), g_valueNullCLSID);
	Valued(list.AddChild(g_valueStringCLSID, 0), g_valueStringCLSID).SetValue(kValueFieldData, ibString("a < b"));
	Valued(list.AddChild(g_valueStringCLSID, 0), g_valueStringCLSID).SetValue(kValueFieldData, ibString());
	Valued(list.AddChild(g_valueBooleanCLSID, 0), g_valueBooleanCLSID).SetValue(kValueFieldData, true);
	Valued(list.AddChild(g_valueDateCLSID, 0), g_valueDateCLSID).SetValue(kValueFieldData, ibDateTime(2026, 3, 29, 2, 30, 59, 7));
	Valued(list.AddChild(g_valueDateCLSID, 0), g_valueDateCLSID).SetValue(kValueFieldData, ibDateTime());
	Valued(list.AddChild(g_valueStringCLSID, 0), g_valueStringCLSID).SetValue(kValueFieldData, ibString::FromUTF8("a\0b", 3));
	Valued(list.AddChild(7, 0), 7);   // a type with no name
	Valued(list.AddChild(0, 3), 2).SetValue("Name", ibString("Price"));   // an id and a field of its own
	return list;
}

ibString Digits(ibClassID clsid)
{
	return ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid));
}

// A string XML cannot hold is no xs:string: the type by its number (the registry above names no String), the payload
// as any field.
ibString EveryForm()
{
	return ibString(ibT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
	ibT("<Node Type=\"\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:type=\"Catalog\">\n")
	ibT("\t<Field Name=\"n\" Kind=\"Number\">10</Field>\n")
	ibT("\t<Children>\n")
	ibT("\t\t<Node xsi:type=\"xs:decimal\">1518500.1</Node>\n")
	ibT("\t\t<Node xsi:nil=\"true\"/>\n")
	ibT("\t\t<Node xsi:type=\"xs:string\">a &lt; b</Node>\n")
	ibT("\t\t<Node xsi:type=\"xs:string\"/>\n")
	ibT("\t\t<Node xsi:type=\"xs:boolean\">true</Node>\n")
	ibT("\t\t<Node xsi:type=\"xs:dateTime\">2026-03-29T02:30:59.007</Node>\n")
	ibT("\t\t<Node xsi:type=\"xs:dateTime\">0001-01-01T00:00:00</Node>\n")
	ibT("\t\t<Node xsi:type=\"")) + Digits(g_valueStringCLSID) + ibT("\">\n")
	ibT("\t\t\t<Field Name=\"v\" Kind=\"String\" Encoding=\"base64\">YQBi</Field>\n")   // a NUL: base64 of its UTF-8
	ibT("\t\t</Node>\n")
	ibT("\t\t<Node xsi:type=\"7\"/>\n")
	ibT("\t\t<Node Type=\"\" Id=\"3\" xsi:type=\"Attribute\">\n")
	ibT("\t\t\t<Field Name=\"Name\">Price</Field>\n")
	ibT("\t\t</Node>\n")
	ibT("\t</Children>\n")
	ibT("</Node>\n");
}

ibString TypeField(const ibDataNode& node)
{
	const auto fields = node.Fields();
	return fields.size() != 0 && fields.front().first == kValueFieldClsid ? fields.front().second.AsString() : ibString();
}

} // namespace

TEST(XmlProvider, Write_AValueSaysItsTypeByXmlSchemaAndAPrimitiveIsItsText)
{
	EXPECT_EQ(Written(AValueOfEveryForm()), EveryForm());
}

TEST(XmlProvider, Read_AValueComesBackWithItsTypeFirstAndWritesTheSameText)
{
	const ibDataNode loaded = ReadBack(EveryForm());
	// The node that was written, field for field and byte for byte - its type the value's first field again, and a node
	// of no type of its own (the root here, and the last child) still of none.
	EXPECT_TRUE(loaded == AValueOfEveryForm());
	EXPECT_EQ(TypeField(loaded), ibString(ibT("1")));
	EXPECT_EQ(loaded.GetClsid(), ibClassID(0));
	EXPECT_EQ(loaded.GetValue<std::int32_t>("n"), 10);
	const auto children = loaded.Children();
	ASSERT_EQ(children.size(), 10u);
	EXPECT_EQ(TypeField(children[0]), Digits(g_valueNumberCLSID));
	EXPECT_EQ(children[0].GetClsid(), g_valueNumberCLSID);
	EXPECT_EQ(children[0].FindField(kValueFieldData)->Kind(), ibDataKind::Number);
	EXPECT_EQ(children[0].GetValue<ibNumber>(kValueFieldData).ToString(), ibString(ibT("1518500.1")));
	EXPECT_EQ(TypeField(children[1]), Digits(g_valueNullCLSID));
	EXPECT_EQ(children[1].Fields().size(), 1u);   // a Null is its type alone
	EXPECT_EQ(children[2].GetValue<ibString>(kValueFieldData), ibString(ibT("a < b")));
	EXPECT_EQ(children[3].FindField(kValueFieldData)->Kind(), ibDataKind::String);
	EXPECT_TRUE(children[3].GetValue<ibString>(kValueFieldData).IsEmpty());
	EXPECT_EQ(children[4].FindField(kValueFieldData)->Kind(), ibDataKind::Bool);
	EXPECT_TRUE(children[4].GetValue<bool>(kValueFieldData));
	EXPECT_EQ(children[5].FindField(kValueFieldData)->Kind(), ibDataKind::Date);
	EXPECT_EQ(children[5].GetValue<ibDateTime>(kValueFieldData).GetValue(), ibDateTime(2026, 3, 29, 2, 30, 59, 7).GetValue());
	EXPECT_TRUE(children[6].GetValue<ibDateTime>(kValueFieldData).IsEmpty());
	EXPECT_EQ(children[7].GetValue<ibString>(kValueFieldData), ibString::FromUTF8("a\0b", 3));
	EXPECT_EQ(TypeField(children[8]), ibString(ibT("7")));
	EXPECT_EQ(children[9].GetClsid(), ibClassID(0));
	EXPECT_EQ(children[9].GetMetaId(), 3);
	EXPECT_EQ(TypeField(children[9]), ibString(ibT("2")));
	EXPECT_EQ(children[9].GetValue<ibString>("Name"), ibString(ibT("Price")));

	EXPECT_EQ(Written(loaded), EveryForm());
}

TEST(XmlProvider, WriteRead_ANumbersTrailingZerosAreNotKept)
{
	// The text is the number's own, with no zeros after its last digit: 1518500.10 reads back as 1518500.1 - the same
	// number, written with one decimal place fewer. As the Field form always did.
	ibDataNode node;
	Valued(node, g_valueNumberCLSID).SetValue(kValueFieldData, ibNumber(ibString("1518500.10")));
	const ibString text = Written(node);
	EXPECT_TRUE(text.Contains(ibT(">1518500.1</Node>"))) << text;
	const ibNumber read = ReadBack(text).GetValue<ibNumber>(kValueFieldData);
	EXPECT_TRUE(read == ibNumber(ibString("1518500.10")));
	EXPECT_EQ(read.ToString(), ibString(ibT("1518500.1")));
}

TEST(XmlProvider, WriteRead_APrimitiveWithNoPayloadIsSaidByItsType)
{
	// Only text is said by XML Schema's word: with nothing to write, the type goes by its number and reads back alone.
	for (const ibClassID clsid : { g_valueNumberCLSID, g_valueStringCLSID, g_valueBooleanCLSID, g_valueDateCLSID }) {
		ibDataNode node;
		Valued(node, clsid);
		const ibString text = Written(node);
		EXPECT_TRUE(text.Contains(ibT("xsi:type=\"") + Digits(clsid) + ibT("\"/>"))) << text;
		const ibDataNode loaded = ReadBack(text);
		EXPECT_EQ(loaded.Fields().size(), 1u) << text;
		EXPECT_EQ(TypeField(loaded), Digits(clsid));
	}
}

TEST(XmlProvider, WriteRead_OnlyAPrimitivesPayloadAloneIsItsText)
{
	// Beside another field, under another name, of another kind: each is said by the type's number and written field by
	// field, and comes back as it went.
	ibDataNode extra;
	Valued(extra, g_valueNumberCLSID).SetValue(kValueFieldData, ibNumber(5));
	extra.SetValue("more", ibString(ibT("x")));
	ibDataNode named;
	Valued(named, g_valueNumberCLSID).SetValue("w", ibNumber(5));
	ibDataNode other;
	Valued(other, g_valueNumberCLSID).SetValue(kValueFieldData, ibString(ibT("5")));
	for (const ibDataNode* node : { &extra, &named, &other }) {
		const ibString text = Written(*node);
		EXPECT_FALSE(text.Contains(ibT("xs:decimal"))) << text;
		EXPECT_TRUE(text.Contains(ibT("xsi:type=\"") + Digits(g_valueNumberCLSID) + ibT("\""))) << text;
		const ibDataNode loaded = ReadBack(text);
		EXPECT_EQ(loaded.Fields().size(), node->Fields().size()) << text;
		EXPECT_EQ(Written(loaded), text);
	}
	EXPECT_EQ(ReadBack(Written(extra)).GetValue<ibString>("more"), ibString(ibT("x")));
	EXPECT_EQ(ReadBack(Written(other)).FindField(kValueFieldData)->Kind(), ibDataKind::String);
}

TEST(XmlProvider, Write_ALookupThatRefusesTheNameLeavesTheTypeANumber)
{
	ibXmlProvider provider = Provider();
	provider.SetTypeLookup([](const ibString& name) -> ibClassID {
		ibCoreException::Error(ibT("Object '%s' is not exist"), name);
	});
	ibDataNode node;
	Valued(node, 1);
	ibString text;
	ASSERT_TRUE(provider.Write(node, text));
	EXPECT_TRUE(text.Contains(ibT("xsi:type=\"1\""))) << text;
}

TEST(XmlProvider, Write_ANameThatReadsBackAsAnotherTypeIsNotUsed)
{
	// Type 5 answers to "Catalog" too, but "Catalog" reads back as type 1: the value's type is said by its number.
	ibXmlProvider provider = Provider();
	provider.SetTypeResolver([](ibClassID clsid) { return clsid == 1 || clsid == 5 ? ibString(ibT("Catalog")) : ibString(); });
	ibDataNode node;
	Valued(node, 5);
	ibString text;
	ASSERT_TRUE(provider.Write(node, text));
	EXPECT_TRUE(text.Contains(ibT("xsi:type=\"5\""))) << text;
}

TEST(XmlProvider, Read_ALookupThatRefusesANameIsRefusedAtItsLine)
{
	ibXmlProvider provider;
	provider.SetTypeLookup([](const ibString& name) -> ibClassID {
		ibCoreException::Error(ibT("Object '%s' is not exist"), name);
	});
	ibDataNode node;
	try {
		provider.Read(ibString(ibT("<Node xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n<Children><Node xsi:type=\"Gone\"/></Children></Node>")), node);
		FAIL() << "the lookup's refusal did not reach the caller";
	}
	catch (const ibCoreException& error) {
		EXPECT_EQ(error.GetErrorDescription(), ibString(ibT("ibXmlProvider: Object 'Gone' is not exist, line 2")));
	}
}

TEST(XmlProvider, Write_ADateOutsideTheYears1To9999_IsRefused)
{
	// The day before the first one, as the value and as a field: four digits of a year hold the years 1 to 9999.
	ibDataNode value;
	Valued(value, g_valueDateCLSID).SetValue(kValueFieldData, ibDateTime().AddDays(-1));
	ibDataNode field;
	field.SetValue("date", ibDateTime().AddDays(-1));
	for (const ibDataNode* node : { &value, &field }) {
		ibString text;
		EXPECT_THROW(Provider().Write(*node, text), ibCoreException);
	}
}

TEST(XmlProvider, Write_AFieldCalledTThatIsNoValuesTypeIsAField)
{
	// A number, digits written another way, not the first field, digits under another name: none of these is what a
	// value writes.
	ibDataNode named;
	named.SetValue("Count", ibString(ibT("5")));
	ibDataNode number;
	number.SetValue(kValueFieldClsid, ibNumber(5));
	ibDataNode padded;
	padded.SetValue(kValueFieldClsid, ibString(ibT("05")));
	ibDataNode second;
	second.SetValue("Name", ibString(ibT("x")));
	second.SetValue(kValueFieldClsid, ibString(ibT("5")));
	for (const ibDataNode* node : { &number, &padded, &second }) {
		const ibString text = Written(*node);
		EXPECT_FALSE(text.Contains(ibT("xsi:"))) << text;
		EXPECT_TRUE(text.Contains(ibT("<Field Name=\"t\""))) << text;
	}
	const ibString text = Written(named);
	EXPECT_FALSE(text.Contains(ibT("xsi:"))) << text;
	EXPECT_TRUE(text.Contains(ibT("<Field Name=\"Count\">5</Field>"))) << text;
}

TEST(XmlProvider, WriteRead_ANodeOfATypeOtherThanItsValuesSaysBoth)
{
	// The node an Attribute, the value it packs a Catalog: neither type is the other, so both are said.
	ibDataNode node(2, 0);
	Valued(node, 1).SetValue("Name", ibString(ibT("Goods")));
	const ibString text = Written(node);
	EXPECT_TRUE(text.Contains(ibT("<Node Type=\"Attribute\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:type=\"Catalog\">")))
		<< text;
	const ibDataNode loaded = ReadBack(text);
	EXPECT_EQ(loaded.GetClsid(), ibClassID(2));
	EXPECT_EQ(TypeField(loaded), ibString(ibT("1")));
	EXPECT_EQ(Written(loaded), text);
}

TEST(XmlProvider, Read_TheFormBeforeXsiTypeStillReads)
{
	const ibDataNode node = ReadBack(ibT("<Node Type=\"Catalog\"><Field Name=\"t\">1</Field><Field Name=\"v\" Kind=\"Number\">5</Field></Node>"));
	EXPECT_EQ(node.GetClsid(), ibClassID(1));
	EXPECT_EQ(TypeField(node), ibString(ibT("1")));
	EXPECT_EQ(node.GetValue<ibNumber>(kValueFieldData).ToString(), ibString(ibT("5")));
}

TEST(XmlProvider, Read_APrefixIsWhatItsDeclarationSays)
{
	// XML Schema under a prefix of the document's choosing, declared further out.
	const ibDataNode node = ReadBack(ibT("<Node xmlns:s=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:type=\"Catalog\">")
		ibT("<Children><Node xsi:type=\"s:boolean\">1</Node></Children></Node>"));
	ASSERT_EQ(node.Children().size(), 1u);
	EXPECT_TRUE(node.Children()[0].GetValue<bool>(kValueFieldData));
}

TEST(XmlProvider, Read_AValuesTypeThatCannotBeRead_Refuses)
{
	const ibString declared = ibT("xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"");
	const auto refusal = [&](const ibString& text) {
		ibDataNode node;
		try {
			Provider().Read(text, node);
		}
		catch (const ibCoreException& error) {
			return error.GetErrorDescription();
		}
		return ibString(ibT("(read)"));
	};
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"Nobody\"/>")).Find(ibT("no type is called 'Nobody'")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"xs:float\">1</Node>")).Find(ibT("'xs:float'")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"q:decimal\">1</Node>")).Find(ibT("does not stand for XML Schema")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node xmlns:xsi=\"urn:other\" xsi:type=\"Catalog\"/>")).Find(ibT("XML Schema instance")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"xs:decimal\">five</Node>")).Find(ibT("'five' is no xs:decimal")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"xs:boolean\">yes</Node>")).Find(ibT("is no xs:boolean")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"xs:dateTime\"/>")).Find(ibT("is no xs:dateTime")), wxNOT_FOUND);
	// Only the form a dateTime is written in: no other order, no date alone, no zone.
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"xs:dateTime\">30.10.2026</Node>")).Find(ibT("is no xs:dateTime")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"xs:dateTime\">2026-10-30</Node>")).Find(ibT("is no xs:dateTime")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"xs:dateTime\">2026-10-30T10:00:00Z</Node>")).Find(ibT("is no xs:dateTime")), wxNOT_FOUND);
	// A number only as the writer writes one; `+5` is a name, and nobody is called that.
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:type=\"+5\"/>")).Find(ibT("no type is called '+5'")), wxNOT_FOUND);
	// A Type nobody knows, by a lookup that refuses it: refused at its line.
	ibXmlProvider refusing;
	refusing.SetTypeLookup([](const ibString& name) -> ibClassID { ibCoreException::Error(ibT("Object '%s' is not exist"), name); });
	ibDataNode node;
	try {
		refusing.Read(ibString(ibT("<Node Type=\"Gone\"/>")), node);
		ADD_FAILURE() << "an unknown Type was read";
	}
	catch (const ibCoreException& error) {
		EXPECT_EQ(error.GetErrorDescription(), ibString(ibT("ibXmlProvider: Object 'Gone' is not exist, line 1")));
	}
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:nil=\"maybe\"/>")).Find(ibT("xsi:nil 'maybe'")), wxNOT_FOUND);
	// A Null is its type alone.
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:nil=\"true\" xsi:type=\"xs:string\"/>")).Find(ibT("xsi:nil holds nothing")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:nil=\"true\">x</Node>")).Find(ibT("xsi:nil holds nothing")), wxNOT_FOUND);
	EXPECT_NE(refusal(ibT("<Node ") + declared + ibT(" xsi:nil=\"true\"><Field Name=\"a\">x</Field></Node>")).Find(ibT("xsi:nil holds nothing")), wxNOT_FOUND);
}
