/////////////////////////////////////////////////////////////////////////////
// test_basMigration — BAS / 1С XML migration to OES mutations[].
//
// Covers:
//   - Single Catalog.xml parse -> meta_create mutation w/ properties
//   - Document.xml with attributes + tabular sections
//   - Type qualifier mapping (String / Number / Boolean / Date /
//     CatalogRef / EnumRef / DocumentRef / unrecognised fallback)
//   - Legacy "Удалить..." prefix skipping
//   - Object filter glob ("Catalog.*")
//   - Unknown kind -> warning + skipped
//   - Compound type -> first non-deleted wins + warning
/////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "backend/migration/basXmlReader.hpp"
#include "backend/migration/basMapping.hpp"
#include "backend/migration/basCfReader.hpp"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/stdpaths.h>
#include <wx/stopwatch.h>
#include <wx/datetime.h>

#include <algorithm>
#include <string>

using migration::bas::ImportResult;
using migration::bas::ImportOptions;
using migration::bas::ImportSingleObjectFromText;
using migration::bas::MapKind;
using migration::bas::MapTypeQualifier;
using migration::bas::KindStatus;
using migration::bas::IsLegacyDeletedName;

namespace {

// ----- mapping unit tests -------------------------------------------------

TEST(BasMapping, KindDirectMapsSupported)
{
	auto m = MapKind(wxT("Catalog"));
	EXPECT_EQ(m.status, KindStatus::Supported);
	EXPECT_EQ(m.oesKind, wxT("Catalog"));

	m = MapKind(wxT("Document"));
	EXPECT_EQ(m.status, KindStatus::Supported);
	EXPECT_EQ(m.oesKind, wxT("Document"));

	m = MapKind(wxT("InformationRegister"));
	EXPECT_EQ(m.status, KindStatus::Supported);
	EXPECT_EQ(m.oesKind, wxT("InformationRegister"));
}

TEST(BasMapping, EnumRenamesToEnumeration)
{
	auto m = MapKind(wxT("Enum"));
	EXPECT_EQ(m.status, KindStatus::Supported);
	EXPECT_EQ(m.oesKind, wxT("Enumeration"));
}

TEST(BasMapping, SubsystemAndRoleDeferred)
{
	EXPECT_EQ(MapKind(wxT("Subsystem")).status, KindStatus::Deferred);
	EXPECT_EQ(MapKind(wxT("Role")).status,      KindStatus::Deferred);
}

TEST(BasMapping, UnknownKindFlagged)
{
	EXPECT_EQ(MapKind(wxT("WeirdKind")).status, KindStatus::Unknown);
	EXPECT_EQ(MapKind(wxT("")).status,          KindStatus::Unknown);
}

TEST(BasMapping, TypeQualifierPrimitive)
{
	wxString ref;
	EXPECT_EQ(MapTypeQualifier(wxT("xs:string"),   ref), wxT("String"));
	EXPECT_TRUE(ref.empty());
	EXPECT_EQ(MapTypeQualifier(wxT("xs:decimal"),  ref), wxT("Number"));
	EXPECT_EQ(MapTypeQualifier(wxT("xs:boolean"),  ref), wxT("Boolean"));
	EXPECT_EQ(MapTypeQualifier(wxT("xs:dateTime"), ref), wxT("Date"));
}

TEST(BasMapping, TypeQualifierReferenceTypes)
{
	wxString ref;
	EXPECT_EQ(MapTypeQualifier(wxT("cfg:CatalogRef.Контрагенты"), ref), wxT("Reference"));
	EXPECT_EQ(ref, wxT("Catalog.Контрагенты"));

	EXPECT_EQ(MapTypeQualifier(wxT("cfg:DocumentRef.ПКО"), ref), wxT("Reference"));
	EXPECT_EQ(ref, wxT("Document.ПКО"));

	EXPECT_EQ(MapTypeQualifier(wxT("cfg:EnumRef.СтавкиНДС"), ref), wxT("Reference"));
	EXPECT_EQ(ref, wxT("Enumeration.СтавкиНДС"));

	EXPECT_EQ(MapTypeQualifier(wxT("cfg:ChartOfAccountsRef.Хозрозрахунковий"), ref),
	          wxT("Reference"));
	EXPECT_EQ(ref, wxT("ChartOfAccounts.Хозрозрахунковий"));
}

TEST(BasMapping, TypeQualifierUnknownReturnsEmpty)
{
	wxString ref;
	EXPECT_EQ(MapTypeQualifier(wxT("xs:weirdType"), ref), wxString());
	EXPECT_TRUE(ref.empty());
}

TEST(BasMapping, LegacyDeletedPrefixDetection)
{
	EXPECT_TRUE(IsLegacyDeletedName(wxT("УдалитьВидыСкладов")));
	EXPECT_TRUE(IsLegacyDeletedName(wxT("УДАЛИТЬТипыСкладов")));
	EXPECT_TRUE(IsLegacyDeletedName(wxT("ВидалитиТипи")));
	EXPECT_FALSE(IsLegacyDeletedName(wxT("Контрагенты")));
	EXPECT_FALSE(IsLegacyDeletedName(wxT("")));
}

// ----- single-object parse tests ------------------------------------------

namespace fixtures {

// Minimal Catalog XML the parser can chew.
const char* kCatalogBanks =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject xmlns=\"http://v8.1c.ru/8.3/MDClasses\" "
"                xmlns:v8=\"http://v8.1c.ru/8.1/data/core\" "
"                xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "
"                xmlns:cfg=\"http://v8.1c.ru/8.1/data/enterprise/current-config\" "
"                version=\"2.16\">"
"  <Catalog uuid=\"f47b419b-4aed-4975-83d8-978c710710d1\">"
"    <Properties>"
"      <Name>Банки</Name>"
"      <Synonym>"
"        <v8:item><v8:lang>ru</v8:lang><v8:content>Банки</v8:content></v8:item>"
"        <v8:item><v8:lang>uk</v8:lang><v8:content>Банки</v8:content></v8:item>"
"      </Synonym>"
"      <Comment/>"
"      <Hierarchical>true</Hierarchical>"
"      <CodeLength>9</CodeLength>"
"      <DescriptionLength>100</DescriptionLength>"
"    </Properties>"
"    <ChildObjects>"
"      <Attribute uuid=\"c38ad01d-6049-4ff0-a15f-e8e89616971c\">"
"        <Properties>"
"          <Name>КоррСчет</Name>"
"          <Synonym>"
"            <v8:item><v8:lang>ru</v8:lang><v8:content>Корр. счет</v8:content></v8:item>"
"          </Synonym>"
"          <Type>"
"            <v8:Type>xs:string</v8:Type>"
"            <v8:StringQualifiers>"
"              <v8:Length>20</v8:Length>"
"            </v8:StringQualifiers>"
"          </Type>"
"        </Properties>"
"      </Attribute>"
"    </ChildObjects>"
"  </Catalog>"
"</MetaDataObject>";

// Document with two attributes + a tabular section.
const char* kDocumentВыработка =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject xmlns=\"http://v8.1c.ru/8.3/MDClasses\" "
"                xmlns:v8=\"http://v8.1c.ru/8.1/data/core\" "
"                xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "
"                xmlns:cfg=\"http://v8.1c.ru/8.1/data/enterprise/current-config\" "
"                version=\"2.16\">"
"  <Document>"
"    <Properties>"
"      <Name>ВыработкаНМА</Name>"
"      <Synonym>"
"        <v8:item><v8:lang>ru</v8:lang><v8:content>Выработка НМА</v8:content></v8:item>"
"      </Synonym>"
"      <NumberType>String</NumberType>"
"      <NumberLength>11</NumberLength>"
"      <Posting>Allow</Posting>"
"    </Properties>"
"    <ChildObjects>"
"      <Attribute>"
"        <Properties>"
"          <Name>Организация</Name>"
"          <Type><v8:Type>cfg:CatalogRef.Организации</v8:Type></Type>"
"        </Properties>"
"      </Attribute>"
"      <TabularSection>"
"        <Properties>"
"          <Name>НМА</Name>"
"          <Synonym>"
"            <v8:item><v8:lang>ru</v8:lang><v8:content>НМА</v8:content></v8:item>"
"          </Synonym>"
"        </Properties>"
"        <ChildObjects>"
"          <Attribute>"
"            <Properties>"
"              <Name>НематериальныйАктив</Name>"
"              <Type><v8:Type>cfg:CatalogRef.НематериальныеАктивы</v8:Type></Type>"
"            </Properties>"
"          </Attribute>"
"          <Attribute>"
"            <Properties>"
"              <Name>Количество</Name>"
"              <Type>"
"                <v8:Type>xs:decimal</v8:Type>"
"                <v8:NumberQualifiers>"
"                  <v8:Digits>15</v8:Digits>"
"                  <v8:FractionDigits>3</v8:FractionDigits>"
"                </v8:NumberQualifiers>"
"              </Type>"
"            </Properties>"
"          </Attribute>"
"        </ChildObjects>"
"      </TabularSection>"
"    </ChildObjects>"
"  </Document>"
"</MetaDataObject>";

// Enum with two values.
const char* kEnumВидыОтчетности =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject xmlns=\"http://v8.1c.ru/8.3/MDClasses\" "
"                xmlns:v8=\"http://v8.1c.ru/8.1/data/core\" "
"                xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
"  <Enum>"
"    <Properties>"
"      <Name>ВидыОтчетности</Name>"
"    </Properties>"
"    <ChildObjects>"
"      <EnumValue>"
"        <Properties>"
"          <Name>РегламентированнаяОтчетность</Name>"
"          <Synonym>"
"            <v8:item><v8:lang>ru</v8:lang><v8:content>Регламентированная отчетность</v8:content></v8:item>"
"          </Synonym>"
"        </Properties>"
"      </EnumValue>"
"      <EnumValue>"
"        <Properties>"
"          <Name>ОтчетностьПоМСФО</Name>"
"        </Properties>"
"      </EnumValue>"
"    </ChildObjects>"
"  </Enum>"
"</MetaDataObject>";

// Compound type (multiple <v8:Type> children) — first wins + warning.
const char* kCompoundTypeAttr =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject xmlns=\"http://v8.1c.ru/8.3/MDClasses\" "
"                xmlns:v8=\"http://v8.1c.ru/8.1/data/core\" "
"                xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "
"                xmlns:cfg=\"http://v8.1c.ru/8.1/data/enterprise/current-config\">"
"  <Catalog>"
"    <Properties>"
"      <Name>Compound</Name>"
"    </Properties>"
"    <ChildObjects>"
"      <Attribute>"
"        <Properties>"
"          <Name>Тип</Name>"
"          <Type>"
"            <v8:Type>cfg:CatalogRef.А</v8:Type>"
"            <v8:Type>cfg:DocumentRef.Б</v8:Type>"
"          </Type>"
"        </Properties>"
"      </Attribute>"
"    </ChildObjects>"
"  </Catalog>"
"</MetaDataObject>";

// Unknown root kind.
const char* kUnknownKind =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject>"
"  <WeirdKind>"
"    <Properties><Name>Whatever</Name></Properties>"
"  </WeirdKind>"
"</MetaDataObject>";

} // namespace fixtures

// Find a mutation by full name; returns nullptr when absent.
const nlohmann::json* FindMutation(const ImportResult& r, const std::string& fullName)
{
	for (const auto& m : r.mutations) {
		if (m.value("fullName", std::string()) == fullName) return &m;
	}
	return nullptr;
}

TEST(BasXmlReader, CatalogProducesCreateMutation)
{
	ImportResult r;
	ASSERT_TRUE(ImportSingleObjectFromText(wxString::FromUTF8(fixtures::kCatalogBanks), r));
	EXPECT_EQ(r.imported, 1);
	EXPECT_EQ(r.skippedDeferred, 0);
	EXPECT_EQ(r.skippedUnknown, 0);

	// Root catalog mutation.
	const auto* m = FindMutation(r, "Catalog.Банки");
	ASSERT_NE(m, nullptr);
	EXPECT_EQ(m->value("op", std::string()), "create");
	EXPECT_EQ(m->value("kind", std::string()), "Catalog");
	ASSERT_TRUE(m->contains("properties"));
	const auto& props = (*m)["properties"];
	EXPECT_EQ(props.value("name", std::string()), "Банки");
	EXPECT_EQ(props.value("hierarchical", false), true);
	EXPECT_EQ(props.value("codeLength", 0), 9);
	EXPECT_EQ(props.value("descriptionLength", 0), 100);
	// Synonym should be a {ru: ..., uk: ...} map.
	ASSERT_TRUE(props.contains("synonym"));
	EXPECT_EQ(props["synonym"].value("ru", std::string()), "Банки");
	EXPECT_EQ(props["synonym"].value("uk", std::string()), "Банки");

	// Attribute mutation under .Attributes.
	const auto* attr = FindMutation(r, "Catalog.Банки.Attributes.КоррСчет");
	ASSERT_NE(attr, nullptr);
	EXPECT_EQ(attr->value("kind", std::string()), "Attribute");
	const auto& aprops = (*attr)["properties"];
	EXPECT_EQ(aprops.value("type", std::string()), "String");
	EXPECT_EQ(aprops.value("length", 0), 20);
}

TEST(BasXmlReader, DocumentProducesTabularStructure)
{
	ImportResult r;
	ASSERT_TRUE(ImportSingleObjectFromText(wxString::FromUTF8(fixtures::kDocumentВыработка), r));
	EXPECT_EQ(r.imported, 1);

	const auto* doc = FindMutation(r, "Document.ВыработкаНМА");
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(doc->value("kind", std::string()), "Document");
	const auto& dprops = (*doc)["properties"];
	EXPECT_EQ(dprops.value("numberType", std::string()),   "String");
	EXPECT_EQ(dprops.value("numberLength", 0),             11);
	EXPECT_EQ(dprops.value("posting", std::string()),      "Allow");

	// Top-level attribute = Reference to Catalog.Организации.
	const auto* topAttr = FindMutation(r, "Document.ВыработкаНМА.Attributes.Организация");
	ASSERT_NE(topAttr, nullptr);
	const auto& taprops = (*topAttr)["properties"];
	EXPECT_EQ(taprops.value("type",    std::string()), "Reference");
	EXPECT_EQ(taprops.value("refType", std::string()), "Catalog.Организации");

	// Tabular section emitted.
	const auto* ts = FindMutation(r, "Document.ВыработкаНМА.TabularSections.НМА");
	ASSERT_NE(ts, nullptr);
	EXPECT_EQ(ts->value("kind", std::string()), "TabularSection");

	// Nested attribute inside tabular section.
	const auto* nested = FindMutation(r,
		"Document.ВыработкаНМА.TabularSections.НМА.Attributes.НематериальныйАктив");
	ASSERT_NE(nested, nullptr);
	const auto& nprops = (*nested)["properties"];
	EXPECT_EQ(nprops.value("type",    std::string()), "Reference");
	EXPECT_EQ(nprops.value("refType", std::string()), "Catalog.НематериальныеАктивы");

	// Decimal with qualifiers.
	const auto* qty = FindMutation(r,
		"Document.ВыработкаНМА.TabularSections.НМА.Attributes.Количество");
	ASSERT_NE(qty, nullptr);
	const auto& qprops = (*qty)["properties"];
	EXPECT_EQ(qprops.value("type",      std::string()), "Number");
	EXPECT_EQ(qprops.value("precision", 0),             15);
	EXPECT_EQ(qprops.value("scale",     0),             3);
}

TEST(BasXmlReader, EnumValuesEmitted)
{
	ImportResult r;
	ASSERT_TRUE(ImportSingleObjectFromText(
		wxString::FromUTF8(fixtures::kEnumВидыОтчетности), r));
	EXPECT_EQ(r.imported, 1);

	const auto* root = FindMutation(r, "Enumeration.ВидыОтчетности");
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(root->value("kind", std::string()), "Enumeration");

	const auto* v1 = FindMutation(r,
		"Enumeration.ВидыОтчетности.Values.РегламентированнаяОтчетность");
	ASSERT_NE(v1, nullptr);
	EXPECT_EQ(v1->value("kind", std::string()), "EnumValue");

	const auto* v2 = FindMutation(r,
		"Enumeration.ВидыОтчетности.Values.ОтчетностьПоМСФО");
	ASSERT_NE(v2, nullptr);
}

TEST(BasXmlReader, UnknownKindEmitsWarningAndSkips)
{
	ImportResult r;
	const bool ok = ImportSingleObjectFromText(
		wxString::FromUTF8(fixtures::kUnknownKind), r);
	EXPECT_FALSE(ok);
	EXPECT_EQ(r.imported,        0);
	EXPECT_EQ(r.skippedUnknown,  1);
	EXPECT_FALSE(r.warnings.empty());
}

TEST(BasXmlReader, CompoundTypeFirstWinsWithWarning)
{
	ImportResult r;
	ASSERT_TRUE(ImportSingleObjectFromText(
		wxString::FromUTF8(fixtures::kCompoundTypeAttr), r));
	const auto* attr = FindMutation(r, "Catalog.Compound.Attributes.Тип");
	ASSERT_NE(attr, nullptr);
	const auto& aprops = (*attr)["properties"];
	EXPECT_EQ(aprops.value("type",     std::string()), "Reference");
	EXPECT_EQ(aprops.value("refType",  std::string()), "Catalog.А");
	EXPECT_EQ(aprops.value("compound", false),         true);

	const bool found_warning = std::any_of(
		r.warnings.begin(), r.warnings.end(),
		[](const wxString& w) { return w.Find(wxT("compound")) != wxNOT_FOUND; });
	EXPECT_TRUE(found_warning);
}

// ----- root-manifest tests via temp dir -----------------------------------

// Write `text` to `path`. Helper for the manifest test below.
void WriteTextFile(const wxString& path, const std::string& text)
{
	wxFile f(path, wxFile::write);
	ASSERT_TRUE(f.IsOpened()) << "failed to open " << path.utf8_str().data();
	f.Write(text.data(), text.size());
	f.Close();
}

TEST(BasXmlReader, ConfigurationManifestSkipsDeletedAndAppliesFilter)
{
	// Build a minimal corpus in a temp dir:
	//   tmp/Configuration.xml lists Catalog.Контрагенты, Catalog.УдалитьСтарый,
	//     Document.ПКО, Enum.Вкусы, Subsystem.Админ
	//   tmp/Catalogs/Контрагенты.xml — supported, passes filter
	//   tmp/Catalogs/УдалитьСтарый.xml — skipped (deleted prefix)
	//   tmp/Documents/ПКО.xml — supported but filtered out
	//   tmp/Enums/Вкусы.xml — supported, passes filter when filter = ["Enum*"]
	//   Subsystem.Админ — no file, kind is deferred
	wxFileName base(wxStandardPaths::Get().GetTempDir(), wxT(""));
	base.AppendDir(wxString::Format(wxT("oes-bas-test-%lld"),
		static_cast<long long>(wxDateTime::Now().GetValue().GetLo())));
	wxFileName::Mkdir(base.GetPath(), 0700, wxPATH_MKDIR_FULL);

	// Configuration.xml
	const std::string manifest =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject xmlns=\"http://v8.1c.ru/8.3/MDClasses\" "
"                xmlns:v8=\"http://v8.1c.ru/8.1/data/core\" "
"                version=\"2.16\">"
"  <Configuration>"
"    <Properties>"
"      <Name>TestConfig</Name>"
"      <Vendor>UnitTest</Vendor>"
"      <Version>1.0</Version>"
"    </Properties>"
"    <ChildObjects>"
"      <Catalog>Контрагенты</Catalog>"
"      <Catalog>УдалитьСтарый</Catalog>"
"      <Document>ПКО</Document>"
"      <Enum>Вкусы</Enum>"
"      <Subsystem>Админ</Subsystem>"
"    </ChildObjects>"
"  </Configuration>"
"</MetaDataObject>";
	WriteTextFile(base.GetPath() + wxFILE_SEP_PATH + wxT("Configuration.xml"),
	              manifest);

	// Per-object Catalog file.
	const wxString catalogsDir = base.GetPath() + wxFILE_SEP_PATH + wxT("Catalogs");
	wxFileName::Mkdir(catalogsDir, 0700, wxPATH_MKDIR_FULL);
	const std::string kontrCatalog =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject xmlns=\"http://v8.1c.ru/8.3/MDClasses\" "
"                xmlns:v8=\"http://v8.1c.ru/8.1/data/core\">"
"  <Catalog>"
"    <Properties>"
"      <Name>Контрагенты</Name>"
"    </Properties>"
"    <ChildObjects/>"
"  </Catalog>"
"</MetaDataObject>";
	WriteTextFile(catalogsDir + wxFILE_SEP_PATH + wxT("Контрагенты.xml"),
	              kontrCatalog);

	// Per-object Document file (will be filtered out).
	const wxString docsDir = base.GetPath() + wxFILE_SEP_PATH + wxT("Documents");
	wxFileName::Mkdir(docsDir, 0700, wxPATH_MKDIR_FULL);
	const std::string pkoDoc =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject xmlns=\"http://v8.1c.ru/8.3/MDClasses\">"
"  <Document>"
"    <Properties><Name>ПКО</Name></Properties>"
"    <ChildObjects/>"
"  </Document>"
"</MetaDataObject>";
	WriteTextFile(docsDir + wxFILE_SEP_PATH + wxT("ПКО.xml"), pkoDoc);

	// Enum file.
	const wxString enumsDir = base.GetPath() + wxFILE_SEP_PATH + wxT("Enums");
	wxFileName::Mkdir(enumsDir, 0700, wxPATH_MKDIR_FULL);
	const std::string vkusyEnum =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<MetaDataObject xmlns=\"http://v8.1c.ru/8.3/MDClasses\">"
"  <Enum>"
"    <Properties><Name>Вкусы</Name></Properties>"
"    <ChildObjects/>"
"  </Enum>"
"</MetaDataObject>";
	WriteTextFile(enumsDir + wxFILE_SEP_PATH + wxT("Вкусы.xml"), vkusyEnum);

	// Filter: only Catalog.* and Enumeration.*
	ImportOptions opts;
	opts.configurationPath = base.GetPath() + wxFILE_SEP_PATH + wxT("Configuration.xml");
	opts.filter.push_back(wxT("Catalog.*"));
	opts.filter.push_back(wxT("Enumeration.*"));
	opts.skipDeleted = true;

	ImportResult r = migration::bas::ImportXmlConfiguration(opts);
	EXPECT_FALSE(r.fatal);
	EXPECT_GE(r.skippedDeleted,  1);    // УдалитьСтарый
	EXPECT_GE(r.skippedDeferred, 1);    // Subsystem
	EXPECT_GE(r.skippedFiltered, 1);    // Document.ПКО (filtered out)

	// Контрагенты imported, ПКО filtered, Вкусы imported, УдалитьСтарый
	// skipped, Subsystem deferred -> totalScanned >= 5, imported >= 2.
	EXPECT_GE(r.imported, 2);
	EXPECT_NE(FindMutation(r, "Catalog.Контрагенты"),     nullptr);
	EXPECT_NE(FindMutation(r, "Enumeration.Вкусы"),       nullptr);
	EXPECT_EQ(FindMutation(r, "Document.ПКО"),            nullptr);
	EXPECT_EQ(FindMutation(r, "Catalog.УдалитьСтарый"),   nullptr);

	// Cleanup is best-effort — temp dirs are typically purged by the OS.
}

// ----- .cf reader tests ---------------------------------------------------

TEST(BasCfReader, MissingPathReturnsError)
{
	auto r = migration::bas::ReadCfArchive(wxT(""));
	EXPECT_EQ(r.status,    migration::bas::CfStatus::FileMissing);
	EXPECT_EQ(r.errorCode, wxT("OES_E_BAS_INVALID_INPUT"));
}

TEST(BasCfReader, NonexistentFileReturnsError)
{
	auto r = migration::bas::ReadCfArchive(wxT("/nonexistent/dir/foo.cf"));
	EXPECT_EQ(r.status,    migration::bas::CfStatus::FileMissing);
}

TEST(BasCfReader, NonCfFileDetectedAsNotCf)
{
	// Write 16 bytes of "hello world..." which won't match the .cf magic.
	wxFileName tmp(wxStandardPaths::Get().GetTempDir(), wxT("not-a-cf.bin"));
	wxFile f(tmp.GetFullPath(), wxFile::write);
	ASSERT_TRUE(f.IsOpened());
	const char* junk = "hello world junk";
	f.Write(junk, 16);
	f.Close();

	auto r = migration::bas::ReadCfArchive(tmp.GetFullPath());
	EXPECT_EQ(r.status,    migration::bas::CfStatus::NotACfFile);
	EXPECT_EQ(r.errorCode, wxT("OES_E_BAS_PARSE_FAIL"));

	wxRemoveFile(tmp.GetFullPath());
}

TEST(BasCfReader, MagicHeaderReturnsUnsupportedWithGuidance)
{
	// Synthesise a tiny "fake" .cf with the magic header.
	wxFileName tmp(wxStandardPaths::Get().GetTempDir(), wxT("fake.cf"));
	wxFile f(tmp.GetFullPath(), wxFile::write);
	ASSERT_TRUE(f.IsOpened());
	const unsigned char magic[8] = { 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x00 };
	f.Write(magic, sizeof(magic));
	f.Close();

	auto r = migration::bas::ReadCfArchive(tmp.GetFullPath());
	EXPECT_EQ(r.status,    migration::bas::CfStatus::Unsupported);
	EXPECT_EQ(r.errorCode, wxT("OES_E_BAS_CF_UNSUPPORTED"));
	EXPECT_NE(r.message.Find(wxT("import_bas_xml")), wxNOT_FOUND);

	wxRemoveFile(tmp.GetFullPath());
}

} // namespace
