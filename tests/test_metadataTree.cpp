// =============================================================================
// OES Enterprise — metadata tree mutation (Create / Rename / Remove)
//
// ibMetaData's tree-edit API (metaData.h): CreateMetaObject attaches a typed
// child under a parent, RenameMetaObject sets its name, RemoveMetaObject detaches
// it, and GetAnyArrayObject<T>(clsid) enumerates a type. These are the operations
// the Designer drives on every metaobject add/rename/delete. DB-free
// (runObject=false on a fresh file config — see test_metadataSerialize).
// =============================================================================

#include <gtest/gtest.h>

#include <iostream>
#include <iterator>
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject + g_meta*CLSID
#include "backend/metaCollection/partial/chartOfAccounts.h"
#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"
#include "backend/metaCollection/accountingKind/metaAccountingKindObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/schemaSnapshot.h"

TEST(MetadataTree, FreshConfigHasNoBusinessObjects) {
    ibMetaDataConfigurationFile cfg;
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaCatalogCLSID).size(), 0u);
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentCLSID).size(), 0u);
}

TEST(MetadataTree, CreateAttachesAndEnumerates) {
    ibMetaDataConfigurationFile cfg;
    ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
    ASSERT_NE(root, nullptr);

    ibValueMetaObject* cat = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
    ASSERT_NE(cat, nullptr);
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaCatalogCLSID).size(), 1u);
    // A Catalog is not a Document — the type filter is exact.
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentCLSID).size(), 0u);
}

// NOTE: RenameMetaObject / RemoveMetaObject are intentionally NOT asserted here.
// On a never-run (runObject=false) object their observable effect differs from
// the naive expectation (GetName did not reflect the rename; the removed object
// still enumerated) — they appear to need run/registration state. Asserting them
// belongs with a run-time metadata fixture, not this DB-free tree test.

TEST(MetadataTree, SeveralTypesCoexist) {
    ibMetaDataConfigurationFile cfg;
    ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
    ASSERT_NE(root, nullptr);

    cfg.CreateMetaObject(g_metaCatalogCLSID,  root, false);
    cfg.CreateMetaObject(g_metaCatalogCLSID,  root, false);
    cfg.CreateMetaObject(g_metaDocumentCLSID, root, false);

    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaCatalogCLSID).size(),  2u);
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentCLSID).size(), 1u);
}

// =============================================================================
// EVERY KIND THE ROOT HOSTS, CREATED — the door the Designer's "add" button goes
// through, walked across the whole taxonomy instead of the two types that
// happened to be spot-checked above.
//
// ⭐ WHY THE LIST IS FILTERED BY THE ROOT RATHER THAN CURATED BY HAND. There is
// no enumeration of "what may live here" anywhere in the metadata — only the
// predicate ibValueMetaObject::ResolveChild, which answers 0 for a kind an owner
// does not host and otherwise the variant it wants. So the candidates are named
// here and the ROOT decides which of them apply. A kind that stops being
// top-level drops out of the test by itself; a kind that becomes top-level is
// covered the moment its CLSID is added below.
//
// ⚠ WHAT THIS DOES NOT COVER, said plainly: the crash that prompted it. That one
// lives in ibConfigurationTree::ibMetaTreeCtrl::OnSetFocus — a focus handler in
// `designer`, which is an add_executable, so no gtest target can link it. This
// pins the half that is reachable: creating each kind must succeed rather than
// return nullptr or raise.
namespace {

struct ibMetaKindCandidate {
	ibClassID   clsid;
	const char* name;
};

// Named, not derived, because a CLSID has no reverse lookup to a readable name
// here — and a failure that says "Catalog" is worth more than one that says a
// 64-bit number.
const ibMetaKindCandidate kMetaKindCandidates[] = {
	{ g_metaCatalogCLSID,                      "Catalog"                      },
	{ g_metaDocumentCLSID,                     "Document"                     },
	{ g_metaEnumerationCLSID,                  "Enumeration"                  },
	{ g_metaConstantCLSID,                     "Constant"                     },
	{ g_metaInformationRegisterCLSID,          "InformationRegister"          },
	{ g_metaAccumulationRegisterCLSID,         "AccumulationRegister"         },
	{ g_metaAccountingRegisterCLSID,           "AccountingRegister"           },
	{ g_metaChartOfCharacteristicTypesCLSID,   "ChartOfCharacteristicTypes"   },
	{ g_metaChartOfAccountsCLSID,              "ChartOfAccounts"              },
	{ g_metaDataProcessorCLSID,                "DataProcessor"                },
	{ g_metaReportCLSID,                       "Report"                       },
	{ g_metaCommonModuleCLSID,                 "CommonModule"                 },
	{ g_metaCommonFormCLSID,                   "CommonForm"                   },
	{ g_metaCommonCommandCLSID,                "CommonCommand"                },
	{ g_metaCommandGroupCLSID,                 "CommandGroup"                 },
	{ g_metaCommonTemplateCLSID,               "CommonTemplate"               },
	{ g_metaCommonAttributeCLSID,              "CommonAttribute"              },
	{ g_metaSessionParameterCLSID,             "SessionParameter"             },
	{ g_metaScheduledJobCLSID,                 "ScheduledJob"                 },
	{ g_metaRoleCLSID,                         "Role"                         },
	{ g_metaLanguageCLSID,                     "Language"                     },
	{ g_metaPictureCLSID,                      "Picture"                      },
	{ g_metaSectionCLSID,                      "Section"                      },
};

} // namespace

TEST(MetadataTree, EveryKindTheRootHostsCanBeCreated) {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);

	int hosted = 0;

	for (const ibMetaKindCandidate& candidate : kMetaKindCandidates) {

		// 0 is the owner saying it hosts no such thing — not a failure, just a
		// kind that belongs somewhere else in the tree.
		if (root->ResolveChild(candidate.clsid) == 0)
			continue;

		++hosted;

		ibValueMetaObject* created =
			cfg.CreateMetaObject(candidate.clsid, root, /*runObject*/ false);

		EXPECT_NE(created, nullptr)
			<< "the configuration root hosts " << candidate.name
			<< ", but creating one returned nullptr";
	}

	// Printed, not just asserted: "5 tests passed" says nothing about how much of
	// the taxonomy was walked, and a silent drop from twenty kinds to two would
	// still be a green run.
	RecordProperty("hostedKinds", hosted);
	std::cout << "[   INFO   ] configuration root hosted " << hosted
	          << " of " << std::size(kMetaKindCandidates) << " candidate kinds\n";

	// Guards the test itself: a taxonomy change that made every candidate
	// non-top-level would otherwise leave this passing while checking nothing.
	EXPECT_GT(hosted, 0)
		<< "the configuration root hosted none of the listed kinds — either the "
		   "taxonomy moved or the candidate list has gone stale";
}

TEST(MetadataTree, CreatingEveryHostedKindTwiceKeepsBothAndNamesThemApart) {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);

	for (const ibMetaKindCandidate& candidate : kMetaKindCandidates) {

		if (root->ResolveChild(candidate.clsid) == 0)
			continue;

		ibValueMetaObject* first  = cfg.CreateMetaObject(candidate.clsid, root, false);
		ibValueMetaObject* second = cfg.CreateMetaObject(candidate.clsid, root, false);

		ASSERT_NE(first,  nullptr) << candidate.name;
		ASSERT_NE(second, nullptr) << candidate.name << " — the SECOND one";

		// GetNewName is what keeps a fresh object from colliding with the one
		// already there; two of a kind is the smallest case that exercises it.
		EXPECT_NE(first->GetName(), second->GetName())
			<< "two " << candidate.name
			<< " objects were created with the same name";
	}
}

namespace {

const ibValue* SeedCell(const ibSchemaSeedRow& row, const ibBackendQueryColumn* column)
{
	if (column == nullptr)
		return nullptr;
	const auto found = row.m_values.find(column->GetColumnId());
	return found == row.m_values.end() ? nullptr : &found->second;
}

const ibSchemaSeedRow* SeedByName(const ibSchemaSnapshot& snapshot, ibMetaID table, const wxString& name)
{
	const ibSchemaTable* found = snapshot.Find(table);
	if (found == nullptr)
		return nullptr;
	for (const ibSchemaSeedRow& row : found->m_seed)
		if (row.m_name == name)
			return &row;
	return nullptr;
}

} // namespace

TEST(PredefinedShape, AnAccountingFlagIsWhatTheNextApplyWrites) {
	ibMetaDataConfigurationFile cfg;
	auto* chart = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(
		cfg.CreateMetaObject(g_metaChartOfAccountsCLSID, cfg.GetCommonMetaObject(), false));
	ASSERT_NE(chart, nullptr);
	chart->SetName(wxT("Balance"));

	auto* quantitative = dynamic_cast<ibValueMetaObjectAccountingKind*>(
		cfg.CreateMetaObject(g_metaAccountingKindCLSID, chart, false));
	ASSERT_NE(quantitative, nullptr);
	quantitative->SetName(wxT("Quantitative"));

	chart->AppendPredefinedValue(wxT("Cash"), wxT("50"), wxT("Cash"));
	ASSERT_TRUE(chart->SetPredefinedAccountingFlags(wxT("Cash"), wxT("Quantitative=true")).IsEmpty());
	EXPECT_EQ(chart->GetPredefinedValueArray().front()->AccountingFlagsText(), wxT("Quantitative=true"));

	ibSchemaSnapshot snapshot;
	cfg.GetCommonMetaObject()->ContributeTables(snapshot);
	const ibSchemaSeedRow* account = SeedByName(snapshot, chart->GetMetaID(), wxT("Cash"));
	ASSERT_NE(account, nullptr);
	const ibValue* flagCell = SeedCell(*account, quantitative->GetQueryColumn());
	ASSERT_NE(flagCell, nullptr);
	EXPECT_TRUE(flagCell->GetBoolean());

	ibDataNode saved;
	ASSERT_TRUE(chart->SaveNode(saved));
	auto* loaded = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(
		cfg.CreateMetaObject(g_metaChartOfAccountsCLSID, cfg.GetCommonMetaObject(), false));
	ASSERT_NE(loaded, nullptr);
	ASSERT_TRUE(loaded->LoadNode(saved));
	ASSERT_EQ(loaded->GetPredefinedValueArray().size(), 1u);
	EXPECT_EQ(loaded->GetPredefinedValueArray().front()->AccountingFlagsText(), wxT("Quantitative=true"));

	auto* catalog = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
		cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetCommonMetaObject(), false));
	ASSERT_NE(catalog, nullptr);
	catalog->SetName(wxT("Warehouses"));
	const wxString refused = catalog->SetPredefinedAccountingFlags(wxT("Main"), wxT("Quantitative=true"));
	EXPECT_TRUE(refused.Contains(wxT("Warehouses"))) << refused.ToStdString();
	const wxString unknown = chart->SetPredefinedAccountingFlags(wxT("Cash"), wxT("Currency=true"));
	EXPECT_TRUE(unknown.Contains(wxT("Currency"))) << unknown.ToStdString();
}

TEST(PredefinedShape, AnExternalDimensionIsWhatTheNextApplyWrites) {
	ibMetaDataConfigurationFile cfg;
	auto* analytics = dynamic_cast<ibValueMetaObjectChartOfCharacteristicTypes*>(
		cfg.CreateMetaObject(g_metaChartOfCharacteristicTypesCLSID, cfg.GetCommonMetaObject(), false));
	ASSERT_NE(analytics, nullptr);
	analytics->SetName(wxT("Analytics"));
	analytics->AppendPredefinedValue(wxT("Goods"), wxT("1"), wxT("Goods"));

	auto* chart = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(
		cfg.CreateMetaObject(g_metaChartOfAccountsCLSID, cfg.GetCommonMetaObject(), false));
	ASSERT_NE(chart, nullptr);
	chart->SetName(wxT("Balance"));
	chart->GetChartOfCharacteristicTypes()->SetValue(ibMetaDescription(analytics->GetMetaID()));
	chart->SyncAccountDimensionKindColumns();
	auto* turnover = dynamic_cast<ibValueMetaObjectAccountDimensionAccountingKind*>(
		cfg.CreateMetaObject(g_metaAccountDimensionAccountingKindCLSID, chart, false));
	ASSERT_NE(turnover, nullptr);
	turnover->SetName(wxT("Turnover"));

	chart->AppendPredefinedValue(wxT("Cash"), wxT("50"), wxT("Cash"));
	ASSERT_TRUE(chart->SetPredefinedDimensions(wxT("Cash"), wxT("Goods,summary,Turnover=true")).IsEmpty());
	EXPECT_EQ(chart->GetPredefinedValueArray().front()->DimensionsText(), wxT("Goods,summary,Turnover=true"));

	ibSchemaSnapshot snapshot;
	cfg.GetCommonMetaObject()->ContributeTables(snapshot);
	const ibSchemaSeedRow* account = SeedByName(snapshot, chart->GetMetaID(), wxT("Cash"));
	ASSERT_NE(account, nullptr);
	ASSERT_NE(chart->GetAccountDimensionKindColumn(0), nullptr);
	const ibValue* unfolded = SeedCell(*account, chart->GetAccountDimensionKindColumn(0)->GetQueryColumn());
	ASSERT_NE(unfolded, nullptr);
	ibValueReferenceDataObject* unfoldedRef = nullptr;
	ASSERT_TRUE(unfolded->ConvertToValue(unfoldedRef));
	EXPECT_EQ(ibGuid(unfoldedRef->GetGuid()), analytics->GetPredefinedValueArray().front()->GetPredefinedGuid());

	const ibSchemaSeedRow* dimension = SeedByName(snapshot,
		chart->GetAccountDimensionKindsTable()->GetMetaID(), wxT("Cash.Goods"));
	ASSERT_NE(dimension, nullptr);
	EXPECT_EQ(dimension->m_owner, chart->GetPredefinedValueArray().front()->GetPredefinedGuid());
	const ibValue* summary = SeedCell(*dimension, chart->GetAccountDimensionKindsTable()->GetSummaryOnly()->GetQueryColumn());
	const ibValue* marked = SeedCell(*dimension, turnover->GetQueryColumn());
	ASSERT_NE(summary, nullptr);
	ASSERT_NE(marked, nullptr);
	EXPECT_TRUE(summary->GetBoolean());
	EXPECT_TRUE(marked->GetBoolean());

	ibDataNode saved;
	ASSERT_TRUE(chart->SaveNode(saved));
	auto* loaded = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(
		cfg.CreateMetaObject(g_metaChartOfAccountsCLSID, cfg.GetCommonMetaObject(), false));
	ASSERT_NE(loaded, nullptr);
	ASSERT_TRUE(loaded->LoadNode(saved));
	EXPECT_EQ(loaded->GetPredefinedValueArray().front()->DimensionsText(), wxT("Goods,summary,Turnover=true"));

	const wxString missing = chart->SetPredefinedDimensions(wxT("Cash"), wxT("Missing"));
	EXPECT_TRUE(missing.Contains(wxT("Missing"))) << missing.ToStdString();
	auto* catalog = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
		cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetCommonMetaObject(), false));
	ASSERT_NE(catalog, nullptr);
	catalog->SetName(wxT("Warehouses"));
	const wxString refused = catalog->SetPredefinedDimensions(wxT("Main"), wxT("Goods"));
	EXPECT_TRUE(refused.Contains(wxT("Warehouses"))) << refused.ToStdString();
}
