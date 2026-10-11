#include <gtest/gtest.h>
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaDocumentJournalObject.h"
#include "backend/moduleManager/globalContextManager.h"
#include "backend/system/value/valueArray.h"
struct OpenJournalFile : ibMetaDataConfigurationFile {
	ibMetaData::LoadGuard m_open{ this };
};
TEST(DocumentJournal, TheConfigurationAcceptsOneAndACatalogDoesNot) {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* const root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	ibValueMetaObject* const journal = cfg.CreateMetaObject(g_metaDocumentJournalCLSID, root, false);
	ASSERT_NE(journal, nullptr);
	EXPECT_EQ(journal->GetClassType(), g_metaDocumentJournalCLSID);
	EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentJournalCLSID).size(), 1u);
	ibValueMetaObject* const catalog = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
	ASSERT_NE(catalog, nullptr);
	EXPECT_EQ(cfg.CreateMetaObject(g_metaDocumentJournalCLSID, catalog, false), nullptr);
}
TEST(DocumentJournal, DocumentsListsTheNamesTheJournalWasGiven) {
	OpenJournalFile cfg;
	ibValueMetaObjectConfiguration* const root = cfg.GetCommonMetaObject();
	auto* const journal = dynamic_cast<ibValueMetaObjectDocumentJournal*>(
		cfg.CreateMetaObject(g_metaDocumentJournalCLSID, root, true));
	ASSERT_NE(journal, nullptr);
	journal->SetName(wxT("Sales"));
	journal->SetContent(wxT("Document.Invoice\nReturn"));
	ibValueGlobalContextManager context(&cfg);
	const long prop = context.FindProp(wxT("DocumentJournals"));
	ASSERT_NE(prop, wxNOT_FOUND);
	ibValue list;
	ASSERT_TRUE(context.GetPropVal(prop, list));
	ibValue manager;
	ASSERT_TRUE(list.GetAt(ibValue(wxT("Sales")), manager));
	const long method = manager.FindMethod(wxT("Documents"));
	ASSERT_NE(method, wxNOT_FOUND);
	ibValue result;
	ASSERT_TRUE(manager.CallAsFunc(method, result, nullptr, 0));
	const ibValueArray* const names = dynamic_cast<const ibValueArray*>(result.GetRef());
	ASSERT_NE(names, nullptr);
	ASSERT_EQ(names->Count(), 2u);
	EXPECT_EQ(names->Values()[0].GetString(), wxString(wxT("Invoice")));
	EXPECT_EQ(names->Values()[1].GetString(), wxString(wxT("Return")));
}
