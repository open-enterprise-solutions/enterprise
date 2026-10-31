#include <gtest/gtest.h>

#include "core/serialize/provider/fileProvider.h"
#include "core/exception.h"
#include "core/fileSystem.h"
#include "backend/compiler/value.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/metaDiff.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace {
namespace fs = std::filesystem;

// A registry of two, both ways: type 1 is a Catalog, type 2 an Attribute; any other has no name.
ibFileProvider Provider()
{
	ibFileProvider provider;
	provider.SetTypeResolver([](ibClassID clsid) {
		return clsid == 1 ? ibString(ibT("Catalog")) : clsid == 2 ? ibString(ibT("Attribute")) : ibString();
	});
	provider.SetTypeLookup([](const ibString& name) {
		return name == ibT("Catalog") ? ibClassID(1) : name == ibT("Attribute") ? ibClassID(2) : ibClassID(0);
	});
	return provider;
}

ibString PathText(const fs::path& path)
{
	const std::u8string text = path.u8string();
	return ibString::FromUTF8(reinterpret_cast<const char*>(text.data()), text.size());
}

ibString Text(const ibDataNode& files, const ibString& path)
{
	const auto value = files.FindField(path);
	if (!value)
		return ibString();
	const ibMemoryBuffer bytes = value->AsBinary();
	return ibString::FromUTF8(static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
}

void SetText(ibDataNode& files, const ibString& path, const ibString& text)
{
	const std::string utf8 = text.ToUTF8();
	ibMemoryBuffer bytes;
	bytes.AppendData(utf8.data(), utf8.size());
	files.SetField(path, ibDataValue::Binary(bytes));
}

// The set as path -> bytes, whatever order it was laid out in.
std::map<std::string, std::string> Files(const ibDataNode& files)
{
	std::map<std::string, std::string> result;
	for (const auto& [path, value] : files.Fields()) {
		const ibMemoryBuffer bytes = value.AsBinary();
		result.emplace(path.ToUTF8(), std::string(static_cast<const char*>(bytes.GetData()), bytes.GetDataLen()));
	}
	return result;
}

class FileProvider : public ::testing::Test {
protected:
	fs::path m_folder = fs::temp_directory_path() / ("oes-files-" + ibGuid(ibGuid::newGuid()).str().ToStdString());
	ibString Directory() const { return PathText(m_folder / "configuration"); }
	virtual void TearDown() override { std::error_code error; fs::remove_all(m_folder, error); }

	std::map<std::string, std::string> Snapshot() const
	{
		std::map<std::string, std::string> result;
		for (const auto& file : fs::recursive_directory_iterator(m_folder / "configuration")) {
			if (!file.is_regular_file()) continue;
			std::ifstream input(file.path(), std::ios::binary);
			const std::u8string name = fs::relative(file.path(), m_folder / "configuration").generic_u8string();
			result.emplace(std::string(reinterpret_cast<const char*>(name.data()), name.size()),
				std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
		}
		return result;
	}
};
}

// ---- the layout, in memory --------------------------------------------------------------------------------------

TEST_F(FileProvider, WriteRead_AllKinds_ReadsBackAndWritesTheSameFiles)
{
	ibMemoryBuffer bytes;
	bytes.AppendData("\0\1\xff", 3);
	ibDataNode original(0xfedcba9876543210ULL, -42);
	original.AddField("same", ibDataValue());
	original.AddField("same", ibDataValue::Bool(true));
	original.SetValue("same", ibString("a string"));
	original.SetValue("nul", ibString::FromUTF8("a\0b", 3));
	original.SetValue("Module", ibString::FromUTF8("Unicode: \xc3\xa9 \xf0\x9f\x98\x80\r\nnext line\n"));
	original.SetValue("number", ibNumber(ibString("123456789012345678901234567890.1234567890123456789")));
	original.SetValue("date", ibDateTime(2026, 3, 29, 2, 30, 59, 123));
	original.SetValue("Picture", bytes);
	original.SetRawData(bytes);
	original.AddField("array", ibDataValue::Array({ ibDataValue::Int(-2), ibDataValue() }));
	original.Child("nested").AddField("value", ibDataValue::UInt(18446744073709551615ULL));
	original.AddChild(1, 2).SetValue("Name", ibString("Goods"));

	const ibFileProvider provider = Provider();
	ibDataNode files, loaded;
	ASSERT_TRUE(provider.Write(original, files));
	ASSERT_TRUE(provider.Read(files, loaded));

	EXPECT_EQ(loaded.GetClsid(), original.GetClsid());
	EXPECT_EQ(loaded.GetMetaId(), original.GetMetaId());
	std::vector<ibString> names;
	for (const auto& field : loaded.Fields())
		names.push_back(field.first);
	ASSERT_GE(names.size(), 3u);
	EXPECT_EQ(names[0], ibString("same"));
	EXPECT_EQ(names[1], ibString("same"));
	EXPECT_EQ(names[2], ibString("same"));
	EXPECT_EQ(loaded.GetValue<ibString>("nul"), original.GetValue<ibString>("nul"));
	EXPECT_EQ(loaded.GetValue<ibString>("Module"), original.GetValue<ibString>("Module"));
	EXPECT_EQ(loaded.GetValue<ibNumber>("number").ToString(), original.GetValue<ibNumber>("number").ToString());
	// A date is its text in the JSON, and whoever asks for a date reads it back as one — to the millisecond.
	EXPECT_EQ(loaded.GetValue<ibDateTime>("date").GetValue(), ibDateTime(2026, 3, 29, 2, 30, 59, 123).GetValue());
	const ibMemoryBuffer picture = loaded.FindField("Picture")->AsBinary();
	ASSERT_EQ(picture.GetDataLen(), 3u);
	EXPECT_EQ(std::memcmp(picture.GetData(), bytes.GetData(), 3), 0);
	ASSERT_EQ(loaded.RawData().GetDataLen(), 3u);
	EXPECT_EQ(loaded.FindChild("nested")->FindField("value")->AsUInt(), 18446744073709551615ULL);
	ASSERT_EQ(loaded.Children().size(), 1u);
	EXPECT_EQ(loaded.Children()[0].GetClsid(), ibClassID(1));
	EXPECT_EQ(loaded.Children()[0].GetMetaId(), 2);
	EXPECT_EQ(loaded.Children()[0].GetValue<ibString>("Name"), ibString("Goods"));

	// Laid out by name: a text of several lines and a binary beside the node, the child in its type's and name's folder.
	EXPECT_TRUE(files.Has("Module.txt"));
	EXPECT_TRUE(files.Has("Picture.bin"));
	EXPECT_TRUE(files.Has("Catalog/Goods/node.json"));

	// …and what was read writes the same files again.
	ibDataNode again;
	ASSERT_TRUE(provider.Write(loaded, again));
	EXPECT_EQ(Files(files), Files(again));
}

TEST_F(FileProvider, Write_FolderByTypeAndName_ThenGuid_ThenPlace)
{
	const ibGuid guid = ibGuid::newGuid();
	ibDataNode root;
	root.AddChild(1, 1).SetValue("Name", ibString("Goods"));
	root.AddChild(1, 2).SetValue("Guid", guid);
	root.AddChild(1, 3);
	root.AddChild(7, 4).SetValue("Name", ibString("Unnamed"));
	ibDataNode files;
	ASSERT_TRUE(Provider().Write(root, files));
	EXPECT_TRUE(files.Has("Catalog/Goods/node.json"));
	EXPECT_TRUE(files.Has(ibString(ibT("Catalog/")) + guid.str() + ibT("/node.json")));
	EXPECT_TRUE(files.Has("Catalog/item-2/node.json"));
	EXPECT_TRUE(files.Has("7/Unnamed/node.json"));   // a type with no name — by its number
}

TEST_F(FileProvider, Write_TypeByNameOnlyWhereItReadsBack)
{
	ibDataNode root(1, 0);
	root.AddChild(7, 1);
	ibDataNode files;
	ASSERT_TRUE(Provider().Write(root, files));
	EXPECT_TRUE(Text(files, ibT("configuration.json")).Contains(ibT("\"NodeType\": \"Catalog\"")));
	EXPECT_TRUE(Text(files, ibT("7/item-0/node.json")).Contains(ibT("\"NodeType\": 7")));
}

TEST_F(FileProvider, Write_FieldNamesApart_ByPlaceWhereRepeatedOrNoFileName)
{
	ibDataNode root;
	root.SetValue("Module", ibString("one\n"));
	root.SetValue("Module", ibString("two\n"));
	root.SetValue("a:b", ibString("three\n"));
	ibDataNode files, loaded;
	ASSERT_TRUE(Provider().Write(root, files));
	EXPECT_TRUE(files.Has("Module.txt"));
	EXPECT_TRUE(files.Has("field-1.txt"));
	EXPECT_TRUE(files.Has("field-2.txt"));
	ASSERT_TRUE(Provider().Read(files, loaded));
	std::vector<ibString> texts;
	for (const auto& field : loaded.Fields())
		texts.push_back(field.second.AsString());
	EXPECT_EQ(texts, (std::vector<ibString>{ ibString("one\n"), ibString("two\n"), ibString("three\n") }));
}

// A module's text is a field of a sub-node (ObjectModule's Module), not of the node itself: set aside all the same,
// named by the names on the way — and by the places on the way where a name cannot be a file's.
TEST_F(FileProvider, Write_TextInsideAValue_ByTheNamesOnTheWay)
{
	ibDataNode root;
	root.Child("ObjectModule").SetValue("Module", ibString("Procedure A()\nEndProcedure\n"));
	root.AddField("Forms", ibDataValue::Array({ ibDataValue::String(ibString("first\n")), ibDataValue::Int(3) }));
	root.Child("a:b").SetValue("Module", ibString("by place\n"));
	ibDataNode files, loaded;
	ASSERT_TRUE(Provider().Write(root, files));
	EXPECT_TRUE(files.Has("ObjectModule.Module.txt"));
	EXPECT_TRUE(files.Has("Forms.0.txt"));
	EXPECT_TRUE(files.Has("field-2.0.txt"));
	EXPECT_FALSE(Text(files, ibT("configuration.json")).Contains(ibT("EndProcedure")));

	ASSERT_TRUE(Provider().Read(files, loaded));
	EXPECT_EQ(loaded.FindChild("ObjectModule")->GetValue<ibString>("Module"), ibString("Procedure A()\nEndProcedure\n"));
	const auto forms = loaded.FindField("Forms");
	ASSERT_TRUE(forms && forms->Kind() == ibDataKind::Array && forms->AsArray().size() == 2u);
	EXPECT_EQ(forms->AsArray()[0].AsString(), ibString("first\n"));
	EXPECT_EQ(forms->AsArray()[1].AsInt(), 3);
	EXPECT_EQ(loaded.FindChild("a:b")->GetValue<ibString>("Module"), ibString("by place\n"));
}

TEST_F(FileProvider, Write_NamesApartOnlyByCase_Refuses)
{
	ibDataNode root;
	root.AddChild(1, 1).SetValue("Name", ibString("Goods"));
	root.AddChild(1, 2).SetValue("Name", ibString("goods"));
	ibDataNode files;
	EXPECT_THROW(Provider().Write(root, files), ibCoreException);
}

// ---- reading refuses what an export cannot be -------------------------------------------------------------------

TEST_F(FileProvider, Read_FileOutsideTheExport_Refuses)
{
	ibDataNode root;
	root.SetValue("Module", ibString("line\n"));
	root.AddChild(1, 1).SetValue("Name", ibString("Goods"));
	ibDataNode files, loaded;
	ASSERT_TRUE(Provider().Write(root, files));

	ibDataNode outside = files;
	ibString manifest = Text(outside, ibT("configuration.json"));
	manifest.Replace(ibT("\"Module.txt\""), ibT("\"../secret.txt\""));
	SetText(outside, ibT("configuration.json"), manifest);
	EXPECT_THROW(Provider().Read(outside, loaded), ibCoreException);

	ibDataNode upward = files;
	manifest = Text(upward, ibT("configuration.json"));
	manifest.Replace(ibT("\"Catalog/Goods\""), ibT("\"..\""));
	SetText(upward, ibT("configuration.json"), manifest);
	EXPECT_THROW(Provider().Read(upward, loaded), ibCoreException);
}

TEST_F(FileProvider, Read_MissingFile_RefusesWithoutChangingRoot)
{
	ibDataNode root;
	root.SetValue("Module", ibString("line\n"));
	ibDataNode files;
	ASSERT_TRUE(Provider().Write(root, files));
	files.RemoveField("Module.txt");
	ibDataNode loaded(999, 888);
	EXPECT_THROW(Provider().Read(files, loaded), ibCoreException);
	EXPECT_EQ(loaded.GetClsid(), ibClassID(999));
}

TEST_F(FileProvider, Read_FolderNamedTwice_Refuses)
{
	ibDataNode root;
	root.AddChild(1, 1).SetValue("Name", ibString("Goods"));
	ibDataNode files, loaded;
	ASSERT_TRUE(Provider().Write(root, files));
	ibString manifest = Text(files, ibT("configuration.json"));
	manifest.Replace(ibT("\"Catalog/Goods\""), ibT("\"Catalog/Goods\", \"Catalog/Goods\""));
	SetText(files, ibT("configuration.json"), manifest);
	EXPECT_THROW(Provider().Read(files, loaded), ibCoreException);
}

TEST_F(FileProvider, Read_UnknownVersion_Refuses)
{
	ibDataNode files, loaded;
	ASSERT_TRUE(Provider().Write(ibDataNode(), files));
	ibString manifest = Text(files, ibT("configuration.json"));
	ASSERT_TRUE(manifest.Contains(ibT("\"NodeVersion\": 1")));
	manifest.Replace(ibT("\"NodeVersion\": 1"), ibT("\"NodeVersion\": 2"));
	SetText(files, ibT("configuration.json"), manifest);
	EXPECT_THROW(Provider().Read(files, loaded), ibCoreException);
}

TEST_F(FileProvider, Read_TypeNamedAndKnownToNobody_Refuses)
{
	ibDataNode root;
	root.AddChild(1, 1).SetValue("Name", ibString("Goods"));
	ibDataNode files, loaded;
	ASSERT_TRUE(Provider().Write(root, files));
	ibFileProvider stranger;   // a build that knows no Catalog
	stranger.SetTypeLookup([](const ibString&) { return ibClassID(0); });
	EXPECT_THROW(stranger.Read(files, loaded), ibCoreException);
}

// ---- on a disk ---------------------------------------------------------------------------------------------------

TEST_F(FileProvider, PublishCollect_TheSameFiles)
{
	ibDataNode root;
	root.SetValue("Module", ibString("line\n"));
	root.AddChild(1, 1).SetValue("Name", ibString("Goods"));
	ibDataNode files, collected;
	ASSERT_TRUE(Provider().Write(root, files));
	ASSERT_TRUE(ibFileProvider::Publish(files, Directory()));
	EXPECT_EQ(Files(files), Snapshot());
	ASSERT_TRUE(ibFileProvider::Collect(Directory(), collected));
	EXPECT_EQ(Files(files), Files(collected));
}

TEST_F(FileProvider, Publish_UnrelatedFiles_RefusesAndPreservesThem)
{
	fs::create_directories(m_folder / "configuration");
	std::ofstream(m_folder / "configuration/keep.txt") << "keep";
	ibDataNode files;
	ASSERT_TRUE(Provider().Write(ibDataNode(), files));
	EXPECT_THROW(ibFileProvider::Publish(files, Directory()), ibCoreException);
	EXPECT_TRUE(fs::exists(m_folder / "configuration/keep.txt"));
}

TEST_F(FileProvider, Publish_UnrelatedFileInAnExport_RefusesAndPreservesIt)
{
	ibDataNode root, files;
	root.AddChild(1, 1).SetValue("Name", ibString("Goods"));
	ASSERT_TRUE(Provider().Write(root, files));
	ASSERT_TRUE(ibFileProvider::Publish(files, Directory()));
	fs::create_directories(m_folder / "configuration/.git");
	std::ofstream(m_folder / "configuration/Catalog/Goods/keep.txt") << "keep";
	const auto before = Snapshot();
	EXPECT_THROW(ibFileProvider::Publish(files, Directory()), ibCoreException);
	EXPECT_EQ(before, Snapshot());
	EXPECT_TRUE(fs::exists(m_folder / "configuration/.git"));
}

TEST_F(FileProvider, Publish_RemovedPart_Disappears)
{
	ibDataNode root, files;
	root.AddChild(1, 1).SetValue("Name", ibString("Goods"));
	ASSERT_TRUE(Provider().Write(root, files));
	ASSERT_TRUE(ibFileProvider::Publish(files, Directory()));
	root.RemoveChildren();
	ASSERT_TRUE(Provider().Write(root, files));
	ASSERT_TRUE(ibFileProvider::Publish(files, Directory()));
	EXPECT_FALSE(fs::exists(m_folder / "configuration/Catalog"));
}

TEST_F(FileProvider, Publish_NoExport_Refuses)
{
	ibDataNode files;
	files.AddField("note.txt", ibDataValue::Binary(ibMemoryBuffer()));
	EXPECT_THROW(ibFileProvider::Publish(files, Directory()), ibCoreException);
	EXPECT_FALSE(fs::exists(m_folder / "configuration"));
}

TEST_F(FileProvider, Collect_SymbolicLink_Refuses)
{
	ibDataNode root, files, collected;
	root.SetValue("Module", ibString("line\n"));
	ASSERT_TRUE(Provider().Write(root, files));
	ASSERT_TRUE(ibFileProvider::Publish(files, Directory()));
	const fs::path asset = m_folder / "configuration/Module.txt";
	fs::rename(asset, m_folder / "outside.txt");
	std::error_code error;
	fs::create_symlink(m_folder / "outside.txt", asset, error);
	if (error) GTEST_SKIP() << "This host does not allow symbolic links";
	EXPECT_THROW(ibFileProvider::Collect(Directory(), collected), ibCoreException);
}

TEST_F(FileProvider, PublishCollect_UnicodeDirectory)
{
	m_folder /= fs::u8path("Folder-\xc3\xa9-\xe6\x96\x87");
	ibDataNode root(12, 34), files, collected;
	root.AddChild(1, 78).SetValue("Name", ibString::FromUTF8("\xd0\xa2\xd0\xbe\xd0\xb2\xd0\xb0\xd1\x80\xd1\x8b"));
	ASSERT_TRUE(Provider().Write(root, files));
	ASSERT_TRUE(ibFileProvider::Publish(files, Directory()));
	ASSERT_TRUE(ibFileProvider::Collect(Directory(), collected));
	EXPECT_EQ(Files(files), Files(collected));
}

// ---- a configuration --------------------------------------------------------------------------------------------

TEST_F(FileProvider, SaveLoadFiles_MetadataIntoFreshConfiguration_IsIdempotent)
{
	ibMetaDataConfigurationFile original;
	for (const ibClassID type : {g_metaCatalogCLSID, g_metaDocumentCLSID, g_metaReportCLSID,
		g_metaInformationRegisterCLSID, g_metaEnumerationCLSID, g_metaCommonModuleCLSID,
		g_metaConstantCLSID, g_metaAccumulationRegisterCLSID, g_metaAccountingRegisterCLSID,
		g_metaChartOfCharacteristicTypesCLSID, g_metaChartOfAccountsCLSID, g_metaDataProcessorCLSID,
		g_metaRoleCLSID, g_metaCommonFormCLSID, g_metaCommonTemplateCLSID, g_metaSectionCLSID})
		ASSERT_NE(original.CreateMetaObject(type, original.GetCommonMetaObject(), false), nullptr);
	auto* module = static_cast<ibValueMetaObjectCommonModule*>(original.GetAnyArrayObject(g_metaCommonModuleCLSID).front());
	module->SetModuleText("Function Answer() {\n    Return 42;\n}\n");
	ibValueMetaObject* catalog = original.GetAnyArrayObject(g_metaCatalogCLSID).front();
	for (const ibClassID type : {g_metaAttributeCLSID, g_metaFormCLSID, g_metaTemplateCLSID, g_metaTableRefCLSID})
		ASSERT_NE(original.CreateMetaObject(type, catalog, false), nullptr);
	ibValueMetaObject* report = original.GetAnyArrayObject(g_metaReportCLSID).front();
	ASSERT_NE(original.CreateMetaObject(g_metaComposerCLSID, report, false), nullptr);
	ASSERT_TRUE(original.SaveConfigToFiles(Directory()));

	// Each part in its name's folder, under its type's.
	ibDataNode exported;
	ASSERT_TRUE(original.GetCommonMetaObject()->BuildDataNode(exported, saveToFileFlag));
	for (const ibDataNode& part : exported.Children()) {
		const ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(part.GetClsid());
		ASSERT_NE(ctor, nullptr);
		const ibString folder = ctor->GetClassName() + ibT("/") + part.GetValue<ibString>("Name");
		EXPECT_TRUE(fs::exists(m_folder / "configuration" / ibPathOf(folder) / "node.json")) << folder.ToUTF8();
	}

	const auto before = Snapshot();
	ibMetaDataConfigurationFile imported;
	ASSERT_TRUE(imported.LoadConfigFromFiles(Directory()));
	for (const ibMetaDiffRecord& difference : ibMetaDiffWalker::Walk(
		original.GetCommonMetaObject(), imported.GetCommonMetaObject()))
		EXPECT_EQ(difference.m_status, ibMetaDiffStatus::Same);
	ASSERT_TRUE(imported.SaveConfigToFiles(Directory()));
	EXPECT_EQ(before, Snapshot());
	ASSERT_TRUE(imported.LoadConfigFromFiles(Directory()));
	ASSERT_TRUE(imported.SaveConfigToFiles(Directory()));
	EXPECT_EQ(before, Snapshot());
}

TEST_F(FileProvider, LoadFiles_MissingFile_PreservesExistingMetadata)
{
	ibMetaDataConfigurationFile configuration;
	ASSERT_NE(configuration.CreateMetaObject(g_metaCatalogCLSID, configuration.GetCommonMetaObject(), false), nullptr);
	ASSERT_TRUE(configuration.SaveConfigToFiles(Directory()));
	const auto before = Snapshot();

	// One part's file gone: the load refuses, and saving what is held gives the export it was.
	fs::path part;
	for (const auto& entry : fs::recursive_directory_iterator(m_folder / "configuration"))
		if (entry.is_regular_file() && entry.path().filename() == "node.json")
			part = entry.path();
	ASSERT_FALSE(part.empty());
	fs::rename(part, m_folder / "aside.json");
	EXPECT_THROW(configuration.LoadConfigFromFiles(Directory()), ibCoreException);
	fs::rename(m_folder / "aside.json", part);
	ASSERT_TRUE(configuration.SaveConfigToFiles(Directory()));
	EXPECT_EQ(before, Snapshot());
}
