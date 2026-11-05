// =============================================================================
// OES Enterprise — what a FORM FIELD says about its choice, over what its attribute says (issue #259).
//
// A Textctrl and a table column carry three properties of their own: QuickChoice (Auto / Use / DontUse),
// ChoiceFoldersAndItems (Auto / Items / Folders / FoldersAndItems) and ChoiceParameters — the same table an
// attribute holds, read against the same holder. These pin the three rules the field adds:
//
//   * its own rows stand OVER the attribute's — a row of the same parameter replaces the attribute's, the
//     others are added (ibChoiceLinkResolver::Resolve);
//   * a field chosen under a row of its own is emptied when that row's source changes, unless the row says
//     Keep (ibChoiceLinkResolver::ClearLinked with `rows`) — and the form hears the change from its source's
//     write, whatever wrote it, and applies its fields' rows there, once (ibValueForm::NotifyChange);
//   * the three survive the form's own save and load (the control's WriteData / ReadData).
//
// DB-FREE AND UI-FREE: a fresh ibMetaDataConfigurationFile that is never run, a source that answers hops from
// a map (the shape test_sourceHopChain uses), and controls made by their own ctor.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>

#include <map>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/choiceLinkResolver.h"
#include "backend/srcDataObject.h"
#include "backend/propertyManager/property/propertyChoiceLink.h"
#include "core/serialize/dataBuilder.h"
#include "backend/objCtor.h"                    // ibCtorAbstractType — a control made by its own ctor

#include "frmserver/visualView/ctrl/frame.h"         // ibValueFrame — what a control saves and loads through
#include "frmserver/visualView/ctrl/typeControl.h"
#include "frmserver/visualView/ctrl/widgets.h"       // g_controlTextCtrlCLSID
#include "frmserver/visualView/ctrl/form.h"          // a form the fields stand on
#include "backend/appData.h"                         // the base a session belongs to
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"         // MintUnlisted — a harness's session
#include "backend/backend_mainFrame.h"               // the frame a source's form is found through
#include "backend/propertyManager/property/propertySource.h"   // a field's binding
#include "backend/metaCollection/partial/commonObject.h"          // a register and its record manager
#include "backend/databaseLayer/connectionPool.h"                 // the base a run configuration opens
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"   // …an in-memory one
#include <wx/log.h>                                  // wxLogStderr — a warning must not become a modal box

namespace {

// The form's object: answers a hop from a map, and writes one back into it — the holder a form field stands on.
class MockSource : public ibValue, public ibSourceDataObject {
public:
	std::map<ibSourceId, ibValue> m_fields;
	const ibMetaData* m_metaData = nullptr;
	const ibValueMetaObjectGenericData* m_metaObject = nullptr;

	MockSource() : ibValue(ibValueTypes::TYPE_VALUE) {}

	bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override {
		const auto it = m_fields.find(hop.m_id);
		if (it == m_fields.end())
			return false;
		out = it->second;
		return true;
	}
	// With the gate on, a write is the object's: a value that CHANGED tells the resolver, which empties what was
	// chosen under it and tells the form open on this source (commonObject.cpp, the object's own write gate).
	bool m_gate = false;
	bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override {
		const auto it = m_fields.find(hop.m_id);
		const bool changed = it == m_fields.end() || !(it->second == value);
		m_fields[hop.m_id] = value;
		if (m_gate && changed) {
			ibChoiceHolder holder(this);
			ibChoiceLinkResolver::ClearLinked(holder, hop.m_id);
		}
		return true;
	}

	void SourceIncrRef() override {}
	void SourceDecrRef() override {}
	bool IsEmpty() const override { return m_fields.empty(); }
	// A key of its own: the form open on the source is found by it, and a source with none tells no form.
	const ibUniqueKey m_key{ wxNewUniqueGuid };
	const ibUniqueKey& GetGuid() const override { return m_key; }
	const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return m_metaObject; }
	const ibSourceExplorer* GetSourceExplorer() const override { return nullptr; }
	const ibMetaData* GetSourceMetaData() const override { return m_metaData; }
	ibClassID GetSourceClassType() const override { return 0; }
	ibString GetSourceCaption() const override { return wxEmptyString; }
};

// A row: `parameter` (a field of what is chosen) takes its value from `source` (a field beside this one).
ibChoiceParameterRowDescription Row(const ibMetaID& parameter, const ibMetaID& source,
	ibChoiceParameterOnChange onChange = ibChoiceParameterOnChange::Clear)
{
	ibChoiceParameterRowDescription row;
	row.m_parameter = parameter;
	row.m_source.AppendSource(source);
	row.m_onChange = onChange;
	return row;
}

// A sale document with a counterparty, an organisation, a second counterparty and a contract — and the contracts
// catalog whose Counterparty and Organisation the contract's choice is narrowed by.
struct FormFieldChoiceFix : ::testing::Test {
	wxInitializer m_wxInit;
	ibMetaDataConfigurationFile cfg;

	ibValueMetaObjectAttributeBase* contractCounterparty = nullptr;   // parameters: fields of the chosen contract
	ibValueMetaObjectAttributeBase* contractOrganisation = nullptr;
	ibValueMetaObjectAttributeBase* counterparty = nullptr;           // the sale's fields: sources and the field
	ibValueMetaObjectAttributeBase* payer = nullptr;
	ibValueMetaObjectAttributeBase* organisation = nullptr;
	ibValueMetaObjectAttributeBase* contract = nullptr;

	MockSource sale;

	ibValueMetaObjectAttributeBase* Attribute(ibValueMetaObject* owner, const wxString& name) {
		auto* attribute = dynamic_cast<ibValueMetaObjectAttributeBase*>(cfg.CreateMetaObject(g_metaAttributeCLSID, owner, false));
		if (attribute != nullptr)
			attribute->SetName(name);
		return attribute;
	}

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";

		ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		ibValueMetaObject* contracts = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
		ibValueMetaObject* sales = cfg.CreateMetaObject(g_metaDocumentCLSID, root, /*runObject*/ false);
		ASSERT_NE(contracts, nullptr);
		ASSERT_NE(sales, nullptr);
		contracts->SetName(wxT("Contracts"));
		sales->SetName(wxT("Sale"));

		contractCounterparty = Attribute(contracts, wxT("Counterparty"));
		contractOrganisation = Attribute(contracts, wxT("Organisation"));
		counterparty = Attribute(sales, wxT("Counterparty"));
		payer = Attribute(sales, wxT("Payer"));
		organisation = Attribute(sales, wxT("Organisation"));
		contract = Attribute(sales, wxT("Contract"));
		ASSERT_TRUE(contractCounterparty && contractOrganisation && counterparty && payer && organisation && contract);

		sale.m_metaData = &cfg;
		sale.m_metaObject = dynamic_cast<const ibValueMetaObjectGenericData*>(sales);
		sale.m_fields[counterparty->GetMetaID()] = ibValue(ibString(wxT("Acme")));
		sale.m_fields[payer->GetMetaID()] = ibValue(ibString(wxT("Globex")));
		sale.m_fields[organisation->GetMetaID()] = ibValue(ibString(wxT("Our firm")));
		sale.m_fields[contract->GetMetaID()] = ibValue(ibString(wxT("No. 7")));
	}

	// The attribute's own table, set as the designer sets it.
	void SetAttributeRows(const ibChoiceParametersDescription& rows) {
		auto* property = dynamic_cast<ibPropertyChoiceParameters*>(contract->GetProperty(wxT("ChoiceParameters")));
		ASSERT_NE(property, nullptr);
		property->SetValue(rows);
	}
};

} // namespace

// No rows of its own: the field's answer is exactly its attribute's.
TEST_F(FormFieldChoiceFix, Resolve_NoOwnRows_IsTheAttributesAnswer)
{
	ibChoiceParametersDescription attributeRows;
	attributeRows.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));
	SetAttributeRows(attributeRows);

	const ibChoiceHolder holder(&sale);
	const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(holder, contract, ibChoiceParametersDescription());

	ASSERT_EQ(condition.m_parameters.size(), 1u);
	EXPECT_TRUE(condition.m_parameters.at(wxT("Counterparty")).GetString() == wxT("Acme"));
}

// A row of the same parameter replaces the attribute's; a row of another parameter is added to it.
TEST_F(FormFieldChoiceFix, Resolve_OwnRows_StandOverTheAttributes)
{
	ibChoiceParametersDescription attributeRows;
	attributeRows.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));
	SetAttributeRows(attributeRows);

	ibChoiceParametersDescription own;
	own.SetRow(Row(contractCounterparty->GetMetaID(), payer->GetMetaID()));
	own.SetRow(Row(contractOrganisation->GetMetaID(), organisation->GetMetaID()));

	const ibChoiceHolder holder(&sale);
	const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(holder, contract, own);

	ASSERT_EQ(condition.m_parameters.size(), 2u);
	EXPECT_TRUE(condition.m_parameters.at(wxT("Counterparty")).GetString() == wxT("Globex"));
	EXPECT_TRUE(condition.m_parameters.at(wxT("Organisation")).GetString() == wxT("Our firm"));

	// …and the attribute's own table is left as it was: the field's rows are the form's.
	EXPECT_EQ(contract->GetChoiceParameters().m_rows.size(), 1u);
}

// A field bound to no attribute still narrows by its own rows.
TEST_F(FormFieldChoiceFix, Resolve_OwnRows_WithoutAnAttribute)
{
	ibChoiceParametersDescription own;
	own.SetRow(Row(contractOrganisation->GetMetaID(), organisation->GetMetaID()));

	const ibChoiceHolder holder(&sale);
	const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(holder, nullptr, own);

	ASSERT_EQ(condition.m_parameters.size(), 1u);
	EXPECT_TRUE(condition.m_parameters.at(wxT("Organisation")).GetString() == wxT("Our firm"));
}

// The counterparty changed: the contract chosen under the field's own row is emptied.
TEST_F(FormFieldChoiceFix, ClearLinked_OwnRow_EmptiesTheField)
{
	ibChoiceParametersDescription own;
	own.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));

	ibChoiceHolder holder(&sale);
	ibChoiceLinkResolver::ClearLinked(holder, counterparty->GetMetaID(), contract, own);

	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString().IsEmpty());
}

// …unless the row says Keep, and a change of a field no row names empties nothing.
TEST_F(FormFieldChoiceFix, ClearLinked_KeepOrUnrelated_LeavesTheField)
{
	ibChoiceParametersDescription own;
	own.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID(), ibChoiceParameterOnChange::Keep));

	ibChoiceHolder holder(&sale);
	ibChoiceLinkResolver::ClearLinked(holder, counterparty->GetMetaID(), contract, own);
	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString() == wxT("No. 7"));

	ibChoiceParametersDescription cleared;
	cleared.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));
	ibChoiceLinkResolver::ClearLinked(holder, organisation->GetMetaID(), contract, cleared);
	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString() == wxT("No. 7"));
}

// A Textctrl made by its class's own ctor, the way the registry makes one, with no form around it (a form's
// Init places it on that form, and there is none here). Asked through the exported classes it derives — the
// frame saves and loads it, the factory answers its choice: the control's own class is not exported.
ibValue MakeTextctrl()
{
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(g_controlTextCtrlCLSID);
	return ctor != nullptr ? ctor->CreateObject() : ibValue();
}

struct TextctrlUnderTest {
	ibValue m_value = MakeTextctrl();
	ibValueFrame* m_frame = dynamic_cast<ibValueFrame*>(m_value.GetRef());
	ibTypeControlFactory* m_factory = dynamic_cast<ibTypeControlFactory*>(m_value.GetRef());

	bool IsOk() const { return m_frame != nullptr && m_factory != nullptr; }
	ibProperty* Property(const wxString& name) const { return m_frame->GetProperty(name); }
};

// The three properties go out with the form and come back with it.
TEST_F(FormFieldChoiceFix, TextctrlProperties_SurviveSaveAndLoad)
{
	TextctrlUnderTest written;
	ASSERT_TRUE(written.IsOk());

	auto* quickChoice = dynamic_cast<ibPropertyEnumBase*>(written.Property(wxT("QuickChoice")));
	auto* foldersAndItems = dynamic_cast<ibPropertyEnumBase*>(written.Property(wxT("ChoiceFoldersAndItems")));
	auto* parameters = dynamic_cast<ibPropertyChoiceParameters*>(written.Property(wxT("ChoiceParameters")));
	ASSERT_NE(quickChoice, nullptr);
	ASSERT_NE(foldersAndItems, nullptr);
	ASSERT_NE(parameters, nullptr);

	// Nothing said: the type's and the attribute's answers stand.
	EXPECT_EQ(written.m_factory->GetQuickChoiceMode(), ibQuickChoiceMode_Auto);
	EXPECT_EQ(written.m_factory->GetChoiceFoldersAndItems(), ibChoiceFoldersAndItems_Auto);
	EXPECT_FALSE(written.m_factory->GetChoiceParameters().IsOk());
	EXPECT_EQ(written.m_factory->GetSelectMode(), ibSelectMode::ibSelectMode_Items);

	quickChoice->SetValue(static_cast<int>(ibQuickChoiceMode_DontUse));
	foldersAndItems->SetValue(static_cast<int>(ibChoiceFoldersAndItems_FoldersAndItems));
	ibChoiceParametersDescription rows;
	rows.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID(), ibChoiceParameterOnChange::Keep));
	parameters->SetValue(rows);

	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	TextctrlUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));

	EXPECT_EQ(read.m_factory->GetQuickChoiceMode(), ibQuickChoiceMode_DontUse);
	EXPECT_EQ(read.m_factory->GetChoiceFoldersAndItems(), ibChoiceFoldersAndItems_FoldersAndItems);
	EXPECT_TRUE(read.m_factory->GetChoiceParameters() == rows);
	EXPECT_EQ(read.m_factory->GetSelectMode(), ibSelectMode::ibSelectMode_FoldersAndItems);
}

// ---------------------------------------------------------------------------------------------------------------
// THROUGH A FORM: the source's write says a field changed, the resolver tells the form open on that source, and
// the form applies its fields' own rows there — the same road a module's assignment takes, and a person's typing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// The form open on the sale, found by its source's key as any open form is found.
struct ChoiceFrame : ibBackendDocFrame {
	explicit ChoiceFrame(ibSessionHolder&& holder) : ibBackendDocFrame(std::move(holder)) {}
	void SetTitle(const ibString&) override {}
	void SetStatusText(const ibString&, int) override {}
	void RefreshFrame() override {}
	void RaiseFrame() override {}
	ibUniqueKey CreateFormUniqueKey(const ibBackendControlFrame*, const ibSourceDataObject*, const ibUniqueKey& formGuid) override {
		if (formGuid.isValid())
			return formGuid;
		ibUniqueKey made = wxNewUniqueGuid;
		return made;
	}
	ibBackendValueForm* FindFormBySourceUniqueKey(const ibUniqueKey& key) override { m_asked = key; return m_form; }
	ibBackendValueForm* m_form = nullptr;
	ibUniqueKey m_asked;   // the key the form was looked for by
};

struct ChoiceSession : ibSession {
	using ibSession::ibSession;
	ibBackendDocFrame* GetFrame() const override { return m_frame; }
	ibBackendDocFrame* m_frame = nullptr;
};

// A form standing on the sale.
struct SaleForm : ibValueForm {
	ibSourceDataObject* m_source = nullptr;
	ibSourceDataObject* GetSourceObject() const override { return m_source; }
};

// …and one that only says what reached it: the table of the holder a change was heard with.
struct HearingForm : SaleForm {
	int m_heard = 0;
	ibTabularDataObject* m_heardTable = nullptr;
	void NotifyChange(ibChoiceHolder& holder, const ibMetaID&) override { ++m_heard; m_heardTable = holder.m_table; }
};

// The sale's lines: a table whose rows have the sale's fields (the shape test_tabularHop uses).
class MockLines : public ibTabularDataObject {
public:
	const ibMetaData* m_metaData = nullptr;
	const ibValueMetaObjectGenericData* m_metaObject = nullptr;

	const ibValueMetaObjectCompositeData* GetSourceMetaObject() const override { return m_metaObject; }
	const ibMetaData* GetSourceMetaData() const override { return m_metaData; }
	ibClassID GetSourceClassType() const override { return 0; }
};

struct FormFieldChoiceThroughForm : FormFieldChoiceFix {
	void SetUp() override {
		FormFieldChoiceFix::SetUp();
		if (IsSkipped())
			return;
		if (wxLog::GetActiveTarget() != nullptr)
			delete wxLog::SetActiveTarget(new wxLogStderr());
		if (ibApplicationInstance::Get() == nullptr && !ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
	}
	void TearDown() override {
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	// A Textctrl placed on `form`, bound to the sale's `field` (a head hop, then the field), with `rows` of its own.
	ibValue AddField(SaleForm* form, const ibValueMetaObjectAttributeBase* field, const ibChoiceParametersDescription& rows) {
		ibValue control = MakeTextctrl();
		ibValue formValue(static_cast<ibValue*>(form));
		ibValue parent(static_cast<ibValue*>(form));
		ibValue generateId(false);
		ibValue* params[] = { &formValue, &parent, &generateId };
		EXPECT_TRUE(control.Init(params, 3));
		ibValueFrame* frame = dynamic_cast<ibValueFrame*>(control.GetRef());
		ibSourceDescription path;
		path.AppendSource(1);
		path.AppendSource(field->GetMetaID());
		dynamic_cast<ibPropertySource*>(frame->GetProperty(wxT("Source")))->SetValue(path);
		dynamic_cast<ibPropertyChoiceParameters*>(frame->GetProperty(wxT("ChoiceParameters")))->SetValue(rows);
		return control;
	}
};

} // namespace

// The counterparty changes on the sale: the contract, chosen under the field's own row naming it, is emptied.
// The same counterparty again changes nothing, and nothing is emptied; a row that says Keep empties nothing.
TEST_F(FormFieldChoiceThroughForm, AChangedSourceFieldEmptiesTheFieldChosenUnderIt)
{
	auto session = std::make_shared<ChoiceSession>(wxT("form-field-choice"), ibSessionKind::Enterprise);
	ibSessionHolder holder = ibApplicationInstance::GetSessionRegistry()->MintUnlisted(session);
	ChoiceFrame frame(std::move(holder));
	session->m_frame = &frame;
	ibSessionScope scope(session.get());

	SaleForm* form = new SaleForm();
	const ibValue formValue(static_cast<ibValue*>(form));
	form->m_source = &sale;
	frame.m_form = form;
	sale.m_gate = true;

	ibChoiceParametersDescription clear;
	clear.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));
	const ibValue contractField = AddField(form, contract, clear);

	// A different counterparty: the contract goes.
	sale.SetValueBySourceHop(ibSourceHop{ counterparty->GetMetaID() }, ibValue(ibString(wxT("Initech"))));
	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString().IsEmpty());

	// The same counterparty again: no change, nothing emptied.
	sale.m_fields[contract->GetMetaID()] = ibValue(ibString(wxT("No. 8")));
	sale.SetValueBySourceHop(ibSourceHop{ counterparty->GetMetaID() }, ibValue(ibString(wxT("Initech"))));
	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString() == wxT("No. 8"));

	// A field nobody's row names: nothing emptied.
	sale.SetValueBySourceHop(ibSourceHop{ organisation->GetMetaID() }, ibValue(ibString(wxT("Another firm"))));
	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString() == wxT("No. 8"));

	// The row says Keep: a different counterparty leaves the contract.
	ibChoiceParametersDescription keep;
	keep.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID(), ibChoiceParameterOnChange::Keep));
	dynamic_cast<ibPropertyChoiceParameters*>(dynamic_cast<ibValueFrame*>(contractField.GetRef())
		->GetProperty(wxT("ChoiceParameters")))->SetValue(keep);
	sale.SetValueBySourceHop(ibSourceHop{ counterparty->GetMetaID() }, ibValue(ibString(wxT("Globex"))));
	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString() == wxT("No. 8"));

	sale.m_gate = false;
	frame.m_form = nullptr;
}

// A ROW tells the form of the object its table is part of: the holder carries that object's key — a section passes
// its object's — and the form is found by it. A register's line, keyed by nothing, looks for no form.
TEST_F(FormFieldChoiceThroughForm, ARowTellsTheFormOfItsObject)
{
	auto session = std::make_shared<ChoiceSession>(wxT("row-choice"), ibSessionKind::Enterprise);
	ibSessionHolder holder = ibApplicationInstance::GetSessionRegistry()->MintUnlisted(session);
	ChoiceFrame frame(std::move(holder));
	session->m_frame = &frame;
	ibSessionScope scope(session.get());

	HearingForm* form = new HearingForm();
	const ibValue formValue(static_cast<ibValue*>(form));
	frame.m_form = form;

	MockLines lines;
	lines.m_metaData = &cfg;
	lines.m_metaObject = sale.m_metaObject;
	const ibDataViewItem row;

	ibChoiceHolder keyed(&lines, row, sale.GetGuid());
	ibChoiceLinkResolver::ClearLinked(keyed, counterparty->GetMetaID());
	EXPECT_EQ(form->m_heard, 1);
	EXPECT_TRUE(form->m_heardTable == &lines);
	EXPECT_TRUE(frame.m_asked == sale.GetGuid());

	ibChoiceHolder line(&lines, row);
	ibChoiceLinkResolver::ClearLinked(line, counterparty->GetMetaID());
	EXPECT_EQ(form->m_heard, 1);

	frame.m_form = nullptr;
}

// A REGISTER'S RECORD FORM stands on the record manager, and the manager tells it: the same road as the object's,
// heard at the manager's own write door. A different counterparty empties the contract under the field's own row;
// the same one, an unrelated field and a Keep row leave it.
TEST_F(FormFieldChoiceThroughForm, ARecordFormHearsItsRecordManager)
{
	auto session = std::make_shared<ChoiceSession>(wxT("record-form-choice"), ibSessionKind::Enterprise);
	ibSessionHolder holder = ibApplicationInstance::GetSessionRegistry()->MintUnlisted(session);
	ChoiceFrame frame(std::move(holder));
	session->m_frame = &frame;
	ibSessionScope scope(session.get());

	auto* rates = dynamic_cast<ibValueMetaObjectRegisterData*>(
		cfg.CreateMetaObject(g_metaInformationRegisterCLSID, cfg.GetCommonMetaObject(), /*runObject*/ false));
	ASSERT_NE(rates, nullptr);
	auto* recordCounterparty = dynamic_cast<ibValueMetaObjectAttributeBase*>(cfg.CreateMetaObject(g_metaDimensionCLSID, rates, false));
	auto* recordContract = dynamic_cast<ibValueMetaObjectAttributeBase*>(cfg.CreateMetaObject(g_metaDimensionCLSID, rates, false));
	auto* recordOrganisation = dynamic_cast<ibValueMetaObjectAttributeBase*>(cfg.CreateMetaObject(g_metaResourceCLSID, rates, false));
	ASSERT_TRUE(recordCounterparty && recordContract && recordOrganisation);
	recordCounterparty->SetName(wxT("Counterparty"));
	recordContract->SetName(wxT("Contract"));
	recordOrganisation->SetName(wxT("Organisation"));
	for (ibValueMetaObjectAttributeBase* field : { recordCounterparty, recordContract, recordOrganisation })
		field->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);

	// The configuration RUN, so the register's types are registered: a record set compiles its module against
	// them. A new record is not read, so the base needs no tables (test_informationRegisterGet runs it the same way).
	ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
	ASSERT_NE(pool, nullptr);
	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(db->Open(wxT(":memory:")));
	pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
	ASSERT_TRUE(cfg.RunDatabase());

	{
		ibValuePtr<ibValueRecordManagerObject> manager = rates->CreateRecordManagerObjectValue();
		ASSERT_NE(manager, nullptr);

		SaleForm* form = new SaleForm();
		const ibValue formValue(static_cast<ibValue*>(form));
		form->m_source = manager;
		frame.m_form = form;

		ibChoiceParametersDescription clear;
		clear.SetRow(Row(contractCounterparty->GetMetaID(), recordCounterparty->GetMetaID()));
		const ibValue contractField = AddField(form, recordContract, clear);

		const auto contractNow = [&]() {
			ibValue value;
			manager->GetValueByMetaID(recordContract->GetMetaID(), value);
			return value.GetString();
		};

		manager->SetValueByMetaID(recordCounterparty->GetMetaID(), ibValue(ibString(wxT("Acme"))));
		manager->SetValueByMetaID(recordContract->GetMetaID(), ibValue(ibString(wxT("No. 7"))));
		ASSERT_TRUE(contractNow() == wxT("No. 7"));

		// A different counterparty: the contract goes.
		manager->SetValueByMetaID(recordCounterparty->GetMetaID(), ibValue(ibString(wxT("Initech"))));
		EXPECT_TRUE(contractNow().IsEmpty());

		// The same counterparty again, and a field nobody's row names: nothing emptied.
		manager->SetValueByMetaID(recordContract->GetMetaID(), ibValue(ibString(wxT("No. 8"))));
		manager->SetValueByMetaID(recordCounterparty->GetMetaID(), ibValue(ibString(wxT("Initech"))));
		EXPECT_TRUE(contractNow() == wxT("No. 8"));
		manager->SetValueByMetaID(recordOrganisation->GetMetaID(), ibValue(ibString(wxT("Our firm"))));
		EXPECT_TRUE(contractNow() == wxT("No. 8"));

		// The row says Keep: a different counterparty leaves the contract.
		ibChoiceParametersDescription keep;
		keep.SetRow(Row(contractCounterparty->GetMetaID(), recordCounterparty->GetMetaID(), ibChoiceParameterOnChange::Keep));
		dynamic_cast<ibPropertyChoiceParameters*>(dynamic_cast<ibValueFrame*>(contractField.GetRef())
			->GetProperty(wxT("ChoiceParameters")))->SetValue(keep);
		manager->SetValueByMetaID(recordCounterparty->GetMetaID(), ibValue(ibString(wxT("Globex"))));
		EXPECT_TRUE(contractNow() == wxT("No. 8"));

		frame.m_form = nullptr;
	}
	cfg.CloseDatabase();
}
