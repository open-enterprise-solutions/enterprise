#include <gtest/gtest.h>

#include "core/serialize/fileProvider.h"
#include "core/exception.h"
#include "core/fileSystem.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/metaDiff.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>
#include <map>
#include <filesystem>

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

class FileProvider : public ::testing::Test {
protected:
	fs::path m_folder = fs::temp_directory_path() / ("oes-files-" + ibGuid(ibGuid::newGuid()).str().ToStdString());
	wxString Directory() const { return wxString::FromUTF8((m_folder / "configuration").u8string()); }
	virtual void TearDown() override { std::error_code error; fs::remove_all(m_folder, error); }

	Json Manifest() const
	{
		std::ifstream input(m_folder / "configuration/configuration.json");
		return Json::parse(input);
	}
	void Manifest(const Json& value)
	{
		std::ofstream output(m_folder / "configuration/configuration.json");
		output << value.dump(2);
	}
	std::map<std::string, std::string> Snapshot() const
	{
		std::map<std::string, std::string> result;
		for (const auto& file : fs::recursive_directory_iterator(m_folder / "configuration")) {
			if (!file.is_regular_file()) continue;
			std::ifstream input(file.path(), std::ios::binary);
			result.emplace(fs::relative(file.path(), m_folder / "configuration").generic_string(),
				std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
		}
		return result;
	}
};
}

TEST_F(FileProvider, WriteRead_AllKinds_PreservesTree)
{
	ibDataNode original(0xfedcba9876543210ULL, -42);
	original.AddField("same", ibDataValue());
	original.AddField("same", ibDataValue::Bool(true));
	original.SetProp("same", wxString("a property"));
	original.SetValue("nul", wxString::FromUTF8("a\0b", 3));
	original.SetValue("text", wxString::FromUTF8("Unicode: \xc3\xa9 \xf0\x9f\x98\x80\r\nnext line\n"));
	original.SetValue("number", ibNumber(wxString("123456789012345678901234567890.1234567890123456789")));
	original.SetValue("date", ibDateTime(2026, 3, 29, 2, 30, 59, 123));
	ibMemoryBuffer bytes;
	bytes.AppendData("\0\1\xff", 3);
	original.SetValue("binary", bytes);
	original.SetRawData(bytes);
	original.AddField("array", ibDataValue::Array({ibDataValue::Int(-2), ibDataValue::Child(nullptr), ibDataValue()}));
	original.Child("nested").AddField("value", ibDataValue::UInt(18446744073709551615ULL));
	original.AddChild(1, 2).SetProp("name", wxString("structured child"));

	ibFileProvider provider(Directory());
	ASSERT_TRUE(provider.Write(original));
	ibDataNode loaded;
	ASSERT_TRUE(provider.Read(loaded));
	EXPECT_EQ(original, loaded);
	const auto files = Snapshot();
	EXPECT_NE(files.find("objects/field-3.txt"), files.end());
	ASSERT_TRUE(provider.Write(loaded));
	EXPECT_EQ(files, Snapshot());
}

TEST_F(FileProvider, Write_MetadataGuid_PathDoesNotUseNodeId)
{
	const ibGuid guid = ibGuid::newGuid();
	ibDataNode original;
	ibDataNode& child = original.AddChild(1, 23);
	child.SetValue("Guid", guid);
	ibFileProvider provider(Directory());
	ASSERT_TRUE(provider.Write(original));
	const auto path = Manifest().at("root").at("children").at(0);
	original.Children().front().SetMetaId(99);
	ASSERT_TRUE(provider.Write(original));
	EXPECT_EQ(path, Manifest().at("root").at("children").at(0));
	EXPECT_NE(path.get<std::string>().find(guid.str().ToStdString()), std::string::npos);
}

TEST_F(FileProvider, Read_ExternalReference_RefusesWithoutChangingRoot)
{
	ibFileProvider provider(Directory());
	ibDataNode original;
	original.SetValue("value", wxString("line\n"));
	ASSERT_TRUE(provider.Write(original));
	Json manifest = Manifest();
	manifest["root"]["fields"][0][1]["file"] = "../secret";
	Manifest(manifest);
	ibDataNode loaded(999, 888);
	const ibDataNode before = loaded;
	EXPECT_THROW(provider.Read(loaded), ibCoreException);
	EXPECT_EQ(before, loaded);
}

TEST_F(FileProvider, Read_MissingAsset_RefusesWithoutChangingRoot)
{
	ibFileProvider provider(Directory());
	ibDataNode original;
	original.SetValue("value", wxString("line\n"));
	ASSERT_TRUE(provider.Write(original));
	fs::remove(m_folder / "configuration/objects/field-0.txt");
	ibDataNode loaded(999, 888);
	EXPECT_THROW(provider.Read(loaded), ibCoreException);
	EXPECT_EQ(loaded.GetClsid(), 999u);
}

TEST_F(FileProvider, Read_UnknownVersion_Refuses)
{
	ibFileProvider provider(Directory());
	ASSERT_TRUE(provider.Write(ibDataNode()));
	Json manifest = Manifest(); manifest["version"] = 2; Manifest(manifest);
	ibDataNode loaded;
	EXPECT_THROW(provider.Read(loaded), ibCoreException);
}

TEST_F(FileProvider, Write_UnrelatedFiles_RefusesAndPreservesThem)
{
	fs::create_directories(m_folder / "configuration");
	std::ofstream(m_folder / "configuration/keep.txt") << "keep";
	EXPECT_THROW(ibFileProvider(Directory()).Write(ibDataNode()), ibCoreException);
	EXPECT_TRUE(fs::exists(m_folder / "configuration/keep.txt"));
}

TEST_F(FileProvider, Write_DuplicateGuid_LeavesPreviousExportIntact)
{
	ibFileProvider provider(Directory());
	ibDataNode original;
	ASSERT_TRUE(provider.Write(original));
	const auto before = Snapshot();
	const ibGuid guid = ibGuid::newGuid();
	original.AddChild(1, 1).SetValue("Guid", guid);
	original.AddChild(1, 2).SetValue("Guid", guid);
	EXPECT_THROW(provider.Write(original), ibCoreException);
	EXPECT_EQ(before, Snapshot());
}

TEST_F(FileProvider, Read_CyclicChildReference_Refuses)
{
	ibFileProvider provider(Directory());
	ibDataNode original;
	original.AddChild(1, 1);
	ASSERT_TRUE(provider.Write(original));
	const std::string name = Manifest().at("root").at("children").at(0);
	const fs::path path = m_folder / "configuration" / name;
	Json child;
	{ std::ifstream input(path); input >> child; }
	child["children"].push_back(name);
	{ std::ofstream output(path); output << child; }
	ibDataNode loaded;
	EXPECT_THROW(provider.Read(loaded), ibCoreException);
}

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
	ibDataNode exported;
	ASSERT_TRUE(original.GetCommonMetaObject()->BuildDataNode(exported, saveToFileFlag));
	ASSERT_EQ(exported.Children().size(), Manifest().at("root").at("children").size());
	for (size_t index = 0; index < exported.Children().size(); ++index) {
		const std::string name = Manifest().at("root").at("children").at(index);
		EXPECT_NE(name.find(exported.Children()[index].GetValue<wxString>("Guid").ToStdString()), std::string::npos);
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

TEST_F(FileProvider, Write_UnrelatedNestedFile_RefusesAndPreservesExport)
{
	ibFileProvider provider(Directory());
	ibDataNode node;
	node.AddChild(1, 1);
	ASSERT_TRUE(provider.Write(node));
	std::ofstream(m_folder / "configuration/objects/keep.txt") << "keep";
	const auto before = Snapshot();
	EXPECT_THROW(provider.Write(node), ibCoreException);
	EXPECT_EQ(before, Snapshot());
}

TEST_F(FileProvider, Read_SymbolicLink_Refuses)
{
	ibFileProvider provider(Directory());
	ibDataNode node;
	node.SetValue("text", wxString("line\n"));
	ASSERT_TRUE(provider.Write(node));
	const fs::path asset = m_folder / "configuration/objects/field-0.txt";
	fs::rename(asset, m_folder / "outside.txt");
	std::error_code error;
	fs::create_symlink(m_folder / "outside.txt", asset, error);
	if (error) GTEST_SKIP() << "This host does not allow symbolic links";
	ibDataNode loaded;
	EXPECT_THROW(provider.Read(loaded), ibCoreException);
}

TEST_F(FileProvider, SaveFiles_RemovedObject_RemovesStaleFile)
{
	ibFileProvider provider(Directory());
	ibDataNode node;
	node.AddChild(1, 1).SetValue("text", wxString("line\n"));
	ASSERT_TRUE(provider.Write(node));
	const std::string name = Manifest().at("root").at("children").at(0);
	node.Children().clear();
	ASSERT_TRUE(provider.Write(node));
	EXPECT_FALSE(fs::exists(m_folder / "configuration" / name));
	ibDataNode loaded;
	ASSERT_TRUE(provider.Read(loaded));
	EXPECT_EQ(node, loaded);
}

TEST_F(FileProvider, SaveLoad_MemoryAdapter_PreservesRootIdentity)
{
	ibDataBuilder builder;
	builder.Root() = ibDataNode(12345678901234567890ULL, 17);
	builder.Root().AddChild(1, 1).SetValue("value", wxString("line\n"));
	ibFileProvider provider(Directory());
	ibWriterMemory writer;
	ASSERT_TRUE(builder.Save(provider, writer));
	ibReaderMemory reader(writer.buffer());
	ibDataBuilder loaded;
	ASSERT_TRUE(loaded.Load(provider, reader));
	EXPECT_EQ(builder.Root(), loaded.Root());
	EXPECT_TRUE(reader.eof());
}

TEST_F(FileProvider, Read_NumericIdentityAndDateOverflow_Refuses)
{
	ibFileProvider provider(Directory());
	ibDataNode original;
	original.SetValue("date", ibDateTime());
	ASSERT_TRUE(provider.Write(original));
	const Json before = Manifest();
	Json changed = before;
	changed["root"]["id"] = 18446744073709551615ULL;
	Manifest(changed);
	ibDataNode loaded;
	EXPECT_THROW(provider.Read(loaded), ibCoreException);
	changed = before;
	changed["root"]["fields"][0][1]["value"] = 18446744073709551615ULL;
	Manifest(changed);
	EXPECT_THROW(provider.Read(loaded), ibCoreException);
}

TEST_F(FileProvider, Write_EmptyUnrelatedDirectory_Refuses)
{
	ibFileProvider provider(Directory());
	ASSERT_TRUE(provider.Write(ibDataNode()));
	fs::create_directories(m_folder / "configuration/.git");
	EXPECT_THROW(provider.Write(ibDataNode()), ibCoreException);
	EXPECT_TRUE(fs::exists(m_folder / "configuration/.git"));
}

TEST_F(FileProvider, LoadFiles_MissingAsset_PreservesExistingMetadata)
{
	ibMetaDataConfigurationFile configuration;
	ASSERT_NE(configuration.CreateMetaObject(g_metaCatalogCLSID, configuration.GetCommonMetaObject(), false), nullptr);
	ASSERT_TRUE(configuration.SaveConfigToFiles(Directory()));
	const auto before = Snapshot();
	Json manifest = Manifest();
	manifest["root"]["children"].push_back("objects/missing.json");
	Manifest(manifest);
	EXPECT_THROW(configuration.LoadConfigFromFiles(Directory()), ibCoreException);
	// Restore only the damaged manifest; exporting the retained metadata must
	// reproduce the original export, proving that no partial import took place.
	Manifest(Json::parse(before.at("configuration.json")));
	ASSERT_TRUE(configuration.SaveConfigToFiles(Directory()));
	EXPECT_EQ(before, Snapshot());
}

TEST_F(FileProvider, WriteRead_UnicodeDirectory_PreservesTree)
{
	m_folder /= fs::u8path("Folder-\xc3\xa9-\xe6\x96\x87");
	ibDataNode original(12, 34), loaded;
	original.AddChild(56, 78).SetValue("source", wxString("line\n"));
	ibFileProvider provider(Directory());
	ASSERT_TRUE(provider.Write(original));
	ASSERT_TRUE(provider.Read(loaded));
	EXPECT_EQ(original, loaded);
	ASSERT_TRUE(provider.Write(loaded));
}
