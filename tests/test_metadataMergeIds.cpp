// A data processor read from its file INTO a configuration (the designer's "read from file", ReadExternal) cannot keep
// the file's ids: they would collide with the configuration's own. #578: the objects were renumbered, but what HOLDS
// their ids inside a form was not — a button still named the file's command id, a field the file's attribute id, the
// form's main attribute the file's root. In the same configuration the file was saved from, those are the ids of the
// ORIGINAL data processor, so the copy's form quietly bound the original's command.
//
// ReadExternal reads the file on its own and pastes it in, as a copy made in the designer is pasted; the steps are
// taken here as it takes them. The data processor is built through the MCP tools, saved to a file and read back into
// the same configuration, and the copy's form — taken as the designer's editor takes it, from the compile cache — is
// asked what it holds. The configuration is the one a designer session opens, as in test_commandDescription.cpp.

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <filesystem>
#include <memory>

#include <wx/init.h>

#include "framedSession.h"   // KeyFrame / FramedSession — a designer's session has a frame, and a form is born against it

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/metadataDataProcessor.h"
#include "backend/propertyManager/property/propertyCommandSource.h"
#include "backend/propertyManager/property/propertySource.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "core/description/commandDescription.h"
#include "core/description/sourceDescription.h"
#include "core/description/typeDescription.h"
#include "core/memory/memoryStream.h"   // ibWriterMemory / ibReaderMemory — the copy's payload
#include "core/serialize/dataBuilder.h"
#include "frmserver/mcp/mcpTool.h"
#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/formAttribute.h"

namespace {

struct MetadataMergeIds : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> m_db;
	bool m_ownsApp = false;
	std::filesystem::path m_file;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (ibApplicationInstance::Get() == nullptr) {
			if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
				GTEST_SKIP() << "appData env unavailable headless";
			m_ownsApp = true;
		}
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		m_db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!m_db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(m_db, /*maxSize=*/1, /*minIdle=*/0);
		ibMetaDataConfigurationStorage::CreateConfigTable();
		ibMetaDataConfigurationStorage::CreateConfigSaveTable();
		ibMetaDataConfigurationStorage::CreateConfigSequence();
		m_file = std::filesystem::temp_directory_path() / "oes_test_metadataMergeIds.epf";
	}

	void TearDown() override {
		std::error_code ec;
		std::filesystem::remove(m_file, ec);
		if (m_ownsApp && ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

bool CallTool(const wxChar* name, const ibDataNode& params, ibDataNode& result, ibString& refusal)
{
	const ibMcpTool* tool = ibFindMcpTool(name);
	return tool != nullptr && tool->Call(params, result, refusal);
}

std::int64_t Create(const wxChar* kind, const wxChar* name, std::int64_t parent, const wxChar* formType = nullptr)
{
	ibDataNode params, result;
	params.SetValue(ibT("kind"), ibString(kind));
	params.SetValue(ibT("name"), ibString(name));
	if (parent > 0)
		params.AddField(ibT("parentId"), ibDataValue::Int(parent));
	if (formType != nullptr) {
		auto properties = std::make_shared<ibDataNode>();
		properties->SetValue(ibT("FormType"), ibString(formType));
		params.AddField(ibT("properties"), ibDataValue::Child(properties));
	}
	ibString refusal;
	EXPECT_TRUE(CallTool(wxT("metadata_create"), params, result, refusal)) << refusal.ToStdString();
	return result.GetValue<std::int32_t>(ibT("id"));
}

ibValueMetaObject* ChildNamed(ibValueMetaObject* parent, const ibClassID& clsid, const wxChar* name)
{
	for (unsigned int idx = 0; idx < parent->GetChildCount(); idx++) {
		ibValueMetaObject* child = parent->GetChild(idx);
		if (child != nullptr && child->GetClassType() == clsid && child->GetName() == ibString(name))
			return child;
	}
	return nullptr;
}

bool NamesMetaID(const ibTypeDescription& type, ibMetaID id)
{
	for (const ibClassID clsid : type.GetClsidList())
		if (IsMetaValue(clsid) && (ibMetaID)clsid_metaID(clsid) == id)
			return true;
	return false;
}

} // namespace

TEST_F(MetadataMergeIds, ADataProcessorReadIntoTheConfiguration_ItsFormHoldsTheNewIds)
{
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	auto framed = std::make_shared<FramedSession>(wxT("metadata-merge-ids"), ibSessionKind::Designer);
	ibSession* const session = framed.get();
	KeyFrame frame(registry->MintUnlisted(framed));
	framed->Attach(&frame);
	ibSessionScope scope(session);

	registry->NotifyAuthenticated(session);
	struct Leave {
		ibSessionRegistry* m_registry;
		ibSession* m_session;
		~Leave() { m_registry->NotifyDisconnect(m_session); }
	} leave{ registry, session };

	ibMetaDataConfigurationBase* cfg = session->GetMetaData();
	ASSERT_NE(cfg, nullptr);
	if (cfg->IsConfigOpen()) {
		ASSERT_TRUE(cfg->CloseDatabase(ibRuntimeRequest(forceCloseFlag)));
	}
	ASSERT_TRUE(cfg->RunDatabase());

	// The original: an attribute, a command, an object form with a field on the attribute and a button on the command.
	const std::int64_t dpId = Create(wxT("DataProcessor"), wxT("Mover"), 0);
	ASSERT_GT(dpId, 0);
	const std::int64_t amountId = Create(wxT("Attribute"), wxT("Amount"), dpId);
	const std::int64_t actId = Create(wxT("Command"), wxT("Act"), dpId);
	const std::int64_t formId = Create(wxT("Form"), wxT("Main"), dpId, wxT("FormDataProcessor"));
	ASSERT_GT(amountId, 0);
	ASSERT_GT(actId, 0);
	ASSERT_GT(formId, 0);

	auto add = [&](const wxChar* cls) {
		ibDataNode params, result;
		params.AddField(ibT("form"), ibDataValue::Int(formId));
		params.SetValue(ibT("class"), ibString(cls));
		ibString refusal;
		EXPECT_TRUE(CallTool(wxT("form_add"), params, result, refusal)) << refusal.ToStdString();
		return result.GetValue<std::int32_t>(ibT("controlId"));
	};
	auto bind = [&](std::int64_t control, const wxChar* property, const wxChar* path) {
		ibDataNode params, result;
		params.AddField(ibT("form"), ibDataValue::Int(formId));
		params.AddField(ibT("control"), ibDataValue::Int(control));
		params.SetValue(ibT("property"), ibString(property));
		params.SetValue(ibT("path"), ibString(path));
		ibString refusal;
		EXPECT_TRUE(CallTool(wxT("form_bind"), params, result, refusal)) << refusal.ToStdString();
	};
	const std::int64_t buttonId = add(wxT("Button"));
	const std::int64_t fieldId = add(wxT("Textctrl"));
	ASSERT_GT(buttonId, 0);
	ASSERT_GT(fieldId, 0);
	bind(buttonId, wxT("Command"), wxT("Act"));
	bind(fieldId, wxT("Source"), wxT("Amount"));

	ibValueMetaObjectDataProcessor* original = nullptr;
	ibValueMetaObject* const root = cfg->GetCommonMetaObject();
	for (unsigned int idx = 0; idx < root->GetChildCount() && original == nullptr; idx++)
		if (root->GetChild(idx)->GetMetaID() == (ibMetaID)dpId)
			original = dynamic_cast<ibValueMetaObjectDataProcessor*>(root->GetChild(idx));
	ASSERT_NE(original, nullptr);

	// Written to its file, as the designer's "save to file" does…
	ASSERT_TRUE(std::make_shared<ibMetaDataDataProcessor>(cfg, original)->SaveToFile(ibString(m_file.wstring())));

	// …and read back into the same configuration, as "read from file" does (ReadExternal): read on its own, copied, and
	// pasted under the configuration's root.
	ibWriterMemory copied;
	{
		const std::shared_ptr<ibMetaDataDataProcessor> reader = ibMetaData::MakeShared<ibMetaDataDataProcessor>();
		ASSERT_TRUE(reader->LoadFromFile(ibString(m_file.wstring())));
		ASSERT_TRUE(reader->CopyMetaObject(reader->GetDataProcessor(), copied));
	}
	ibReaderMemory pasted(copied.pointer(), copied.size());
	ibValueMetaObject* const copy = cfg->PasteMetaObject(g_metaDataProcessorCLSID, cfg->GetCommonMetaObject(), pasted);
	ASSERT_NE(copy, nullptr);

	// Every object of the copy has an id of its own: the root, the parts, the predefined children (its modules).
	EXPECT_NE(copy->GetMetaID(), (ibMetaID)dpId);
	ibValueMetaObject* const amount = ChildNamed(copy, g_metaAttributeCLSID, wxT("Amount"));
	ibValueMetaObject* const act = ChildNamed(copy, g_metaCommandCLSID, wxT("Act"));
	auto* const form = dynamic_cast<ibValueMetaObjectFormBase*>(ChildNamed(copy, g_metaFormCLSID, wxT("Main")));
	ASSERT_NE(amount, nullptr);
	ASSERT_NE(act, nullptr);
	ASSERT_NE(form, nullptr);
	EXPECT_NE(amount->GetMetaID(), (ibMetaID)amountId);
	EXPECT_NE(act->GetMetaID(), (ibMetaID)actId);
	for (unsigned int idx = 0; idx < copy->GetChildCount(); idx++) {
		ibValueMetaObject* const child = copy->GetChild(idx);
		for (unsigned int other = 0; other < original->GetChildCount(); other++)
			EXPECT_NE(child->GetMetaID(), original->GetChild(other)->GetMetaID())
				<< "the copy's '" << child->GetName().ToStdString() << "' kept an id of the original";
	}

	// And its form holds them — taken as the designer's editor takes it (ibFormDesignView::LoadForm): from the compile
	// cache, built over its object.
	ibCompileValueCache* const cache = cfg->GetCompileCache();
	ASSERT_NE(cache, nullptr);
	ibValueForm* valueForm = nullptr;
	ASSERT_TRUE(cache->FindCompileModule(form, valueForm));
	ASSERT_NE(valueForm, nullptr);

	// The button runs the copy's command — not the original's, whose id the file carried.
	ibValueFrame* const button = valueForm->FindControlByID((ibFormID)buttonId);
	ASSERT_NE(button, nullptr);
	auto* const command = dynamic_cast<ibPropertyCommandSource*>(button->GetProperty(ibT("Command")));
	ASSERT_NE(command, nullptr);
	const ibCommandDescription& commandDesc = command->GetValueAsCommandDesc();
	ASSERT_EQ(commandDesc.m_listCommand.size(), 1u);
	EXPECT_EQ(commandDesc.m_listCommand[0].m_id, act->GetMetaID());

	// The field shows the copy's attribute: the head hop is the form's own attribute and stays, the next is Amount.
	ibValueFrame* const field = valueForm->FindControlByID((ibFormID)fieldId);
	ASSERT_NE(field, nullptr);
	auto* const source = dynamic_cast<ibPropertySource*>(field->GetProperty(ibT("Source")));
	ASSERT_NE(source, nullptr);
	const ibSourceDescription& sourceDesc = source->GetValueAsSourceDesc();
	ASSERT_EQ(sourceDesc.m_listSource.size(), 2u);
	EXPECT_EQ(sourceDesc.m_listSource[1].m_id, amount->GetMetaID());

	// The main attribute is typed by the copy itself: an object form's main is made from its object.
	ibFormAttributeValue* const main = valueForm->GetMainAttribute();
	ASSERT_NE(main, nullptr);
	EXPECT_TRUE(NamesMetaID(main->GetTypeDesc(), copy->GetMetaID()));
	EXPECT_FALSE(NamesMetaID(main->GetTypeDesc(), (ibMetaID)dpId));
}
