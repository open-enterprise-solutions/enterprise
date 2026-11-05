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
#include "backend/backend_type.h"   // ibBackendTypeDescriptionMemory / ibBackendMetaDescriptionMemory - a property's value as its node
#include "backend/compiler/compileCode.h"
#include "backend/metadataConfiguration.h"
#include "backend/metadataSerialization.h"   // ibDeserializeValue - a declared value, as the configuration keeps it
#include "backend/metaCollection/partial/chartOfAccounts.h"
#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"
#include "backend/metaCollection/accountingKind/metaAccountingKindObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/restructureInfo.h"
#include "backend/system/value/valueGuid.h"
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

// A code or a description said as text, as a window types it — the door makes it a value of its field's type.
ibPredefinedCellDescription Text(const ibString& text)
{
	ibPredefinedCellDescription cell;
	if (text.IsEmpty())
		return cell;
	cell.m_title = text;
	ibSerializeValue(ibValue(text), cell.m_value);
	return cell;
}

// An item that declares nothing beyond its name, its code and its description — the door's own form.
ibPredefinedItemDescription Item(const ibString& name, const ibString& code)
{
	ibPredefinedItemDescription item;
	item.m_name = name;
	item.m_code = Text(code);
	item.m_description = Text(name);
	return item;
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
	// The contour the Type is picked within, set through the property's door as the designer sets it: a string, a number.
	ibDataValue contour;
	ASSERT_TRUE(ibBackendTypeDescriptionMemory::WriteNode(contour,
		ibTypeDescription(std::vector<ibClassID>{ g_valueStringCLSID, g_valueNumberCLSID }), cfg));
	ibString why;
	ASSERT_TRUE(ibPropertyGate::SetValue(chart, chart->GetProperty(ibT("TypesOfCharacteristics")), contour, &why))
		<< why.ToStdString();
	ASSERT_TRUE(cfg->RunDatabase());

	ibString refusal;
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("MainWarehouse")));
		auto values = Node();
		values->SetValue(ibT("Type"), ibString(wxT("String")));
		params.AddField(ibT("values"), ibDataValue::Child(values));
		ASSERT_TRUE(CallTool(wxT("predefined_add"), params, result, refusal)) << refusal.ToStdString();
	}

	// …and changed afterwards, to a whole description: a number of fifteen digits, two after the point.
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("MainWarehouse")));
		auto described = Node();
		described->AddField(ibT("Types"), ibDataValue::Array({ ibDataValue::String(wxT("Number")) }));
		described->SetValue(ibT("Precision"), (std::int32_t)15);
		described->SetValue(ibT("Scale"), (std::int32_t)2);
		auto values = Node();
		values->AddField(ibT("Type"), ibDataValue::Child(described));
		params.AddField(ibT("values"), ibDataValue::Child(values));
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
	const auto& declared = loaded->GetPredefinedValueArray().front().m_row;
	const auto kept = declared.find(chart->GetDataType()->GetMetaID());
	ASSERT_NE(kept, declared.end());
	const ibValue keptValue = ibDeserializeValue(kept->second.m_value, cfg);
	ibValueTypeDescription* keptType = nullptr;
	ASSERT_TRUE(keptValue.ConvertToValue(keptType));
	ASSERT_NE(keptType, nullptr);
	EXPECT_EQ(keptType->GetTypeDesc().GetPrecision(), 15u);

	// A word that is no type is refused, naming the word.
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("MainWarehouse")));
		auto values = Node();
		values->SetValue(ibT("Type"), ibString(wxT("NoSuchType")));
		params.AddField(ibT("values"), ibDataValue::Child(values));
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
		params.SetValue(ibT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("Capital")));
		params.SetValue(ibT("code"), ibString(wxT("80")));
		auto values = Node();
		values->SetValue(ibT("AccountType"), ibString(wxT("Passive")));
		values->SetValue(ibT("OffBalance"), true);
		values->SetValue(ibT("Quantitative"), true);
		params.AddField(ibT("values"), ibDataValue::Child(values));
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
		params.SetValue(ibT("id"), (std::int32_t)id);
		params.SetValue(ibT("name"), ibString(wxT("Capital")));
		auto values = Node();
		values->AddField(field, value);
		params.AddField(ibT("values"), ibDataValue::Child(values));
		ibString why;
		EXPECT_FALSE(CallTool(id == chart->GetMetaID() ? wxT("predefined_set") : wxT("predefined_add"), params, result, why));
		return why;
	};
	EXPECT_TRUE(refused(chart->GetMetaID(), wxT("AccountType"), ibDataValue::String(wxT("Sideways"))).Contains(wxT("Sideways")));
	EXPECT_TRUE(refused(chart->GetMetaID(), wxT("Currency"), ibDataValue::Bool(true)).Contains(wxT("Currency")));
	EXPECT_TRUE(refused(catalog->GetMetaID(), wxT("AccountType"), ibDataValue::String(wxT("Active"))).Contains(wxT("Warehouses")));

	// ⚠ `false` IS DECLARED, as surely as `true`: an account said not to be off-balance keeps the cell, and the seed
	// writes it — taken for "not declared", the column would keep whatever the data holds.
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("Capital")));
		auto values = Node();
		values->SetValue(ibT("OffBalance"), false);
		params.AddField(ibT("values"), ibDataValue::Child(values));
		ASSERT_TRUE(CallTool(wxT("predefined_set"), params, result, refusal)) << refusal.ToStdString();
	}
	const ibSchemaSnapshot after = cfg->BuildSchemaSnapshot();
	const ibSchemaSeedRow* capital = SeedNamed(after, chart->GetMetaID(), wxT("Capital"));
	ASSERT_NE(capital, nullptr);
	const ibValue* notOffBalance = SeedCell(*capital, chart->GetOffBalance());
	ASSERT_NE(notOffBalance, nullptr);
	EXPECT_FALSE(notOffBalance->GetBoolean());

	// An account added without a side is born with the chart's default one — whoever adds it.
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("Cash")));
		params.SetValue(ibT("code"), ibString(wxT("50")));
		ASSERT_TRUE(CallTool(wxT("predefined_add"), params, result, refusal)) << refusal.ToStdString();
		const auto item = result.FindChild(ibT("item"));
		ASSERT_TRUE(item);
		const auto said = item->FindChild(ibT("values"));
		ASSERT_TRUE(said);
		EXPECT_EQ(said->GetValue<ibString>(ibT("AccountType")), ibString(wxT("Active")));
	}
}

// What an item declares is what the object holds: a catalog's own attribute is declared like an account's side, and
// written by the apply - but never required, so an item that does not declare it leaves it the data's.
TEST(PredefinedCells, ACatalogsOwnAttributeIsDeclaredAndWritten) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	ActiveConfiguration active;
	if (!active.m_ok)
		GTEST_SKIP() << "appData env unavailable headless";
	ibMetaDataConfigurationFile* cfg = active.m_cfg.get();

	auto* catalog = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
		cfg->CreateMetaObject(g_metaCatalogCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(catalog, nullptr);
	catalog->SetName(wxT("Warehouses"));
	ibValueMetaObject* address = cfg->CreateMetaObject(g_metaAttributeCLSID, catalog, false);
	ASSERT_NE(address, nullptr);
	address->SetName(wxT("Address"));
	ASSERT_TRUE(cfg->RunDatabase());

	ibString refusal;
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)catalog->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("Main")));
		auto values = Node();
		values->SetValue(ibT("Address"), ibString(wxT("1 Harbour")));
		params.AddField(ibT("values"), ibDataValue::Child(values));
		ASSERT_TRUE(CallTool(wxT("predefined_add"), params, result, refusal)) << refusal.ToStdString();
	}
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)catalog->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("Transit")));
		ASSERT_TRUE(CallTool(wxT("predefined_add"), params, result, refusal)) << refusal.ToStdString();
	}

	const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();
	const ibSchemaSeedRow* main = SeedNamed(snapshot, catalog->GetMetaID(), wxT("Main"));
	const ibSchemaSeedRow* transit = SeedNamed(snapshot, catalog->GetMetaID(), wxT("Transit"));
	ASSERT_NE(main, nullptr);
	ASSERT_NE(transit, nullptr);
	const auto* attribute = catalog->FindAnyAttributeObjectByFilter(address->GetMetaID());
	const ibValue* written = SeedCell(*main, attribute);
	ASSERT_NE(written, nullptr);
	EXPECT_EQ(written->GetString(), ibString(wxT("1 Harbour")));
	EXPECT_EQ(SeedCell(*transit, attribute), nullptr);
}

// ⭐ A CODE'S TYPE IS CHOSEN ON ITS OWNER (#646), through the holder that shows it (ibPropertyPredefinedType) - a string or
// a number and its length; kept in the attribute's node, which is what the file holds; an item's code a value of that
// type, and written into the seed as the type holds it, the one chosen since included.
TEST(PredefinedCells, ACodesTypeIsChosenAndItsItemsFitIt) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	ActiveConfiguration active;
	if (!active.m_ok)
		GTEST_SKIP() << "appData env unavailable headless";
	ibMetaDataConfigurationFile* cfg = active.m_cfg.get();

	auto* catalog = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
		cfg->CreateMetaObject(g_metaCatalogCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(catalog, nullptr);
	catalog->SetName(wxT("Units"));
	ASSERT_TRUE(cfg->RunDatabase());

	const auto setType = [](const ibValueMetaObjectAttributeBase* attribute, const ibString& type, std::int32_t precision,
		ibString& refusal) {
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)attribute->GetMetaID());
		params.SetValue(ibT("type"), type);
		params.SetValue(ibT("precision"), precision);
		return CallTool(wxT("metadata_set_type"), params, result, refusal);
	};

	ibString refusal;
	ASSERT_TRUE(setType(catalog->GetDataCode(), wxT("Number"), 3, refusal)) << refusal.ToStdString();
	ASSERT_EQ(catalog->GetDataCode()->GetTypeDesc().GetClsidCount(), 1u);
	EXPECT_EQ(catalog->GetDataCode()->GetTypeDesc().GetFirstClsid(), g_valueNumberCLSID);
	EXPECT_EQ(static_cast<int>(catalog->GetDataCode()->GetTypeDesc().GetPrecision()), 3);
	EXPECT_FALSE(setType(catalog->GetDataDescription(), wxT("Date"), 0, refusal)) << "a string or a number, nothing else";

	// …and read back where it was placed: the owner's Code is a type, with the attribute's id that took it. It read as
	// {"name": "Code", "id": …}, so a type could not be read, changed and sent back (2026-11-05).
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)catalog->GetMetaID());
		ASSERT_TRUE(CallTool(wxT("metadata_get"), params, result, refusal)) << refusal.ToStdString();
		bool found = false;
		if (auto properties = result.FindField(ibT("properties")); properties && properties->Kind() == ibDataKind::Array)
			for (const ibDataValue& item : properties->AsArray()) {
				const std::optional<ibDataNode> entry = item.AsChild();
				if (!entry || entry->GetValue<ibString>(ibT("name")) != catalog->GetDataCode()->GetName())
					continue;
				found = true;
				EXPECT_EQ(entry->GetValue<ibString>(ibT("kind")), ibString(wxT("type")));
				ibTypeDescription shown;
				ASSERT_TRUE(entry->FindField(ibT("value")).has_value());
				ASSERT_TRUE(ibTypeDescriptionMemory::ReadNode(*entry->FindField(ibT("value")), shown));
				EXPECT_EQ(shown.GetFirstClsid(), g_valueNumberCLSID);
				EXPECT_EQ(static_cast<int>(shown.GetPrecision()), 3);
				EXPECT_EQ(entry->GetValue<std::int32_t>(ibT("id")), (std::int32_t)catalog->GetDataCode()->GetMetaID());
			}
		EXPECT_TRUE(found) << "metadata_get names the owner's Code";
	}

	// A precision or a length the field cannot hold is refused, not cut — 70000 was placed as 4464, 300 digits as 44.
	EXPECT_FALSE(setType(catalog->GetDataCode(), wxT("Number"), MAX_PRECISION_NUMBER + 1, refusal));
	EXPECT_EQ(static_cast<int>(catalog->GetDataCode()->GetTypeDesc().GetPrecision()), 3) << "the refused one is not placed";
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)catalog->GetDataCode()->GetMetaID());
		params.SetValue(ibT("type"), ibString(wxT("String")));
		params.SetValue(ibT("length"), (std::int32_t)(MAX_LENGTH_STRING + 1));
		EXPECT_FALSE(CallTool(wxT("metadata_set_type"), params, result, refusal));
	}
	// …and through a whole description, the other road in: it placed 5000 characters and 36 digits (2026-11-05).
	const auto setShape = [&](const ibString& type, std::int32_t length, std::int32_t precision) {
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)catalog->GetDataCode()->GetMetaID());
		ibDataNode& shape = params.Child(ibT("description"));
		shape.SetField(ibT("Types"), ibDataValue::Array(std::vector<ibDataValue>{ ibDataValue::String(type) }));
		shape.SetValue(ibT("Length"), length);
		shape.SetValue(ibT("Precision"), precision);
		shape.SetValue(ibT("Scale"), (std::int32_t)0);
		shape.SetValue(ibT("NonNegative"), false);
		shape.SetValue(ibT("DateFraction"), (std::int32_t)0);
		shape.SetValue(ibT("AllowedLength"), (std::int32_t)0);
		return CallTool(wxT("metadata_set_type"), params, result, refusal);
	};
	EXPECT_FALSE(setShape(wxT("String"), MAX_LENGTH_STRING + 1, 10));
	EXPECT_FALSE(setShape(wxT("Number"), 10, MAX_PRECISION_NUMBER + 1));
	EXPECT_EQ(catalog->GetDataCode()->GetTypeDesc().GetFirstClsid(), g_valueNumberCLSID);
	EXPECT_EQ(static_cast<int>(catalog->GetDataCode()->GetTypeDesc().GetPrecision()), 3) << "the refused ones are not placed";

	// The holder's node is what the file keeps: read back, the type comes with it; a node saved before a type could be
	// chosen keeps the one standing.
	ibProperty* const holder = catalog->GetProperty(catalog->GetDataCode()->GetName());
	ASSERT_NE(holder, nullptr);
	const ibDataValue saved = holder->GetNodeValue();
	ASSERT_TRUE(setType(catalog->GetDataCode(), wxT("String"), 0, refusal)) << refusal.ToStdString();
	ASSERT_TRUE(holder->SetNodeValue(saved));
	EXPECT_EQ(catalog->GetDataCode()->GetTypeDesc().GetFirstClsid(), g_valueNumberCLSID);
	auto untyped = std::make_shared<ibDataNode>(*saved.AsChild());
	untyped->RemoveField(ibT("Types"));
	ASSERT_TRUE(holder->SetNodeValue(ibDataValue::Child(untyped)));
	EXPECT_EQ(catalog->GetDataCode()->GetTypeDesc().GetFirstClsid(), g_valueNumberCLSID);
	// …and a node whose qualifier the field cannot hold is not read at all, whoever wrote it.
	auto tooWide = std::make_shared<ibDataNode>(*saved.AsChild());
	tooWide->SetField(ibT("Precision"), ibDataValue::Int(300));
	EXPECT_FALSE(holder->SetNodeValue(ibDataValue::Child(tooWide)));
	EXPECT_EQ(static_cast<int>(catalog->GetDataCode()->GetTypeDesc().GetPrecision()), 3);

	// A code is a value of that type whoever says it: `0042`, said over MCP, the number 42.
	const auto add = [&](const ibString& name, const ibString& code) {
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)catalog->GetMetaID());
		params.SetValue(ibT("name"), name);
		params.SetValue(ibT("code"), code);
		params.SetValue(ibT("description"), name);
		return CallTool(wxT("predefined_add"), params, result, refusal);
	};
	ASSERT_TRUE(add(wxT("Piece"), wxT("7"))) << refusal.ToStdString();
	ASSERT_TRUE(add(wxT("Box"), wxT("0042"))) << refusal.ToStdString();
	const ibPredefinedItemDescription* const box = catalog->FindPredefinedValue(ibString(wxT("Box")));
	ASSERT_NE(box, nullptr);
	EXPECT_EQ(box->m_code.m_title, ibString(wxT("42")));

	const auto seeded = [&](const ibString& name) -> ibValue {
		const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();
		const ibSchemaSeedRow* row = SeedNamed(snapshot, catalog->GetMetaID(), name);
		const ibValue* cell = row != nullptr ? SeedCell(*row, catalog->GetDataCode()) : nullptr;
		return cell != nullptr ? *cell : ibValue();
	};
	const ibValue piece = seeded(wxT("Piece"));
	EXPECT_EQ(piece.GetType(), ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(piece.GetInteger(), 7);

	// …and under a type chosen since, written as that type holds it.
	ASSERT_TRUE(setType(catalog->GetDataCode(), wxT("String"), 0, refusal)) << refusal.ToStdString();
	const ibValue boxAsString = seeded(wxT("Box"));
	EXPECT_EQ(boxAsString.GetType(), ibValueTypes::TYPE_STRING);
	EXPECT_EQ(boxAsString.GetString(), ibString(wxT("42")));
}

// A code is written as its field holds it, the cases #626 brought (#646): a string code whole, leading zeros and all; a
// number code a number, whose leading zeros are not digits it counts; a description no longer than its field — made so
// by the door as it is declared, and by the seed for one kept under a type chosen since.
TEST(PredefinedCells, AnItemsCodeIsWrittenAsItsFieldHoldsIt) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	ActiveConfiguration active;
	if (!active.m_ok)
		GTEST_SKIP() << "appData env unavailable headless";
	ibMetaDataConfigurationFile* cfg = active.m_cfg.get();

	auto* firms = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
		cfg->CreateMetaObject(g_metaCatalogCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(firms, nullptr);
	firms->SetName(wxT("Firms"));
	ibTypeDescription& code = firms->GetDataCode()->GetTypeDesc();
	code.ClearMetaType();
	code.SetString(9);
	firms->GetDataDescription()->GetTypeDesc().ClearMetaType();
	firms->GetDataDescription()->GetTypeDesc().SetString(150);
	ASSERT_TRUE(cfg->RunDatabase());
	firms->AppendPredefinedValue(Item(wxT("Main"), wxT("000000001")));

	const auto written = [&](const ibSchemaSnapshot& snapshot) -> ibValue {
		const ibSchemaSeedRow* main = SeedNamed(snapshot, firms->GetMetaID(), wxT("Main"));
		const ibValue* cell = main != nullptr ? SeedCell(*main, firms->GetDataCode()) : nullptr;
		return cell != nullptr ? *cell : ibValue();
	};
	const ibValue asString = written(cfg->BuildSchemaSnapshot());
	EXPECT_EQ(asString.GetType(), ibValueTypes::TYPE_STRING);
	EXPECT_EQ(asString.GetString(), ibString(wxT("000000001")));

	code.ClearMetaType();
	code.SetNumber(9, 0);
	const ibValue asNumber = written(cfg->BuildSchemaSnapshot());
	EXPECT_EQ(asNumber.GetType(), ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(asNumber.GetInteger(), 1);

	// The door insures what it is given by the attribute it goes into: a number code the number, a description no longer
	// than its length — and the seed writes it so.
	ibPredefinedItemDescription wordy = Item(wxT("Wordy"), wxT("0002"));
	wordy.m_description = Text(ibString(ibT('x'), 151));
	firms->AppendPredefinedValue(wordy);
	const ibPredefinedItemDescription* const kept = firms->FindPredefinedValue(ibString(wxT("Wordy")));
	ASSERT_NE(kept, nullptr);
	EXPECT_EQ(kept->m_code.m_title, ibString(wxT("2")));
	EXPECT_EQ(kept->m_description.m_title.length(), 150u);

	const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();
	const ibSchemaSeedRow* row = SeedNamed(snapshot, firms->GetMetaID(), wxT("Wordy"));
	ASSERT_NE(row, nullptr);
	const ibValue* wordyCode = SeedCell(*row, firms->GetDataCode());
	const ibValue* wordyDescription = SeedCell(*row, firms->GetDataDescription());
	ASSERT_NE(wordyCode, nullptr);
	ASSERT_NE(wordyDescription, nullptr);
	EXPECT_EQ(wordyCode->GetInteger(), 2);
	EXPECT_EQ(wordyDescription->GetString().length(), 150u);
}

// ⭐ A CODE AND A DESCRIPTION ARE NOT NARROWED (#646): no shorter, no fewer digits, a code not turned between a string and
// a number - measured against what the APPLIED configuration declared, never against rows. Two configurations of one
// shape, the way the apply reads its baseline off the saved one: the snapshot holds the live columns, so the one it was
// cannot be read off the one it is. A length or a count of digits of 0 holds anything.
TEST(PredefinedCells, ACodeAndADescriptionAreNotNarrowed) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	ActiveConfiguration active;
	if (!active.m_ok)
		GTEST_SKIP() << "appData env unavailable headless";

	ibMetaDataConfigurationFile applied;
	ibMetaDataConfigurationFile edited;
	const auto declare = [](ibMetaDataConfigurationFile& cfg) {
		auto* catalog = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
			cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetCommonMetaObject(), false));
		if (catalog != nullptr)
			catalog->SetName(wxT("Units"));
		return catalog;
	};
	const ibValueMetaObjectRecordDataHierarchyMutableRef* was = declare(applied);
	const ibValueMetaObjectRecordDataHierarchyMutableRef* now = declare(edited);
	ASSERT_NE(was, nullptr);
	ASSERT_NE(now, nullptr);
	ASSERT_EQ(was->GetMetaID(), now->GetMetaID());
	ASSERT_EQ(was->GetDataCode()->GetMetaID(), now->GetDataCode()->GetMetaID());
	ASSERT_EQ(was->GetDataDescription()->GetMetaID(), now->GetDataDescription()->GetMetaID());

	const auto text = [](const ibValueMetaObjectAttributeBase* attribute, unsigned short length) {
		attribute->GetTypeDesc().ClearMetaType();
		attribute->GetTypeDesc().SetString(length);
	};
	const auto digits = [](const ibValueMetaObjectAttributeBase* attribute, unsigned char precision) {
		attribute->GetTypeDesc().ClearMetaType();
		attribute->GetTypeDesc().SetNumber(precision, 0);
	};
	// What the check before the update says of the edited declaration against the applied one, in the ledger's words.
	const auto refusals = [&]() {
		const ibSchemaSnapshot baseline = applied.BuildSchemaSnapshot();
		const ibSchemaSnapshot target = edited.BuildSchemaSnapshot();
		std::vector<ibString> said;
		const ibSchemaTable* table = target.Find(now->GetMetaID());
		if (table == nullptr) {
			said.push_back(ibT("no table"));
			return said;
		}
		ibRestructureInfo report;
		const bool passed = table->m_beforeChange(baseline.Find(was->GetMetaID()), &report);
		for (size_t idx = 0; idx < report.Count(); idx++)
			said.push_back(report.At(idx).descr);
		EXPECT_EQ(passed, said.empty());
		return said;
	};
	const auto names = [](const std::vector<ibString>& said, const ibValueMetaObjectAttributeBase* attribute) {
		return said.size() == 1 && said.front().Contains(wxT("Units")) && said.front().Contains(attribute->GetSynonym());
	};

	text(was->GetDataCode(), 9);
	text(was->GetDataDescription(), 100);
	text(now->GetDataCode(), 9);
	text(now->GetDataDescription(), 100);
	EXPECT_TRUE(refusals().empty()) << "the same declaration narrows nothing";

	text(now->GetDataCode(), 12);
	text(now->GetDataDescription(), 0);
	EXPECT_TRUE(refusals().empty()) << "longer, and unlimited, hold what was";

	text(now->GetDataDescription(), 50);
	EXPECT_TRUE(names(refusals(), now->GetDataDescription()));

	text(now->GetDataDescription(), 100);
	digits(now->GetDataCode(), 12);
	EXPECT_TRUE(names(refusals(), now->GetDataCode())) << "a string code turned into a number";

	digits(was->GetDataCode(), 9);
	EXPECT_TRUE(refusals().empty()) << "more digits hold what was";
	digits(now->GetDataCode(), 5);
	EXPECT_TRUE(names(refusals(), now->GetDataCode()));
	text(now->GetDataCode(), 9);
	EXPECT_TRUE(names(refusals(), now->GetDataCode())) << "a number code turned into a string";

	digits(now->GetDataCode(), 9);
	text(was->GetDataDescription(), 0);
	EXPECT_TRUE(names(refusals(), now->GetDataDescription())) << "a length under an unlimited one";
}

// An account's analytics are rows of its kinds section: seed rows of the section's own table, found by the account
// and the position (the section's key), and unfolded into its kind columns, the way saving an account writes them.
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
	analytics->AppendPredefinedValue(Item(wxT("Counterparties"), wxT("1")));

	auto* chart = dynamic_cast<ibValueMetaObjectChartOfAccounts*>(
		cfg->CreateMetaObject(g_metaChartOfAccountsCLSID, cfg->GetCommonMetaObject(), false));
	ASSERT_NE(chart, nullptr);
	chart->SetName(wxT("Balance"));
	// Bound through the property's door, as the designer binds it: the kinds column takes the chart's type there.
	ibDataValue bound;
	ASSERT_TRUE(ibBackendMetaDescriptionMemory::WriteNode(bound, ibMetaDescription(analytics->GetMetaID()), cfg));
	ibString why;
	ASSERT_TRUE(ibPropertyGate::SetValue(chart, chart->GetChartOfCharacteristicTypes(), bound, &why)) << why.ToStdString();
	chart->AppendPredefinedValue(Item(wxT("Settlements"), wxT("60")));
	ASSERT_TRUE(cfg->RunDatabase());

	ibValueMetaObjectAccountDimensionKindsTable* kinds = chart->GetAccountDimensionKindsTable();
	ASSERT_NE(kinds, nullptr);

	ibString refusal;
	const auto declare = [&](const ibString& kind, ibString& why) {
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("Settlements")));
		auto line = Node();
		line->SetValue(ibT("AccountDimensionKind"), kind);
		line->SetValue(ibT("SummaryOnly"), true);
		auto lines = Node();
		lines->AddField(kinds->GetName(), ibDataValue::Array({ ibDataValue::Child(line) }));
		params.AddField(ibT("lines"), ibDataValue::Child(lines));
		return CallTool(wxT("predefined_set"), params, result, why);
	};
	ASSERT_TRUE(declare(wxT("Counterparties"), refusal)) << refusal.ToStdString();

	const ibGuid counterparties = analytics->GetPredefinedValueArray().front().m_guid;
	const ibGuid settlements = chart->FindPredefinedValue(wxT("Settlements"))->m_guid;

	const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();

	// The section's row, the account's, at position 1 — the two cells its key is.
	const ibSchemaTable* section = snapshot.Find(kinds->GetMetaID());
	ASSERT_NE(section, nullptr);
	ASSERT_EQ(section->m_seedKey.size(), 2u);
	ASSERT_EQ(section->m_seed.size(), 1u);
	const ibSchemaSeedRow& line = section->m_seed.front();
	const auto owner = line.m_values.find(section->m_seedKey.front()->GetColumnId());
	ASSERT_NE(owner, line.m_values.end());
	ibValueGuid* ownerGuid = nullptr;
	ASSERT_TRUE(owner->second.ConvertToValue(ownerGuid));
	ASSERT_NE(ownerGuid, nullptr);
	EXPECT_EQ(static_cast<ibGuid>(*ownerGuid), settlements);
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
	const auto& keptTables = loaded->FindPredefinedValue(wxT("Settlements"))->m_tables;
	const auto keptKinds = keptTables.find(kinds->GetMetaID());
	ASSERT_NE(keptKinds, keptTables.end());
	ASSERT_EQ(keptKinds->second.size(), 1u);
	EXPECT_EQ(keptKinds->second.front().count(kinds->GetAccountDimensionKind()->GetMetaID()), 1u);

	// A kind that is not a predefined item of the bound chart is refused, naming it.
	refusal.clear();
	EXPECT_FALSE(declare(wxT("Nobody"), refusal));
	EXPECT_TRUE(refusal.Contains(wxT("Nobody"))) << refusal.ToStdString();

	// ⭐ MORE ROWS THAN THE CHART TAKES (MaxAccountDimensionCount, 3 by default) REFUSE THE APPLY before a statement: the
	// accounts table's check before the update says it in the ledger, naming the account.
	{
		ibDataNode params, result;
		params.SetValue(ibT("id"), (std::int32_t)chart->GetMetaID());
		params.SetValue(ibT("name"), ibString(wxT("Settlements")));
		std::vector<ibDataValue> rows;
		for (unsigned int idx = 0; idx <= chart->GetMaxAccountDimensionCount(); idx++) {
			auto row = Node();
			row->SetValue(ibT("AccountDimensionKind"), ibString(wxT("Counterparties")));
			rows.push_back(ibDataValue::Child(row));
		}
		auto lines = Node();
		lines->AddField(kinds->GetName(), ibDataValue::Array(rows));
		params.AddField(ibT("lines"), ibDataValue::Child(lines));
		refusal.clear();
		ASSERT_TRUE(CallTool(wxT("predefined_set"), params, result, refusal)) << refusal.ToStdString();

		const ibSchemaSnapshot overfull = cfg->BuildSchemaSnapshot();
		const ibSchemaTable* accounts = overfull.Find(chart->GetMetaID());
		ASSERT_NE(accounts, nullptr);
		ibRestructureInfo report;
		EXPECT_FALSE(accounts->m_beforeChange(nullptr, &report));
		ASSERT_TRUE(report.HasErrors());
		EXPECT_TRUE(report.At(0).descr.Contains(wxT("Settlements"))) << report.At(0).descr.ToStdString();

		ASSERT_TRUE(declare(wxT("Counterparties"), refusal)) << refusal.ToStdString();   // back to the one row
	}

	// ⭐ A KIND THE ANALYTICS NAME, REMOVED FROM ITS CHART, REFUSES THE APPLY before a statement — not half way through:
	// the accounts table's event before the update starts says it in the ledger, naming the account.
	analytics->DeletePredefinedValue(counterparties);
	const ibSchemaSnapshot stale = cfg->BuildSchemaSnapshot();
	const ibSchemaTable* accounts = stale.Find(chart->GetMetaID());
	ASSERT_NE(accounts, nullptr);
	ibRestructureInfo report;
	EXPECT_FALSE(accounts->m_beforeChange(nullptr, &report));
	ASSERT_TRUE(report.HasErrors());
	EXPECT_TRUE(report.At(0).descr.Contains(wxT("Settlements"))) << report.At(0).descr.ToStdString();
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
