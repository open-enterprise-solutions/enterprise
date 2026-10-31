// =============================================================================
// XMLString / XMLValue - a value through its node and the XML provider, and back: every kind a script holds
// comes back as itself, against a configuration made in memory. The text's own forms (a number, a date, base64)
// are the provider's and are pinned in test_xmlProvider.cpp.
// =============================================================================

#include <gtest/gtest.h>

#include "activeMetaDataAccess.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/metadataConfiguration.h"
#include "backend/metadataSerialization.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/enumeration.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/system/systemManager.h"
#include "backend/system/systemManagerEnum.h"   // XMLNodeType - a system enumeration
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueBinaryData.h"
#include "backend/system/value/valueGuid.h"
#include "backend/system/value/valueMap.h"
#include "core/clsid.h"
#include "core/serialize/dataBuilder.h"

#include <wx/init.h>

#include <functional>
#include <vector>

namespace {

ibString Refusal(const std::function<void()>& body)
{
	try {
		body();
	}
	catch (const ibCoreException& error) {
		return error.GetErrorDescription();
	}
	return ibString();
}

// Written, read back as the type the text names, and compared as the language compares - the value is its own reference.
ibValue RoundTrip(const ibValue& value)
{
	const ibString text = ibValueSystemFunction::XMLString(value);
	EXPECT_TRUE(text.StartsWith(ibT("<?xml"))) << text.ToStdString();
	return ibValueSystemFunction::XMLValue(text);
}

} // namespace

TEST(XmlValueFunctions, EveryKindGoesToXmlAndComesBackAsItself)
{
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	std::shared_ptr<ibMetaDataConfigurationFile> cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
	ActiveMetaDataAccess::Replace(cfg);
	struct Release {
		const short codeStyle = ibCompileCode::GetCodeStyle();
		~Release()
		{
			ibCompileCode::SetCodeStyle(codeStyle);
			ibApplicationInstance::DestroyAppDataEnv();
		}
	} release;

	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	ibValueMetaObject* goods = cfg->CreateMetaObject(g_metaCatalogCLSID, root, false);
	auto* rates = dynamic_cast<ibValueMetaObjectEnumeration*>(cfg->CreateMetaObject(g_metaEnumerationCLSID, root, false));
	ASSERT_NE(goods, nullptr);
	ASSERT_NE(rates, nullptr);
	goods->SetName(wxT("Goods"));
	rates->SetName(wxT("VatRates"));
	auto* vat20 = dynamic_cast<ibValueMetaObjectEnum*>(cfg->CreateMetaObject(g_metaEnumCLSID, rates, false));
	ASSERT_NE(vat20, nullptr);
	vat20->SetName(wxT("Vat20"));
	ASSERT_TRUE(cfg->RunDatabase());
	ASSERT_TRUE(cfg->IsConfigOpen());

	// The primitives: exact, to the millisecond, with what XML cannot hold as text.
	const ibValue exact(ibNumber(ibString(ibT("123456789012345678901234567890.0000000001"))));
	EXPECT_TRUE(RoundTrip(exact).CompareValueEQ(exact));
	EXPECT_TRUE(RoundTrip(ibValue(-0.5)).CompareValueEQ(ibValue(-0.5)));
	EXPECT_TRUE(RoundTrip(ibValue(true)).GetBoolean());
	EXPECT_FALSE(RoundTrip(ibValue(false)).GetBoolean());
	const ibValue moment(ibDateTime(2026, 10, 29, 12, 30, 59, 123));
	EXPECT_TRUE(RoundTrip(moment).CompareValueEQ(moment));
	EXPECT_TRUE(RoundTrip(ibValue(1, 1, 1, 0, 0, 0)).IsEmpty());
	const ibValue awkward(ibString::FromUTF8("<a & \"b\">\r\n\xd0\x9f\xf0\x9f\x98\x80 \0end", 22));
	ASSERT_NE(awkward.GetString().Find(ibT("end")), wxNOT_FOUND);   // the NUL is inside, not the end
	EXPECT_EQ(RoundTrip(awkward).GetString(), awkward.GetString());
	EXPECT_EQ(RoundTrip(ibValue(ibString())).GetString(), ibString());
	EXPECT_EQ(RoundTrip(ibValue()).GetType(), ibValueTypes::TYPE_EMPTY);
	EXPECT_EQ(RoundTrip(ibValue(ibValueTypes::TYPE_NULL)).GetType(), ibValueTypes::TYPE_NULL);

	// A Guid and BinaryData pack themselves now - a Guid as a Guid, not as the string its text would read as; and from its
	// text, where a text form left it a string (the JSON view).
	const ibGuid key(ibString(ibT("0f8fad5b-d9cb-469f-a165-70867728950e")));
	ibValuePtr<ibValueGuid> guid(new ibValueGuid(key));
	const ibValue guidBack = RoundTrip(guid);
	EXPECT_EQ(guidBack.GetClassType(), guid->GetClassType());
	EXPECT_EQ(guidBack.GetString(), key.str());
	ibValuePtr<ibValueGuid> nil(new ibValueGuid(ibGuid()));
	EXPECT_EQ(RoundTrip(nil).GetString(), ibGuid().str());
	ibDataNode guidAsText;
	guidAsText.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"), (unsigned long long)guid->GetClassType()));
	guidAsText.SetValue(kValueFieldData, ibString(key.str()));
	EXPECT_EQ(cfg->Deserialize(guidAsText).GetString(), key.str());
	ibMemoryBuffer bytes;
	bytes.AppendData("\0\1\xff", 3);
	ibValuePtr<ibValueBinaryData> binary(new ibValueBinaryData(bytes));
	const ibValue binaryBack = RoundTrip(binary);
	EXPECT_TRUE(binaryBack.CompareValueEQ(binary));
	ibValuePtr<ibValueBinaryData> noBytes(new ibValueBinaryData(ibMemoryBuffer()));
	EXPECT_TRUE(RoundTrip(noBytes).IsEmpty());

	// A system enumeration's value, a reference, a configuration's enumeration value and the empty ones.
	const ibValue text = ibValue::CreateEnumObject<ibValueEnumXmlNodeType>(ibXmlNodeType_CDATASection);
	EXPECT_TRUE(RoundTrip(text).CompareValueEQ(text));

	ibValuePtr<ibValueReferenceDataObject> item(ibValueReferenceDataObject::Create(cfg.get(), goods->GetMetaID(), key));
	EXPECT_TRUE(RoundTrip(item).CompareValueEQ(item));
	ibValuePtr<ibValueReferenceDataObject> noItem(ibValueReferenceDataObject::Create(cfg.get(), goods->GetMetaID()));
	EXPECT_TRUE(RoundTrip(noItem).IsEmpty());
	ibValuePtr<ibValueReferenceDataObject> rate(ibValueReferenceDataObject::Create(cfg.get(), rates->GetMetaID(), vat20->GetGuid()));
	EXPECT_TRUE(RoundTrip(rate).CompareValueEQ(rate));

	// …copied, as a parameter passed by value is: the copy is the configuration's to make too.
	EXPECT_TRUE(ibValue(item).CloneValue().CompareValueEQ(item));
	EXPECT_TRUE(ibValue(rate).CloneValue().CompareValueEQ(rate));

	// …and through the door both ways: the configuration handed in, and none - the one this thread works in.
	ibDataNode packedItem;
	ibSerializeValue(ibValue(item), packedItem);
	EXPECT_TRUE(ibDeserializeValue(packedItem, cfg.get()).CompareValueEQ(item));
	EXPECT_TRUE(ibDeserializeValue(packedItem).CompareValueEQ(item));

	// An array and a structure come back with what they hold - a reference and a configuration's enumeration value
	// among it, made by the configuration as the one at the top is: through XML, through JSON, and as a Val copy.
	ibValuePtr<ibValueArray> list(new ibValueArray());
	list->Add(ibValue(1));
	list->Add(item);
	list->Add(rate);
	ibValuePtr<ibValueStructure> record(new ibValueStructure());
	record->Insert(ibValue(ibString(ibT("Item"))), item);
	record->Insert(ibValue(ibString(ibT("Rate"))), rate);
	record->Insert(ibValue(ibString(ibT("List"))), list);
	ibValuePtr<ibValueContainer> byRef(new ibValueContainer());
	byRef->Insert(item, ibValue(ibString(ibT("the item"))));
	byRef->Insert(rate, ibValue(20));
	for (const ibValue& holder : { ibValue(list), ibValue(record), ibValue(byRef) }) {
		const ibString xml = ibValueSystemFunction::XMLString(holder);
		const ibValue viaXml = ibValueSystemFunction::XMLValue(xml);
		EXPECT_EQ(viaXml.GetClassType(), holder.GetClassType());
		EXPECT_EQ(ibValueSystemFunction::XMLString(viaXml), xml);   // the references inside by their type and their guid

		const ibString json = ibValueSystemFunction::JSONString(holder);
		const ibValue viaJson = ibValueSystemFunction::JSONValue(json);
		EXPECT_EQ(ibValueSystemFunction::XMLString(viaJson), xml);

		const ibValue copy = holder.CloneValue();
		EXPECT_EQ(ibValueSystemFunction::XMLString(copy), xml);
	}
	// A container keyed by references, past the size where it builds its index: each value is found again by the key
	// as read back - a reference that hashes and compares as the one written.
	ibValuePtr<ibValueContainer> indexed(new ibValueContainer());
	std::vector<ibValue> keys;
	for (int at = 0; at < 12; ++at) {
		ibValuePtr<ibValueReferenceDataObject> key(ibValueReferenceDataObject::Create(cfg.get(), goods->GetMetaID(), ibGuid(ibGuid::newGuid())));
		keys.push_back(key);
		indexed->Insert(key, ibValue(at));
	}
	ibValuePtr<ibValueContainer> indexedBack(ibValueSystemFunction::XMLValue(ibValueSystemFunction::XMLString(indexed)));
	ASSERT_TRUE(static_cast<bool>(indexedBack));
	for (int at = 0; at < 12; ++at) {
		ibValue found;
		ASSERT_TRUE(indexedBack->Property(keys[at], found)) << at;
		EXPECT_EQ(found.GetInteger(), at);
	}

	// The pieces themselves, read back: the reference inside is a reference of the catalog, with its guid.
	const ibValue listBack = ibValueSystemFunction::XMLValue(ibValueSystemFunction::XMLString(list));
	ibValuePtr<ibValueArray> listRead(listBack);
	ASSERT_TRUE(static_cast<bool>(listRead));
	ASSERT_EQ(listRead->Count(), 3u);
	EXPECT_TRUE(listRead->Values()[1].CompareValueEQ(item));
	EXPECT_TRUE(listRead->Values()[2].CompareValueEQ(rate));

	// A reference inside that the configuration cannot read is refused, not read as the empty one.
	ibString spoiled = ibValueSystemFunction::XMLString(list);
	ASSERT_EQ(spoiled.Replace(key.str(), ibT("not-a-guid")), 1u);
	EXPECT_NE(Refusal([&] { ibValueSystemFunction::XMLValue(spoiled); }).Find(ibT("'not-a-guid' is no guid")), wxNOT_FOUND);
}

TEST(XmlValueFunctions, WhatIsNotAValuesXmlIsRefused)
{
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	std::shared_ptr<ibMetaDataConfigurationFile> cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
	ActiveMetaDataAccess::Replace(cfg);
	struct Release {
		~Release() { ibApplicationInstance::DestroyAppDataEnv(); }
	} release;
	ASSERT_TRUE(cfg->RunDatabase());

	const ibValue number = ibValue(5);
	// Not XML at all, and XML that is not a value's node.
	EXPECT_NE(Refusal([&] { ibValueSystemFunction::XMLValue(ibString(ibT("5"))); }), ibString());
	EXPECT_NE(Refusal([&] { ibValueSystemFunction::XMLValue(ibString(ibT("<a/>"))); })
		.Find(ibT("'a' does not belong in the document")), wxNOT_FOUND);
	// A field of another kind - what a text edited by hand would give - is a value that cannot read its contents.
	ibDataNode bytesAsText;
	bytesAsText.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"), (unsigned long long)ibValue::GetIDObjectFromString(ibT("BinaryData"))));
	bytesAsText.SetValue(kValueFieldData, ibString(ibT("AAEC")));
	EXPECT_NE(Refusal([&] { cfg->Deserialize(bytesAsText); }).Find(ibT("Failed to read the contents")), wxNOT_FOUND);
	ibDataNode guidAsNumber;
	guidAsNumber.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"), (unsigned long long)ibValue::GetIDObjectFromString(ibT("Guid"))));
	guidAsNumber.SetValue(kValueFieldData, ibNumber(5));
	EXPECT_NE(Refusal([&] { cfg->Deserialize(guidAsNumber); }).Find(ibT("Failed to read the contents")), wxNOT_FOUND);
	// A Guid goes as its text - a literal here, not produced by the writer under test - and a text that is no guid is
	// refused, not read as the nil guid: by the provider in XML, and by the value where a text form left it a string.
	ibValuePtr<ibValueGuid> guid(new ibValueGuid(ibGuid(ibString(ibT("0f8fad5b-d9cb-469f-a165-70867728950e")))));
	ibString broken = ibValueSystemFunction::XMLString(guid);
	ASSERT_EQ(broken.Replace(ibT("Kind=\"Guid\">0f8fad5b-d9cb-469f-a165-70867728950e<"), ibT("Kind=\"Guid\">not-a-guid<")), 1u);
	EXPECT_NE(Refusal([&] { ibValueSystemFunction::XMLValue(broken); }).Find(ibT("'not-a-guid' is no Guid")), wxNOT_FOUND);
	ibDataNode guidAsText;
	guidAsText.SetValue(kValueFieldClsid, ibString::Format(ibT("%llu"), (unsigned long long)ibValue::GetIDObjectFromString(ibT("Guid"))));
	guidAsText.SetValue(kValueFieldData, ibString(ibT("not-a-guid")));
	EXPECT_NE(Refusal([&] { cfg->Deserialize(guidAsText); }).Find(ibT("'not-a-guid' is no guid")), wxNOT_FOUND);
	// A value with no packed form - a type - is refused by the door, not written as nothing.
	EXPECT_EQ(Refusal([&] { ibValueSystemFunction::XMLString(ibValueSystemFunction::TypeOf(number)); }),
		ibString(ibT("The value 'Type' cannot be serialized")));
}
