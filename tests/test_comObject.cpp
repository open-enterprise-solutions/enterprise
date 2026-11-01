// =============================================================================
// COMObject — an object of another program, reached by name through IDispatch.
//
// On every platform: the type is registered, so `New COMObject("A.B")` COMPILES — a configuration that
// drives equipment through COM has to compile on a Mac too. Elsewhere than Windows, creating one is
// refused at New with the Windows-only message.
//
// On Windows: late binding against the automation objects every Windows has — Scripting.Dictionary
// (methods, properties, a SAFEARRAY back, For Each) and VBScript.RegExp (property put, a returned object
// collection) — and the values crossing both ways through the dictionary's own storage.
// =============================================================================

#include <gtest/gtest.h>

#include <vector>

#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueOLE.h"

namespace {

// New COMObject(progId), the way a script's New reaches it. A refusal comes back as its text.
ibValue Create(const ibString& progId, ibString* refusal = nullptr)
{
	ibValue name(progId);
	ibValue* params[] = { &name };
	try {
		return ibValue::CreateObject(ibString(wxT("COMObject")), params, 1);
	}
	catch (const ibBackendException& err) {
		if (refusal != nullptr)
			*refusal = err.GetErrorDescription();
	}
	return ibValue();
}

#ifdef __WXMSW__

ibValue Call(ibValue& object, const ibString& method, std::vector<ibValue> args = {})
{
	const long id = object.FindMethod(method);
	EXPECT_GE(id, 0) << method.ToStdString();
	std::vector<ibValue*> params;
	for (ibValue& arg : args)
		params.push_back(&arg);
	ibValue result;
	object.CallAsFunc(id, result, params.empty() ? nullptr : params.data(), static_cast<long>(params.size()));
	return result;
}

ibValue Get(ibValue& object, const ibString& prop)
{
	const long id = object.FindProp(prop);
	EXPECT_GE(id, 0) << prop.ToStdString();
	ibValue result;
	object.GetPropVal(id, result);
	return result;
}

void Set(ibValue& object, const ibString& prop, const ibValue& value)
{
	const long id = object.FindProp(prop);
	EXPECT_GE(id, 0) << prop.ToStdString();
	object.SetPropVal(id, value);
}

// What a value comes back as after a stay in the dictionary: put in as a VARIANT, read out of one.
ibValue RoundTrip(const ibValue& value)
{
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("k"))), value });
	return Call(dictionary, wxT("Item"), { ibValue(ibString(wxT("k"))) });
}

#endif

} // namespace

TEST(ComObject, TheTypeIsRegisteredOnEveryPlatform)
{
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("COMObject"), ibCtorObjectType::ibCtorObjectType_object_value))
		<< "names are case-insensitive: COMObject is the registered ComObject";
}

TEST(ComObject, NewCompilesOnEveryPlatform)
{
	const ibString src = ibCompileCode::GetCodeStyle() == CODE_CES
		? ibString(wxT("var c = New COMObject(\"A.B\");"))
		: ibString(wxT("c = New COMObject(\"A.B\");"));

	ibCompileCode cc(wxT("comObject"), wxT("memory"), false);
	ibString why;
	bool compiled = false;
	try {
		compiled = cc.Compile(src);
	}
	catch (const ibBackendException& err) {
		why = err.GetErrorDescription();
	}
	EXPECT_TRUE(compiled) << why.ToStdString();
}

#ifndef __WXMSW__

TEST(ComObject, CreatingOneElsewhereIsRefusedAtNew)
{
	ibString refusal;
	const ibValue made = Create(wxT("Scripting.Dictionary"), &refusal);
	EXPECT_TRUE(made.IsEmpty());
	EXPECT_EQ(refusal, ibString(_("COM objects are available on Windows only")));
}

#else

TEST(ComObject, AnUnregisteredProgIdIsNamed)
{
	ibString refusal;
	Create(wxT("NoSuch.ProgId.636"), &refusal);
	EXPECT_NE(refusal.Find(wxT("NoSuch.ProgId.636")), wxNOT_FOUND) << refusal.ToStdString();
}

TEST(ComObject, MethodsAndPropertiesByName)
{
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	ASSERT_EQ(dictionary.GetType(), ibValueTypes::TYPE_REFFER);

	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("a"))), ibValue(ibNumber(1)) });
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("b"))), ibValue(ibNumber(2)) });

	EXPECT_EQ(Get(dictionary, wxT("Count")).GetNumber(), ibNumber(2));
	EXPECT_EQ(Call(dictionary, wxT("Item"), { ibValue(ibString(wxT("b"))) }).GetNumber(), ibNumber(2));
	EXPECT_TRUE(Call(dictionary, wxT("Exists"), { ibValue(ibString(wxT("a"))) }).GetBoolean());
	EXPECT_FALSE(Call(dictionary, wxT("Exists"), { ibValue(ibString(wxT("z"))) }).GetBoolean());
}

TEST(ComObject, AnArrayComesBackAsAnArray)
{
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("a"))), ibValue(ibNumber(1)) });
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("b"))), ibValue(ibNumber(2)) });

	ibValue keys = Call(dictionary, wxT("Keys"));
	ibValueArray* arr = nullptr;
	ASSERT_TRUE(keys.ConvertToValue(arr));
	ASSERT_EQ(arr->Count(), 2u);
	EXPECT_EQ(arr->Values()[0].GetString(), ibString(wxT("a")));
	EXPECT_EQ(arr->Values()[1].GetString(), ibString(wxT("b")));
}

TEST(ComObject, ForEachWalksTheObjectsOwnEnumerator)
{
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("x"))), ibValue(ibNumber(1)) });
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("y"))), ibValue(ibNumber(2)) });

	std::shared_ptr<ibValueIteratorState> walk = dictionary.CreateIterator();
	ASSERT_NE(walk, nullptr);
	ibValue key;
	std::vector<ibString> keys;
	while (walk->MoveNext(key))
		keys.push_back(key.GetString());
	ASSERT_EQ(keys.size(), 2u);
	EXPECT_EQ(keys[0], ibString(wxT("x")));
	EXPECT_EQ(keys[1], ibString(wxT("y")));
}

TEST(ComObject, ValuesCrossBothWays)
{
	EXPECT_EQ(RoundTrip(ibValue(ibNumber(42))).GetNumber(), ibNumber(42));
	EXPECT_EQ(RoundTrip(ibValue(ibNumber(1099511627776LL))).GetNumber(), ibNumber(1099511627776LL)) << "2^40 goes as VT_I8";
	EXPECT_EQ(RoundTrip(ibValue(ibNumber(ibString(wxT("12.5"))))).GetNumber(), ibNumber(ibString(wxT("12.5"))));
	EXPECT_EQ(RoundTrip(ibValue(ibString(wxT("text")))).GetString(), ibString(wxT("text")));
	EXPECT_TRUE(RoundTrip(ibValue(true)).GetBoolean());
	EXPECT_FALSE(RoundTrip(ibValue(false)).GetBoolean());

	const ibDateTime when(2026, 11, 1, 17, 30, 15, 0);
	EXPECT_EQ(RoundTrip(ibValue(when)).GetDate(), when);

	ibValue null;
	null.SetType(ibValueTypes::TYPE_NULL);
	EXPECT_EQ(RoundTrip(null).GetType(), ibValueTypes::TYPE_NULL);
	EXPECT_EQ(RoundTrip(ibValue()).GetType(), ibValueTypes::TYPE_EMPTY);

	ibValueArray* sent = new ibValueArray();
	ibValue array = sent;
	sent->Add(ibValue(ibNumber(1)));
	sent->Add(ibValue(ibString(wxT("two"))));
	ibValue back = RoundTrip(array);
	ibValueArray* received = nullptr;
	ASSERT_TRUE(back.ConvertToValue(received));
	ASSERT_EQ(received->Count(), 2u);
	EXPECT_EQ(received->Values()[0].GetNumber(), ibNumber(1));
	EXPECT_EQ(received->Values()[1].GetString(), ibString(wxT("two")));

	ibValue inner = Create(wxT("Scripting.Dictionary"));
	ibValue innerBack = RoundTrip(inner);
	EXPECT_TRUE(innerBack.CompareValueEQ(inner)) << "an object goes as its IDispatch and comes back as the same one";
}

TEST(ComObject, AServerErrorNamesTheObjectAndTheMember)
{
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("a"))), ibValue(ibNumber(1)) });
	ibString refusal;
	try {
		Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("a"))), ibValue(ibNumber(2)) });
	}
	catch (const ibBackendException& err) {
		refusal = err.GetErrorDescription();
	}
	EXPECT_NE(refusal.Find(wxT("Scripting.Dictionary")), wxNOT_FOUND) << refusal.ToStdString();
	EXPECT_NE(refusal.Find(wxT("Add")), wxNOT_FOUND) << refusal.ToStdString();
}

TEST(ComObject, AReturnedObjectIsCalledByNameToo)
{
	ibValue regExp = Create(wxT("VBScript.RegExp"));
	Set(regExp, wxT("Pattern"), ibValue(ibString(wxT("[0-9]+"))));
	Set(regExp, wxT("Global"), ibValue(true));
	EXPECT_EQ(Get(regExp, wxT("Pattern")).GetString(), ibString(wxT("[0-9]+")));

	ibValue matches = Call(regExp, wxT("Execute"), { ibValue(ibString(wxT("a1 b22 c333"))) });
	EXPECT_EQ(Get(matches, wxT("Count")).GetNumber(), ibNumber(3));

	std::shared_ptr<ibValueIteratorState> walk = matches.CreateIterator();
	ASSERT_NE(walk, nullptr);
	ibValue match;
	std::vector<ibString> found;
	while (walk->MoveNext(match))
		found.push_back(Get(match, wxT("Value")).GetString());
	ASSERT_EQ(found.size(), 3u);
	EXPECT_EQ(found[2], ibString(wxT("333")));
}

TEST(ComObject, AnObjectWithoutEventsSaysSo)
{
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	ibString refusal;
	try {
		ibValue handler;   // not even a function: the first refusal is about the handler
		Call(dictionary, wxT("AddEventHandler"), { ibValue(ibString(wxT("Changed"))), handler });
	}
	catch (const ibBackendException& err) {
		refusal = err.GetErrorDescription();
	}
	EXPECT_FALSE(refusal.IsEmpty());
}

#endif
