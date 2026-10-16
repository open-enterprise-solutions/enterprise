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

TEST(XmlString, AWriterEscapesTextAndClosesElements) {
	ibValue writer = ibValue::CreateObject(wxT("XMLWriter"));
	const long setString = writer.FindMethod(wxT("SetString"));
	const long declaration = writer.FindMethod(wxT("WriteXMLDeclaration"));
	const long start = writer.FindMethod(wxT("WriteStartElement"));
	const long attribute = writer.FindMethod(wxT("WriteAttribute"));
	const long text = writer.FindMethod(wxT("WriteText"));
	const long end = writer.FindMethod(wxT("WriteEndElement"));
	const long close = writer.FindMethod(wxT("Close"));
	ASSERT_NE(setString, wxNOT_FOUND);
	writer.CallAsProc(setString, nullptr, 0);
	writer.CallAsProc(declaration, nullptr, 0);
	ibValue root(wxT("root"));
	ibValue child(wxT("child"));
	ibValue id(wxT("id"));
	ibValue one(wxT("1"));
	ibValue body(wxT("a<b"));
	ibValue* rootArg[] = { &root };
	ibValue* childArg[] = { &child };
	ibValue* attrArg[] = { &id, &one };
	ibValue* textArg[] = { &body };
	writer.CallAsProc(start, rootArg, 1);
	writer.CallAsProc(attribute, attrArg, 2);
	writer.CallAsProc(start, childArg, 1);
	writer.CallAsProc(text, textArg, 1);
	writer.CallAsProc(end, nullptr, 0);
	writer.CallAsProc(end, nullptr, 0);
	ibValue written;
	ASSERT_TRUE(writer.CallAsFunc(close, written, nullptr, 0));
	EXPECT_EQ(written.GetString(), wxT("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root id=\"1\"><child>a&lt;b</child></root>"));
}

TEST(XmlString, TheNameCompiles) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Procedure Check()\n")
		wxT("    var writer;\n")
		wxT("    writer = New XMLWriter();\n")
		wxT("EndProcedure\n");
	try {
		ASSERT_TRUE(cc.Compile(src));
	} catch (const ibBackendException& err) {
		FAIL() << err.GetErrorDescription().ToStdString();
	}
}

TEST(ValueFactory, AnInitThatAnswersFalseIsStillCleanedUpAfter) {
	ASSERT_EQ(g_alive, 0);
	ibValue quiet(false);
	ibValue* params[] = { &quiet };
	EXPECT_THROW(ibValue::CreateObject(wxT("TestRefusingInit"), params, 1), ibBackendException);
	EXPECT_EQ(g_alive, 0);
}
