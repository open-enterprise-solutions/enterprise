#include <gtest/gtest.h>
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaDocumentJournalObject.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/moduleManager/globalContextManager.h"
#include "backend/system/value/valueArray.h"
#include "backend/query/queryParser.h"
#include "backend/query/queryLowering.h"
#include "backend/query/queryable.h"
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
TEST(DocumentJournal, SelectAndTheQueryReadTheDocumentsItNames) {
	OpenJournalFile cfg;
	ibValueMetaObjectConfiguration* const root = cfg.GetCommonMetaObject();
	auto* const invoice = dynamic_cast<ibValueMetaObjectDocument*>(
		cfg.CreateMetaObject(g_metaDocumentCLSID, root, false));
	auto* const returned = dynamic_cast<ibValueMetaObjectDocument*>(
		cfg.CreateMetaObject(g_metaDocumentCLSID, root, false));
	ASSERT_NE(invoice, nullptr);
	ASSERT_NE(returned, nullptr);
	invoice->SetName(wxT("Invoice"));
	returned->SetName(wxT("Return"));
	auto* const journal = dynamic_cast<ibValueMetaObjectDocumentJournal*>(
		cfg.CreateMetaObject(g_metaDocumentJournalCLSID, root, false));
	ASSERT_NE(journal, nullptr);
	journal->SetName(wxT("Sales"));
	journal->SetContent(wxT("Invoice\nMissing\nReturn"));
	ASSERT_TRUE(journal->OnBeforeRunMetaObject(newObjectFlag));
	ASSERT_TRUE(journal->OnAfterRunMetaObject(newObjectFlag));
	const ibSourceMetaDataScope scope(&cfg);
	ibQueryParser parser;
	const ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT Ref, Date, Number FROM DocumentJournal.Sales"));
	EXPECT_NO_THROW(ibQueryLowering::CheckNames(package, {}));
	std::vector<ibQueryLowering::OutputColumn> schema;
	ASSERT_NE(package.SingleSelect(), nullptr);
	ASSERT_NO_THROW(ibQueryLowering::DescribeOutput(*package.SingleSelect(), {}, schema));
	ASSERT_EQ(schema.size(), 3u);
	EXPECT_EQ(schema[0].m_name, wxString(wxT("Ref")));
	EXPECT_EQ(schema[1].m_name, wxString(wxT("Date")));
	EXPECT_EQ(schema[2].m_name, wxString(wxT("Number")));
	ibValueGlobalContextManager context(&cfg);
	ibValue list;
	ASSERT_TRUE(context.GetPropVal(context.FindProp(wxT("DocumentJournals")), list));
	ibValue manager;
	ASSERT_TRUE(list.GetAt(ibValue(wxT("Sales")), manager));
	ibValue selected;
	ASSERT_TRUE(manager.CallAsFunc(manager.FindMethod(wxT("Select")), selected, nullptr, 0));
	const ibValueArray* const refs = dynamic_cast<const ibValueArray*>(selected.GetRef());
	ASSERT_NE(refs, nullptr);
	EXPECT_EQ(refs->Count(), 0u);
}
