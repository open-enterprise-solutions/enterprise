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

TEST(UserMessage, AConstructorKeepsTheTextTheFieldAndTheKey) {
	ibValue text(wxT("Amount is empty"));
	ibValue field(wxT("Amount"));
	ibValue path(wxT("Object.Amount"));
	ibValue key(wxT("row-1"));
	ibValue* params[] = { &text, &field, &path, &key };
	ibValue msg = ibValue::CreateObject(wxT("UserMessage"), params, 4);
	ASSERT_FALSE(msg.IsEmpty());

	ibValue got;
	ASSERT_TRUE(msg.GetPropVal(msg.FindProp(wxT("Text")), got));
	EXPECT_EQ(got.GetString(), wxT("Amount is empty"));
	ASSERT_TRUE(msg.GetPropVal(msg.FindProp(wxT("Field")), got));
	EXPECT_EQ(got.GetString(), wxT("Amount"));
	ASSERT_TRUE(msg.GetPropVal(msg.FindProp(wxT("DataPath")), got));
	EXPECT_EQ(got.GetString(), wxT("Object.Amount"));
	ASSERT_TRUE(msg.GetPropVal(msg.FindProp(wxT("DataKey")), got));
	EXPECT_EQ(got.GetString(), wxT("row-1"));

	const long method = msg.FindMethod(wxT("Message"));
	ASSERT_NE(method, wxNOT_FOUND);
	EXPECT_TRUE(msg.CallAsProc(method, nullptr, 0));
}

TEST(UserMessage, TheNameCompiles) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Procedure Check()\n")
		wxT("    var msg;\n")
		wxT("    msg = New UserMessage(\"Amount is empty\", \"Amount\");\n")
		wxT("    msg.Message();\n")
		wxT("EndProcedure\n");
	try {
		ASSERT_TRUE(cc.Compile(src));
	} catch (const ibBackendException& err) {
		FAIL() << err.GetErrorDescription().ToStdString();
	}
}
