#include <gtest/gtest.h>
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaFilterCriteriaObject.h"
#include "backend/moduleManager/globalContextManager.h"
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
