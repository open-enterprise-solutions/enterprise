#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <vector>
#include <wx/image.h>
#include <wx/init.h>
#include <wx/log.h>
#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/metaFilterCriteriaObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaCollection/table/metaTableObject.h"
#include "backend/moduleManager/globalContextManager.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/system/value/valueArray.h"
struct OpenCriteriaFile : ibMetaDataConfigurationFile {
	ibMetaData::LoadGuard m_open{ this };
};
TEST(FilterCriteria, TheConfigurationAcceptsOneAndACatalogDoesNot) {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* const root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	ibValueMetaObject* const criterion = cfg.CreateMetaObject(g_metaFilterCriteriaCLSID, root, false);
	ASSERT_NE(criterion, nullptr);
	EXPECT_EQ(criterion->GetClassType(), g_metaFilterCriteriaCLSID);
	EXPECT_EQ(cfg.GetAnyArrayObject(g_metaFilterCriteriaCLSID).size(), 1u);
	ibValueMetaObject* const catalog = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
	ASSERT_NE(catalog, nullptr);
	EXPECT_EQ(cfg.CreateMetaObject(g_metaFilterCriteriaCLSID, catalog, false), nullptr);
}
TEST(FilterCriteria, FindReturnsTheReferencesOfTheNamedCriterion) {
	OpenCriteriaFile cfg;
	ibValueMetaObjectConfiguration* const root = cfg.GetCommonMetaObject();
	ibValueMetaObjectFilterCriteria* const criterion = dynamic_cast<ibValueMetaObjectFilterCriteria*>(
		cfg.CreateMetaObject(g_metaFilterCriteriaCLSID, root, true));
	ASSERT_NE(criterion, nullptr);
	criterion->SetName(wxT("SubordinateDocuments"));
	criterion->SetContent(wxT("Document.Invoice.Basis\nDocument.Return.Attribute.DocumentBasis"));
	EXPECT_EQ(criterion->GetContent(), wxString(wxT("Document.Invoice.Basis\nDocument.Return.Attribute.DocumentBasis")));
	ibValueGlobalContextManager context(&cfg);
	const long prop = context.FindProp(wxT("FilterCriteria"));
	ASSERT_NE(prop, wxNOT_FOUND);
	ibValue list;
	ASSERT_TRUE(context.GetPropVal(prop, list));
	ibValue manager;
	ASSERT_TRUE(list.GetAt(ibValue(wxT("SubordinateDocuments")), manager));
	const long method = manager.FindMethod(wxT("Find"));
	ASSERT_NE(method, wxNOT_FOUND);
	ibValue ref(1);
	ibValue* args[] = { &ref };
	ibValue result;
	ASSERT_TRUE(manager.CallAsFunc(method, result, args, 1));
	const ibValueArray* const rows = dynamic_cast<const ibValueArray*>(result.GetRef());
	ASSERT_NE(rows, nullptr);
	EXPECT_EQ(rows->Count(), 0u);
}

namespace {

wxString FieldList(const std::vector<const ibBackendQueryColumn*>& columns) {
	wxString list;
	std::set<wxString> seen;
	for (const ibBackendQueryColumn* column : columns) {
		for (const wxString& field : ColumnFieldNames(column)) {
			if (!seen.insert(field).second)
				continue;
			wxString type = wxT(" INTEGER");
			if (field.EndsWith(wxT("_RRRef")))
				type = wxT(" BLOB");
			else if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S")))
				type = wxT(" TEXT");
			else if (field.EndsWith(wxT("_N")))
				type = wxT(" NUMERIC");
			list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << type;
		}
	}
	return list;
}

} // namespace

// Document.Invoice.Items.Nomenclature is the section's column. A header attribute of the same
// name is a different field, and Find returns the document that owns the matching line.
TEST(FilterCriteria, FindThroughATabularSectionReturnsTheOwner) {
	struct EnvReset {
		bool armed = false;
		~EnvReset() {
			if (armed && ibApplicationInstance::Get() != nullptr)
				ibApplicationInstance::DestroyAppDataEnv();
		}
	} reset;

	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed";
	wxInitAllImageHandlers();
	if (wxLog::GetActiveTarget() != nullptr)
		delete wxLog::SetActiveTarget(new wxLogStderr());
	if (ibApplicationInstance::Get() == nullptr
		&& !ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable";

	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(db->Open(wxT(":memory:")));
	ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool();
	ASSERT_NE(pool, nullptr);
	pool->Init(db, 1, 0);
	reset.armed = true;

	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* const root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	ibValueMetaObject* const document = cfg.CreateMetaObject(g_metaDocumentCLSID, root, false);
	ASSERT_NE(document, nullptr);
	document->SetName(wxT("Invoice"));

	ibValueMetaObject* const header = cfg.CreateMetaObject(g_metaAttributeCLSID, document, false);
	ASSERT_NE(header, nullptr);
	header->SetName(wxT("Nomenclature"));
	auto* const headerAttr = dynamic_cast<ibValueMetaObjectAttribute*>(header);
	ASSERT_NE(headerAttr, nullptr);
	headerAttr->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);

	ibValueMetaObject* const sectionObject = cfg.CreateMetaObject(g_metaTableRefCLSID, document, false);
	ASSERT_NE(sectionObject, nullptr);
	sectionObject->SetName(wxT("Items"));
	ibValueMetaObject* const columnObject = cfg.CreateMetaObject(g_metaAttributeCLSID, sectionObject, false);
	ASSERT_NE(columnObject, nullptr);
	columnObject->SetName(wxT("Nomenclature"));
	auto* const column = dynamic_cast<ibValueMetaObjectAttribute*>(columnObject);
	ASSERT_NE(column, nullptr);
	column->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);

	auto* const criterion = dynamic_cast<ibValueMetaObjectFilterCriteria*>(
		cfg.CreateMetaObject(g_metaFilterCriteriaCLSID, root, false));
	ASSERT_NE(criterion, nullptr);
	criterion->SetName(wxT("ByItem"));
	criterion->SetContent(wxT("Document.Invoice.Items.Nomenclature"));
	ASSERT_TRUE(cfg.RunDatabase());

	auto* const section = dynamic_cast<ibValueMetaObjectTableDataRef*>(sectionObject);
	ASSERT_NE(section, nullptr);
	const auto* const tabular = dynamic_cast<const ibTabularQueryable*>(section->GetQueryable());
	ASSERT_NE(tabular, nullptr);
	const std::vector<const ibBackendQueryColumn*> columns = tabular->GetColumns();
	ASSERT_FALSE(columns.empty());
	// DDL reports zero rows changed. A failure throws or returns the error sentinel.
	ASSERT_GE(db->RunStatement(wxT("CREATE TABLE ") + section->GetPhysicalTableName()
		+ wxT(" (") + FieldList(columns) + wxT(")")), 0);

	const ibValue owner(ibValueReferenceDataObject::Create(&cfg, document->GetMetaID(), ibGuid(ibGuid::newGuid())));
	const ibValue other(ibValueReferenceDataObject::Create(&cfg, document->GetMetaID(), ibGuid(ibGuid::newGuid())));
	const ibBackendQueryColumn* const refCol = tabular->OwnerRefColumn();
	const ibBackendQueryColumn* const itemCol = column->GetQueryColumn();
	ASSERT_NE(refCol, nullptr);
	ASSERT_NE(itemCol, nullptr);

	auto insert = [&](const ibValue& ref, const wxString& item) {
		ibDataQueryBuilder query;
		query.From(section->GetQueryable());
		query.SetValue(refCol, ref);
		query.SetValue(itemCol, ibValue(item));
		return query.Insert();
	};
	ASSERT_TRUE(insert(owner, wxT("bolt")));
	ASSERT_TRUE(insert(owner, wxT("bolt")));
	ASSERT_TRUE(insert(other, wxT("nut")));

	ibValueGlobalContextManager context(&cfg);
	const long prop = context.FindProp(wxT("FilterCriteria"));
	ASSERT_NE(prop, wxNOT_FOUND);
	ibValue list;
	ASSERT_TRUE(context.GetPropVal(prop, list));
	ibValue manager;
	ASSERT_TRUE(list.GetAt(ibValue(wxT("ByItem")), manager));
	const long method = manager.FindMethod(wxT("Find"));
	ASSERT_NE(method, wxNOT_FOUND);
	ibValue wanted(wxT("bolt"));
	ibValue* args[] = { &wanted };
	ibValue result;
	ASSERT_TRUE(manager.CallAsFunc(method, result, args, 1));
	const ibValueArray* const found = dynamic_cast<const ibValueArray*>(result.GetRef());
	ASSERT_NE(found, nullptr);
	ASSERT_EQ(found->Count(), 1u);
	EXPECT_EQ(found->Values().front().CompareValueLS(owner), 0);

	if (cfg.IsConfigOpen()) {
		EXPECT_TRUE(cfg.CloseDatabase());
	}
}
