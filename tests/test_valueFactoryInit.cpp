// =============================================================================
// The value factory and an Init that REFUSES BY RAISING - which is how a type says why it refuses. The object
// used to be held in a raw pointer until Init returned: deleted when Init answered false, leaked when Init
// raised. A new value is now born owned (the factory answers with its holder), so a refusal either way lets it go.
// =============================================================================

#include <gtest/gtest.h>

#include <string>

#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/system/systemManager.h"

namespace {

int g_alive = 0;

// Registered for this suite only: it counts itself, and refuses any argument by raising.
class ibValueTestRefusingInit : public ibValue {
public:
	ibValueTestRefusingInit() : ibValue(ibValueTypes::TYPE_VALUE, true) { ++g_alive; }
	virtual ~ibValueTestRefusingInit() { --g_alive; }

	virtual bool Init() { return true; }
	virtual bool Init(ibValue** paParams, const long) {
		if (paParams[0]->GetBoolean())
			ibBackendCoreException::Error(wxT("refused, and this is why"));
		return false;   // the quiet refusal, which the factory has always cleaned up after
	}
};

} // namespace

VALUE_TYPE_REGISTER(ibValueTestRefusingInit, "TestRefusingInit", value_to_clsid("VL_TRIN"));

TEST(ValueFactory, AnInitThatRaisesDoesNotLeakTheObject) {
	ASSERT_EQ(g_alive, 0);
	ibValue raise(true);
	ibValue* params[] = { &raise };
	try {
		ibValue::CreateObject(wxT("TestRefusingInit"), params, 1);
		FAIL() << "the refusal must reach the caller";
	}
	catch (const ibBackendException& e) {
		EXPECT_NE(std::string(e.what()).find("this is why"), std::string::npos) << "the type's own words, not a generic sentence";
	}
	EXPECT_EQ(g_alive, 0) << "an object whose Init raised is nobody's - the factory lets go of it";
}

TEST(ValueFactory, AnInitThatAnswersFalseIsStillCleanedUpAfter) {
	ASSERT_EQ(g_alive, 0);
	ibValue quiet(false);
	ibValue* params[] = { &quiet };
	EXPECT_THROW(ibValue::CreateObject(wxT("TestRefusingInit"), params, 1), ibBackendException);
	EXPECT_EQ(g_alive, 0);
}

TEST(CompositionField, APathIsKeptUnderBothNames) {
	ibValue path(wxT("Amount"));
	ibValue* args[] = { &path };
	ibValue field = ibValue::CreateObject(wxT("CompositionField"), args, 1);
	ibValue alias = ibValue::CreateObject(wxT("DataCompositionField"), args, 1);
	ibValue got;
	ASSERT_TRUE(field.GetPropVal(field.FindProp(wxT("Path")), got));
	EXPECT_EQ(got.GetString(), wxT("Amount"));
	ASSERT_NE(alias.FindProp(wxT("Path")), wxNOT_FOUND);
	ASSERT_TRUE(alias.GetPropVal(alias.FindProp(wxT("Path")), got));
	EXPECT_EQ(got.GetString(), wxT("Amount"));
	EXPECT_NE(field.GetClassType(), alias.GetClassType());
}

TEST(CompositionField, TheNamesCompile) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ibValueSystemFunction valueSystem;
	cc.AddContextVariable(wxT("System"), &valueSystem, true);
	const wxString src =
		wxT("Procedure Check()\n")
		wxT("    var field;\n")
		wxT("    var alias;\n")
		wxT("    field = New CompositionField(\"Amount\");\n")
		wxT("    alias = New DataCompositionField(\"Amount\", \"Sum\");\n")
		wxT("EndProcedure\n");
	try {
		ASSERT_TRUE(cc.Compile(src));
	} catch (const ibBackendException& err) {
		FAIL() << err.GetErrorDescription().ToStdString();
	}
}

TEST(FormScriptEnums, TheMembersAreNamed) {
	const wxString pairs[][2] = {
		{ wxT("FormFieldType"), wxT("InputField") },
		{ wxT("FormFieldType"), wxT("RadioButtonField") },
		{ wxT("FormFieldType"), wxT("PDFDocumentField") },
		{ wxT("ButtonRepresentation"), wxT("PictureAndText") },
		{ wxT("ColumnsGroup"), wxT("InCell") },
		{ wxT("StandardPeriodVariant"), wxT("Today") },
		{ wxT("StandardPeriodVariant"), wxT("FromBeginningOfThisYear") },
		{ wxT("StandardPeriodVariant"), wxT("Last7Days") },
		{ wxT("StandardPeriodVariant"), wxT("Month") },
	};
	for (const auto& pair : pairs) {
		ibValue en = ibValue::CreateObject(pair[0]);
		EXPECT_NE(en.FindProp(pair[1]), wxNOT_FOUND)
			<< pair[0].ToStdString() << "." << pair[1].ToStdString();
	}
	ibValue kind = ibValue::CreateObject(wxT("FormFieldType"));
	EXPECT_EQ(kind.FindProp(wxT("NotAField")), wxNOT_FOUND);
}

TEST(FormScriptEnums, TheNamesCompile) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ibValueSystemFunction valueSystem;
	ibValue enums = ibValue::CreateObject(wxT("EnumManager"));
	cc.AddContextVariable(wxT("System"), &valueSystem, true);
	cc.AddContextVariable(wxT("EnumManager"), enums, true);
	const wxString src =
		wxT("Procedure Check()\n")
		wxT("    var kind;\n")
		wxT("    var picture;\n")
		wxT("    var columns;\n")
		wxT("    var period;\n")
		wxT("    kind = FormFieldType.InputField;\n")
		wxT("    picture = ButtonRepresentation.PictureAndText;\n")
		wxT("    columns = ColumnsGroup.InCell;\n")
		wxT("    period = StandardPeriodVariant.Today;\n")
		wxT("EndProcedure\n");
	try {
		ASSERT_TRUE(cc.Compile(src));
	} catch (const ibBackendException& err) {
		FAIL() << err.GetErrorDescription().ToStdString();
	}
}
