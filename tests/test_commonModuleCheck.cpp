// config_check asks whether the common modules compile, beside the questions
// it already asked. The answer is a problem whose diagnostics are the
// compiler's, and a module that was never compiled is still a problem:
// checked false, with an empty diagnostics list, is not a clean module.
//
// The configuration is the one a designer session opens. The compile cache
// is the one that session builds. Nothing here substitutes its own.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/mcp/mcpTool.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"

namespace {

struct CommonModuleCheck : ::testing::Test {
	wxInitializer m_wxInit;
	bool m_ownsApp = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (ibApplicationInstance::Get() == nullptr) {
			if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
				GTEST_SKIP() << "appData env unavailable headless";
			m_ownsApp = true;
		}
	}

	void TearDown() override {
		if (m_ownsApp && ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

struct CodeStyleRestore {
	short m_saved = ibCompileCode::GetCodeStyle();
	~CodeStyleRestore() { ibCompileCode::SetCodeStyle(m_saved); }
};

} // namespace

TEST_F(CommonModuleCheck, ConfigCheckReportsABrokenModuleAndOneThatWasNeverCompiled)
{
	CodeStyleRestore style;
	ibSession session(wxT("module-check"), ibSessionKind::Designer);
	ibSessionScope scope(&session);

	ASSERT_TRUE(ibApplicationInstance::CreateActiveMetaData(
		ibApplicationInstance::Get(), ibSessionKind::Designer, 0));

	ibMetaDataConfigurationBase* cfg = activeMetaData;
	ASSERT_NE(cfg, nullptr);
	if (cfg->IsConfigOpen())
		ASSERT_TRUE(cfg->CloseDatabase(forceCloseFlag));

	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	ASSERT_NE(root, nullptr);

	auto* clean = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, false));
	auto* broken = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, false));
	ASSERT_NE(clean, nullptr);
	ASSERT_NE(broken, nullptr);
	clean->SetModuleText(wxT("Procedure Clean()\na = 1;\nEndProcedure\n"));
	broken->SetModuleText(wxT("Procedure Broken()\na = Nope;\nEndProcedure\n"));

	ASSERT_TRUE(cfg->RunDatabase());
	ibCompileCode::SetCodeStyle(style.m_saved);

	auto* never = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, false));
	ASSERT_NE(never, nullptr);
	never->SetModuleText(wxT("Procedure Never()\na = 1;\nEndProcedure\n"));

	const ibMcpTool* check = ibFindMcpTool(wxT("config_check"));
	ASSERT_NE(check, nullptr);
	ibDataNode params;
	ibDataNode result;
	wxString refusal;
	ASSERT_TRUE(check->Call(params, result, refusal)) << refusal.ToStdString();

	const ibDataValue* brokenProblem = nullptr;
	const ibDataValue* neverProblem = nullptr;
	const ibDataValue* problems = result.FindField(wxT("problems"));
	ASSERT_NE(problems, nullptr);
	ASSERT_EQ(problems->Kind(), ibDataKind::Array);
	for (const ibDataValue& one : problems->AsArray()) {
		if (one.Kind() != ibDataKind::Child || !one.AsChild())
			continue;
		const wxString text = one.AsChild()->GetValue<wxString>(wxT("name"))
			+ one.AsChild()->GetValue<wxString>(wxT("says"));
		const ibDataValue* diagnostics = one.AsChild()->FindField(wxT("diagnostics"));
		if (diagnostics != nullptr && diagnostics->Kind() == ibDataKind::Array) {
			for (const ibDataValue& line : diagnostics->AsArray()) {
				if (line.Kind() == ibDataKind::Child && line.AsChild()
					&& line.AsChild()->GetValue<wxString>(wxT("message")).Contains(wxT("Nope")))
					brokenProblem = &one;
			}
		}
		if (one.AsChild()->GetValue<wxString>(wxT("name")) == never->GetName()
			|| text.Contains(never->GetName()))
			neverProblem = &one;
		EXPECT_FALSE(text.Contains(clean->GetName()) && one.AsChild()->FindField(wxT("diagnostics")) != nullptr
			&& one.AsChild()->GetValue<wxString>(wxT("says")).Contains(wxT("does not compile")));
	}

	ASSERT_NE(brokenProblem, nullptr);
	const ibDataValue* diagnostics = brokenProblem->AsChild()->FindField(wxT("diagnostics"));
	ASSERT_NE(diagnostics, nullptr);
	EXPECT_EQ(diagnostics->Kind(), ibDataKind::Array);
	EXPECT_FALSE(diagnostics->AsArray().empty());
	EXPECT_TRUE(brokenProblem->AsChild()->GetValue<bool>(wxT("checked")));

	ASSERT_NE(neverProblem, nullptr);
	EXPECT_FALSE(neverProblem->AsChild()->GetValue<bool>(wxT("checked")));
	const ibDataValue* empty = neverProblem->AsChild()->FindField(wxT("diagnostics"));
	ASSERT_NE(empty, nullptr);
	EXPECT_TRUE(empty->Kind() == ibDataKind::Array && empty->AsArray().empty());

	const ibMcpTool* apply = ibFindMcpTool(wxT("config_apply"));
	ASSERT_NE(apply, nullptr);
	ibDataNode applyParams;
	applyParams.AddField(wxT("confirm"), ibDataValue::Bool(false));
	ibDataNode applyResult;
	wxString applyRefusal;
	ASSERT_TRUE(apply->Call(applyParams, applyResult, applyRefusal)) << applyRefusal.ToStdString();
	const ibDataValue* applyProblems = applyResult.FindField(wxT("problems"));
	ASSERT_NE(applyProblems, nullptr);
	EXPECT_EQ(applyProblems->Kind(), ibDataKind::Array);
	EXPECT_EQ(applyResult.FindField(wxT("moduleDiagnostics")), nullptr);
}
