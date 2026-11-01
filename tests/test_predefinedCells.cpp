////////////////////////////////////////////////////////////////////////////
//	Description : what a predefined item declares beyond its name (#278) — a
//	              characteristic's value Type, an account's side, off-balance
//	              flag, accounting flags and analytics rows. Said through the
//	              MCP verbs, kept by the configuration, written by the apply.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "frmserver/mcp/mcpTool.h"
#include "core/serialize/dataBuilder.h"
#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/chartOfAccounts.h"
#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"
#include "backend/metaCollection/accountingKind/metaAccountingKindObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/system/value/valueType.h"
#include "core/clsid.h"

#include "activeMetaDataAccess.h"

#include <wx/init.h>

namespace {

// A configuration run as the base's active one, and put back after — the MetadataBind fixture's steps.
struct ActiveConfiguration {
	std::shared_ptr<ibMetaDataConfigurationFile> m_cfg;
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	bool m_ok = false;

	ActiveConfiguration()
	{
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			return;
		m_cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
		ActiveMetaDataAccess::Replace(m_cfg);
		m_ok = true;
	}
	~ActiveConfiguration()
	{
		if (!m_ok)
			return;
		ibCompileCode::SetCodeStyle(m_codeStyle);
		ibApplicationInstance::DestroyAppDataEnv();
	}
};

bool CallTool(const ibString& name, const ibDataNode& params, ibDataNode& result, ibString& refusal)
{
	const ibMcpTool* tool = ibFindMcpTool(name);
	if (tool == nullptr) {
		refusal = ibT("no tool ") + name;
		return false;
	}
	return tool->Call(params, result, refusal);
}

const ibValue* SeedCell(const ibSchemaSeedRow& row, const ibValueMetaObjectAttributeBase* attribute)
{
	if (attribute == nullptr)
		return nullptr;
	const auto found = row.m_values.find(attribute->GetQueryColumn()->GetColumnId());
	return found == row.m_values.end() ? nullptr : &found->second;
}

const ibSchemaSeedRow* SeedNamed(const ibSchemaSnapshot& snapshot, ibMetaID table, const ibString& name)
{
	const ibSchemaTable* found = snapshot.Find(table);
	if (found == nullptr)
		return nullptr;
	for (const ibSchemaSeedRow& row : found->m_seed)
		if (row.m_name == name)
			return &row;
	return nullptr;
}

std::shared_ptr<ibDataNode> Node()
{
	return std::make_shared<ibDataNode>();
}

} // namespace

// A characteristic declares its value Type: by a type's name, or by the whole description.
TEST(PredefinedCells, ACharacteristicsTypeIsDeclaredSavedAndWritten) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	ActiveConfiguration active;
	if (!active.m_ok)
		GTEST_SKIP() << "appData env unavailable headless";
	ibMetaDataConfigurationFile* cfg = active.m_cfg.get();

	auto* chart = dynamic_cast<ibValueMetaObjectChartOfCharacteristicTypes*>(
		cfg->CreateMetaObject(g_metaChartOfCharacteristicTypesCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(chart, nullptr);
	chart->SetName(wxT("UserSettings"));
	ASSERT_TRUE(cfg->RunDatabase());

	ibString refusal;
	{
		ibDataNode params, result;
		params.SetValue(wxT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(wxT("name"), ibString(wxT("MainWarehouse")));
		auto values = Node();
		values->SetValue(wxT("Type"), ibString(wxT("String")));
		params.AddField(wxT("values"), ibDataValue::Child(values));
		ASSERT_TRUE(CallTool(wxT("predefined_add"), params, result, refusal)) << refusal.ToStdString();
	}

	// …and changed afterwards, to a whole description: a number of fifteen digits, two after the point.
	{
		ibDataNode params, result;
		params.SetValue(wxT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(wxT("name"), ibString(wxT("MainWarehouse")));
		auto described = Node();
		described->AddField(wxT("Types"), ibDataValue::Array({ ibDataValue::String(wxT("Number")) }));
		described->SetValue(wxT("Precision"), (std::int32_t)15);
		described->SetValue(wxT("Scale"), (std::int32_t)2);
		auto values = Node();
		values->AddField(wxT("Type"), ibDataValue::Child(described));
		params.AddField(wxT("values"), ibDataValue::Child(values));
		ASSERT_TRUE(CallTool(wxT("predefined_set"), params, result, refusal)) << refusal.ToStdString();
	}

	// The seed carries it — the cell the next apply writes.
	const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();
	const ibSchemaSeedRow* row = SeedNamed(snapshot, chart->GetMetaID(), wxT("MainWarehouse"));
	ASSERT_NE(row, nullptr);
	const ibValue* cell = SeedCell(*row, chart->GetDataType());
	ASSERT_NE(cell, nullptr);
	ibValueTypeDescription* type = nullptr;
	ASSERT_TRUE(cell->ConvertToValue(type));
	ASSERT_NE(type, nullptr);
	EXPECT_TRUE(type->GetTypeDesc().ContainType(ibValueTypes::TYPE_NUMBER));
	EXPECT_EQ(type->GetTypeDesc().GetPrecision(), 15u);
	EXPECT_EQ(type->GetTypeDesc().GetScale(), 2u);

	// The configuration keeps it: written and read back, the item still says it.
	ibDataNode saved;
	ASSERT_TRUE(chart->SaveNode(saved));
	auto* loaded = dynamic_cast<ibValueMetaObjectChartOfCharacteristicTypes*>(
		cfg->CreateMetaObject(g_metaChartOfCharacteristicTypesCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(loaded, nullptr);
	ASSERT_TRUE(loaded->LoadNode(saved));
	ASSERT_EQ(loaded->GetPredefinedValueArray().size(), 1u);
	const auto& cells = loaded->GetPredefinedValueArray().front()->GetPredefinedCells();
	const auto kept = cells.find(chart->GetDataType()->GetMetaID());
	ASSERT_NE(kept, cells.end());
	ibValueTypeDescription* keptType = nullptr;
	const ibValue keptValue = loaded->UnpackPredefinedCell(kept->second);
	ASSERT_TRUE(keptValue.ConvertToValue(keptType));
	EXPECT_EQ(keptType->GetTypeDesc().GetPrecision(), 15u);

	// A word that is no type is refused, naming the word.
	{
		ibDataNode params, result;
		params.SetValue(wxT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(wxT("name"), ibString(wxT("MainWarehouse")));
		auto values = Node();
		values->SetValue(wxT("Type"), ibString(wxT("NoSuchType")));
		params.AddField(wxT("values"), ibDataValue::Child(values));
		refusal.clear();
		EXPECT_FALSE(CallTool(wxT("predefined_set"), params, result, refusal));
		EXPECT_TRUE(refusal.Contains(wxT("NoSuchType"))) << refusal.ToStdString();
	}
}

// An account declares its side, off-balance flag and the chart's accounting flags; a catalog declares none.
TEST(PredefinedCells, AnAccountsSideAndFlagsAreDeclaredAndWritten) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	ActiveConfiguration active;
	if (!active.m_ok)
		GTEST_SKIP() << "appData env unavailable headless";
	ibMetaDataConfigurationFile* cfg = active.m_cfg.get();

	auto* chart = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(
		cfg->CreateMetaObject(g_metaChartOfAccountsCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(chart, nullptr);
	chart->SetName(wxT("Balance"));
	auto* quantitative = dynamic_cast<ibValueMetaObjectAccountingKind*>(
		cfg->CreateMetaObject(g_metaAccountingKindCLSID, chart, false));
	ASSERT_NE(quantitative, nullptr);
	quantitative->SetName(wxT("Quantitative"));
	auto* catalog = cfg->CreateMetaObject(g_metaCatalogCLSID, cfg->GetCommonMetaObject(), false);
	ASSERT_NE(catalog, nullptr);
	catalog->SetName(wxT("Warehouses"));
	ASSERT_TRUE(cfg->RunDatabase());

	ibString refusal;
	{
		ibDataNode params, result;
		params.SetValue(wxT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(wxT("name"), ibString(wxT("Capital")));
		params.SetValue(wxT("code"), ibString(wxT("80")));
		auto values = Node();
		values->SetValue(wxT("AccountType"), ibString(wxT("Passive")));
		values->SetValue(wxT("OffBalance"), true);
		values->SetValue(wxT("Quantitative"), true);
		params.AddField(wxT("values"), ibDataValue::Child(values));
		ASSERT_TRUE(CallTool(wxT("predefined_add"), params, result, refusal)) << refusal.ToStdString();

		// The answer says it back in the words it was said in.
		const auto item = result.FindChild(ibT("item"));
		ASSERT_TRUE(item);
		const auto said = item->FindChild(ibT("values"));
		ASSERT_TRUE(said);
		EXPECT_EQ(said->GetValue<ibString>(ibT("AccountType")), ibString(wxT("Passive")));
		EXPECT_TRUE(said->GetValue<bool>(ibT("OffBalance")));
		EXPECT_TRUE(said->GetValue<bool>(ibT("Quantitative")));
	}

	const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();
	const ibSchemaSeedRow* row = SeedNamed(snapshot, chart->GetMetaID(), wxT("Capital"));
	ASSERT_NE(row, nullptr);
	const ibValue* side = SeedCell(*row, chart->GetAccountType());
	const ibValue* offBalance = SeedCell(*row, chart->GetOffBalance());
	const ibValue* flag = SeedCell(*row, quantitative);
	ASSERT_NE(side, nullptr);
	ASSERT_NE(offBalance, nullptr);
	ASSERT_NE(flag, nullptr);
	EXPECT_TRUE(side->CompareValueEQ(ibValue::CreateEnumObject<ibValueEnumAccountType>(ibAccountType::ePassive)));
	EXPECT_TRUE(offBalance->GetBoolean());
	EXPECT_TRUE(flag->GetBoolean());

	// Refusals name what was wrong: a side that is none, a flag the chart does not declare, a catalog.
	const auto refused = [&](ibMetaID id, const ibString& field, const ibDataValue& value) {
		ibDataNode params, result;
		params.SetValue(wxT("id"), (std::int32_t)id);
		params.SetValue(wxT("name"), ibString(wxT("Capital")));
		auto values = Node();
		values->AddField(field, value);
		params.AddField(wxT("values"), ibDataValue::Child(values));
		ibString why;
		EXPECT_FALSE(CallTool(id == chart->GetMetaID() ? wxT("predefined_set") : wxT("predefined_add"), params, result, why));
		return why;
	};
	EXPECT_TRUE(refused(chart->GetMetaID(), wxT("AccountType"), ibDataValue::String(wxT("Sideways"))).Contains(wxT("Sideways")));
	EXPECT_TRUE(refused(chart->GetMetaID(), wxT("Currency"), ibDataValue::Bool(true)).Contains(wxT("Currency")));
	EXPECT_TRUE(refused(catalog->GetMetaID(), wxT("AccountType"), ibDataValue::String(wxT("Active"))).Contains(wxT("Warehouses")));
}

// An account's analytics are rows of its kinds section: written as lines OWNED by the account, and unfolded
// into its kind columns, the way saving an account writes them.
TEST(PredefinedCells, AnAccountsAnalyticsAreLinesItOwns) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	ActiveConfiguration active;
	if (!active.m_ok)
		GTEST_SKIP() << "appData env unavailable headless";
	ibMetaDataConfigurationFile* cfg = active.m_cfg.get();

	auto* analytics = dynamic_cast<ibValueMetaObjectChartOfCharacteristicTypes*>(
		cfg->CreateMetaObject(g_metaChartOfCharacteristicTypesCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(analytics, nullptr);
	analytics->SetName(wxT("Analytics"));
	analytics->AppendPredefinedValue(wxT("Counterparties"), wxT("1"), wxT("Counterparties"));

	auto* chart = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(
		cfg->CreateMetaObject(g_metaChartOfAccountsCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(chart, nullptr);
	chart->SetName(wxT("Balance"));
	chart->GetChartOfCharacteristicTypes()->SetValue(ibMetaDescription(analytics->GetMetaID()));
	chart->ApplyAccountDimensionKindType();
	chart->AppendPredefinedValue(wxT("Settlements"), wxT("60"), wxT("Settlements"));
	ASSERT_TRUE(cfg->RunDatabase());

	ibValueMetaObjectAccountDimensionKindsTable* kinds = chart->GetAccountDimensionKindsTable();
	ASSERT_NE(kinds, nullptr);

	ibString refusal;
	const auto declare = [&](const ibString& kind, ibString& why) {
		ibDataNode params, result;
		params.SetValue(wxT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(wxT("name"), ibString(wxT("Settlements")));
		auto line = Node();
		line->SetValue(wxT("AccountDimensionKind"), kind);
		line->SetValue(wxT("SummaryOnly"), true);
		auto lines = Node();
		lines->AddField(kinds->GetName(), ibDataValue::Array({ ibDataValue::Child(line) }));
		params.AddField(wxT("lines"), ibDataValue::Child(lines));
		return CallTool(wxT("predefined_set"), params, result, why);
	};
	ASSERT_TRUE(declare(wxT("Counterparties"), refusal)) << refusal.ToStdString();

	const ibGuid counterparties = analytics->GetPredefinedValueArray().front()->GetPredefinedGuid();
	const ibGuid settlements = chart->FindPredefinedValue(wxT("Settlements"))->GetPredefinedGuid();

	const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();

	// The section's row, owned by the account, at position 1.
	const ibSchemaTable* section = snapshot.Find(kinds->GetMetaID());
	ASSERT_NE(section, nullptr);
	ASSERT_EQ(section->m_seed.size(), 1u);
	const ibSchemaSeedRow& line = section->m_seed.front();
	EXPECT_EQ(line.m_owner, settlements);
	const ibValue* number = SeedCell(line, kinds->GetNumberLine());
	const ibValue* kindCell = SeedCell(line, kinds->GetAccountDimensionKind());
	const ibValue* summary = SeedCell(line, kinds->GetSummaryOnly());
	ASSERT_NE(number, nullptr);
	ASSERT_NE(kindCell, nullptr);
	ASSERT_NE(summary, nullptr);
	EXPECT_EQ(number->GetInteger(), 1);
	EXPECT_TRUE(summary->GetBoolean());
	ibValueReferenceDataObject* kindRef = nullptr;
	ASSERT_TRUE(kindCell->ConvertToValue(kindRef));
	EXPECT_EQ(ibGuid(kindRef->GetGuid()), counterparties);

	// …and unfolded into the account's first kind column.
	const ibSchemaSeedRow* account = SeedNamed(snapshot, chart->GetMetaID(), wxT("Settlements"));
	ASSERT_NE(account, nullptr);
	ASSERT_GT(chart->GetAccountDimensionKindColumnCount(), 0u);
	const ibValue* unfolded = SeedCell(*account, chart->GetAccountDimensionKindColumn(0));
	ASSERT_NE(unfolded, nullptr);
	ibValueReferenceDataObject* unfoldedRef = nullptr;
	ASSERT_TRUE(unfolded->ConvertToValue(unfoldedRef));
	EXPECT_EQ(ibGuid(unfoldedRef->GetGuid()), counterparties);

	// The configuration keeps the rows.
	ibDataNode saved;
	ASSERT_TRUE(chart->SaveNode(saved));
	auto* loaded = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(
		cfg->CreateMetaObject(g_metaChartOfAccountsCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(loaded, nullptr);
	ASSERT_TRUE(loaded->LoadNode(saved));
	const auto& keptLines = loaded->FindPredefinedValue(wxT("Settlements"))->GetPredefinedLines();
	const auto keptKinds = keptLines.find(kinds->GetMetaID());
	ASSERT_NE(keptKinds, keptLines.end());
	ASSERT_EQ(keptKinds->second.size(), 1u);
	EXPECT_EQ(keptKinds->second.front().m_id, line.m_id);

	// A kind that is not a predefined item of the bound chart is refused, naming it.
	refusal.clear();
	EXPECT_FALSE(declare(wxT("Nobody"), refusal));
	EXPECT_TRUE(refusal.Contains(wxT("Nobody"))) << refusal.ToStdString();
}

// A type description travels packed, as any other value does.
TEST(PredefinedCells, ATypeDescriptionPacksAndUnpacks) {
	ibTypeDescription described;
	described.SetDefaultMetaType(ibValueTypes::TYPE_STRING);
	const ibValue value(new ibValueTypeDescription(described));

	ibDataNode packed;
	ASSERT_TRUE(value.Serialize(packed));
	const ibValue back = ibValue::FromNode(packed);
	ibValueTypeDescription* read = nullptr;
	ASSERT_TRUE(back.ConvertToValue(read));
	ASSERT_NE(read, nullptr);
	EXPECT_TRUE(read->GetTypeDesc().ContainType(ibValueTypes::TYPE_STRING));
}
