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
//
// ComSafeArray — an array as COM holds it: made by element type and sizes or from an Array, read and written
// by its own indices, and what every array a COM object returns comes back as.
// =============================================================================

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueBinaryData.h"
#include "backend/system/value/valueCOM.h"
#include "backend/system/value/valueSafeArray.h"

#ifdef __IBMSW__
#include <objbase.h>   // CoInitializeEx — the test thread's COM apartment
#endif

namespace {

#ifdef __IBMSW__
// The test's thread is in a single-threaded COM apartment, entered by its owner — the test — as a session's worker is
// by the pool (workerPoolHeadless.cpp). The object no longer enters one itself.
class ibComApartmentEnvironment : public ::testing::Environment {
public:
	virtual void SetUp() override { m_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
	virtual void TearDown() override { if (SUCCEEDED(m_hr)) ::CoUninitialize(); }

private:
	HRESULT m_hr = E_FAIL;
};

::testing::Environment* const s_comApartment = ::testing::AddGlobalTestEnvironment(new ibComApartmentEnvironment);
#endif

// New <typeName>(args), the way a script's New reaches it. A refusal comes back as its text.
ibValue Make(const ibString& typeName, std::vector<ibValue> args, ibString* refusal = nullptr)
{
	std::vector<ibValue*> params;
	for (ibValue& arg : args)
		params.push_back(&arg);
	try {
		return ibValue::CreateObject(typeName, params.data(), static_cast<long>(params.size()));
	}
	catch (const ibBackendException& err) {
		if (refusal != nullptr)
			*refusal = err.GetErrorDescription();
	}
	return ibValue();
}

// New COMObject(progId).
ibValue Create(const ibString& progId, ibString* refusal = nullptr)
{
	return Make(wxT("COMObject"), { ibValue(progId) }, refusal);
}

#ifdef __IBMSW__

// A call the way the interpreter makes it: a procedure through CallAsProc, a function through CallAsFunc
// (procUnit.cpp, OPER_CALL_METHOD). A procedure answered only CallAsFunc once, and did nothing from a module.
ibValue Call(ibValue& object, const ibString& method, std::vector<ibValue> args = {})
{
	const long id = object.FindMethod(method);
	EXPECT_GE(id, 0) << method.ToStdString();
	std::vector<ibValue*> params;
	for (ibValue& arg : args)
		params.push_back(&arg);
	ibValue result;
	if (object.HasRetVal(id))
		object.CallAsFunc(id, result, params.empty() ? nullptr : params.data(), static_cast<long>(params.size()));
	else
		EXPECT_TRUE(object.CallAsProc(id, params.empty() ? nullptr : params.data(), static_cast<long>(params.size())))
			<< "the procedure " << method.ToStdString() << " was not done";
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

// What a call is refused with — empty when it is not.
template <typename F>
ibString RefusalOf(F&& call)
{
	try {
		call();
	}
	catch (const ibBackendException& err) {
		return err.GetErrorDescription();
	}
	return ibString();
}

ibValue ArrayOf(std::initializer_list<ibValue> items)
{
	const ibValuePtr<ibValueArray> arr(new ibValueArray());
	for (const ibValue& item : items)
		arr->Add(item);
	return arr;
}

ibValue Number(const int n)
{
	return ibValue(ibNumber(n));
}

// [[1, 2, 3], [4, 5, 6]] — what a spreadsheet's Range.Value takes.
ibValue TwoRowsOfThree()
{
	return ArrayOf({ ArrayOf({ Number(1), Number(2), Number(3) }), ArrayOf({ Number(4), Number(5), Number(6) }) });
}

ibValue SafeArray(std::vector<ibValue> args, ibString* refusal = nullptr)
{
	return Make(wxT("ComSafeArray"), std::move(args), refusal);
}

#endif

} // namespace

TEST(ComObject, TheTypeIsRegisteredOnEveryPlatform)
{
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("COMObject"), ibCtorObjectType::ibCtorObjectType_object_value))
		<< "names are case-insensitive: COMObject is the registered ComObject";
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("COMSafeArray"), ibCtorObjectType::ibCtorObjectType_object_value));
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

#ifndef __IBMSW__

TEST(ComObject, CreatingOneElsewhereIsRefusedAtNew)
{
	ibString refusal;
	const ibValue made = Create(wxT("Scripting.Dictionary"), &refusal);
	EXPECT_TRUE(made.IsEmpty());
	EXPECT_EQ(refusal, ibString(_("COM objects are available on Windows only")));
}

TEST(ComSafeArray, MakingOneElsewhereIsRefusedAtNew)
{
	ibString refusal;
	Make(wxT("ComSafeArray"), { ibValue(ibString(wxT("VT_I4"))), ibValue(ibNumber(2)) }, &refusal);
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
	ASSERT_EQ(dictionary.GetType(), ibValueTypes::TYPE_OLE);

	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("a"))), ibValue(ibNumber(1)) });
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("b"))), ibValue(ibNumber(2)) });

	EXPECT_EQ(Get(dictionary, wxT("Count")).GetNumber(), ibNumber(2));
	EXPECT_EQ(Call(dictionary, wxT("Item"), { ibValue(ibString(wxT("b"))) }).GetNumber(), ibNumber(2));
	EXPECT_TRUE(Call(dictionary, wxT("Exists"), { ibValue(ibString(wxT("a"))) }).GetBoolean());
	EXPECT_FALSE(Call(dictionary, wxT("Exists"), { ibValue(ibString(wxT("z"))) }).GetBoolean());
}

TEST(ComObject, AnArrayComesBackAsAComSafeArray)
{
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("a"))), ibValue(ibNumber(1)) });
	Call(dictionary, wxT("Add"), { ibValue(ibString(wxT("b"))), ibValue(ibNumber(2)) });

	ibValue keys = Call(dictionary, wxT("Keys"));
	ibValueSafeArray* safeArray = nullptr;
	ASSERT_TRUE(keys.ConvertToValue(safeArray));
	EXPECT_EQ(Call(keys, wxT("GetDimensions")).GetNumber(), ibNumber(1));
	EXPECT_EQ(Call(keys, wxT("GetType")).GetString(), ibString(wxT("VT_VARIANT")));
	EXPECT_EQ(Call(keys, wxT("GetValue"), { Number(1) }).GetString(), ibString(wxT("b")));

	ibValue unloaded = Call(keys, wxT("Unload"));
	ibValueArray* arr = nullptr;
	ASSERT_TRUE(unloaded.ConvertToValue(arr));
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
	const ibNumber precise(ibString(wxT("12345678901234.123456")));
	EXPECT_EQ(RoundTrip(ibValue(precise)).GetNumber(), precise) << "more digits than a double keeps go as a DECIMAL";
	EXPECT_EQ(RoundTrip(ibValue(ibString(wxT("text")))).GetString(), ibString(wxT("text")));
	EXPECT_TRUE(RoundTrip(ibValue(true)).GetBoolean());
	EXPECT_FALSE(RoundTrip(ibValue(false)).GetBoolean());

	const ibDateTime when(2026, 11, 1, 17, 30, 15, 0);
	EXPECT_EQ(RoundTrip(ibValue(when)).GetDate(), when);

	ibValue null;
	null.SetType(ibValueTypes::TYPE_NULL);
	EXPECT_EQ(RoundTrip(null).GetType(), ibValueTypes::TYPE_NULL);
	EXPECT_EQ(RoundTrip(ibValue()).GetType(), ibValueTypes::TYPE_EMPTY);

	ibValue back = RoundTrip(ArrayOf({ Number(1), ibValue(ibString(wxT("two"))) }));
	ibValue unloaded = Call(back, wxT("Unload"));
	ibValueArray* received = nullptr;
	ASSERT_TRUE(unloaded.ConvertToValue(received));
	ASSERT_EQ(received->Count(), 2u);
	EXPECT_EQ(received->Values()[0].GetNumber(), ibNumber(1));
	EXPECT_EQ(received->Values()[1].GetString(), ibString(wxT("two")));

	ibValue inner = Create(wxT("Scripting.Dictionary"));
	ibValue innerBack = RoundTrip(inner);
	EXPECT_TRUE(innerBack.CompareValueEQ(inner)) << "an object goes as its IDispatch and comes back as the same one";
}

// A VT_R8 comes back as the decimal it stands for, a VT_R4 at a float's own digits: through the double's seventeen
// digits 0.43 read 0.42999999999999999, and a float's 0.1 read 0.100000001490116 even at fifteen (2026-11-05).
TEST(ComObject, AFloatingValueComesBackAsItsDecimal)
{
	const ibNumber fraction(ibString(wxT("0.43")));
	const ibNumber back = RoundTrip(ibValue(fraction)).GetNumber();
	EXPECT_EQ(back, fraction) << "goes as VT_R8, came back " << back.ToString();

	VARIANT single;
	::VariantInit(&single);
	single.vt = VT_R4;
	single.fltVal = 0.1f;
	ibValue tenth;
	ibValueCOM::FromVariant(single, tenth, wxT("single"));
	EXPECT_EQ(tenth.GetNumber(), ibNumber(ibString(wxT("0.1")))) << tenth.GetNumber().ToString();
}

TEST(ComObject, AnArgumentDeclaredInIsNotWrittenBack)
{
	// Scripting.Dictionary.Add takes `[in] VARIANT* Item`: a pointer, but no output.
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	ibValue table = TwoRowsOfThree();
	const long id = dictionary.FindMethod(wxT("Add"));
	ibValue key(ibString(wxT("t")));
	ibValue* params[] = { &key, &table };
	ibValue unused;
	dictionary.CallAsFunc(id, unused, params, 2);
	ibValueArray* stillAnArray = nullptr;
	EXPECT_TRUE(table.ConvertToValue(stillAnArray)) << "the script's Array stays its Array, not the dictionary's copy";
}

TEST(ComObject, BytesGoAsAnArrayOfBytes)
{
	const unsigned char sent[] = { 0x00, 0x7F, 0x80, 0xFF };
	ibValue back = RoundTrip(ibValue(new ibValueBinaryData(sent, sizeof(sent))));
	EXPECT_EQ(Call(back, wxT("GetType")).GetString(), ibString(wxT("VT_UI1"))) << "BinaryData goes as VT_ARRAY | VT_UI1";
	ASSERT_EQ(Call(back, wxT("GetLength"), { Number(0) }).GetNumber(), ibNumber(4));
	for (int i = 0; i < 4; i++)
		EXPECT_EQ(Call(back, wxT("GetValue"), { Number(i) }).GetNumber(), ibNumber(sent[i]));
}

TEST(ComObject, RowsOfOneLengthGoAsATable)
{
	// One SAFEARRAY of two dimensions, the rows the left-most.
	VARIANT sent;
	ibValueCOM::FromValue(TwoRowsOfThree(), sent);
	ASSERT_EQ(sent.vt, VT_ARRAY | VT_VARIANT);
	ASSERT_EQ(::SafeArrayGetDim(sent.parray), 2u);
	LONG lastRow = 0, lastColumn = 0;
	::SafeArrayGetUBound(sent.parray, 1, &lastRow);
	::SafeArrayGetUBound(sent.parray, 2, &lastColumn);
	EXPECT_EQ(lastRow, 1) << "the left-most dimension is the rows";
	EXPECT_EQ(lastColumn, 2);
	LONG at[] = { 1, 2 };
	VARIANT six;
	::VariantInit(&six);
	EXPECT_EQ(::SafeArrayGetElement(sent.parray, at, &six), S_OK);
	EXPECT_EQ(six.vt, VT_I4);
	EXPECT_EQ(six.lVal, 6);
	::VariantClear(&six);

	ibValue back;
	ibValueCOM::FromVariant(sent, back, wxT("table"));
	::VariantClear(&sent);
	EXPECT_EQ(Call(back, wxT("GetValue"), { Number(1), Number(2) }).GetNumber(), ibNumber(6));
	ibValue unloaded = Call(back, wxT("Unload"));
	ibValueArray* rows = nullptr;
	ASSERT_TRUE(unloaded.ConvertToValue(rows));
	ASSERT_EQ(rows->Count(), 2u);
	ibValueArray* second = nullptr;
	ASSERT_TRUE(rows->Values()[1].ConvertToValue(second));
	ASSERT_EQ(second->Count(), 3u);
	EXPECT_EQ(second->Values()[2].GetNumber(), ibNumber(6));
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

TEST(ComObject, AHandlerByAnUnknownProcedureNameIsRefusedWhereItIsWritten)
{
	ibValue dictionary = Create(wxT("Scripting.Dictionary"));
	ibString refusal;
	try {
		Call(dictionary, wxT("AddEventHandler"), { ibValue(ibString(wxT("Changed"))), ibValue(ibString(wxT("NoSuchProcedure636"))) });
	}
	catch (const ibBackendException& err) {
		refusal = err.GetErrorDescription();
	}
	EXPECT_NE(refusal.Find(wxT("NoSuchProcedure636")), wxNOT_FOUND)
		<< "a name is checked when the handler is given, not when the event comes: " << refusal.ToStdString();
}

TEST(ComSafeArray, MadeByElementTypeAndSizes)
{
	ibValue table = SafeArray({ ibValue(ibString(wxT("VT_I4"))), Number(2), Number(3) });
	ASSERT_FALSE(table.IsEmpty());
	EXPECT_EQ(Call(table, wxT("GetDimensions")).GetNumber(), ibNumber(2));
	EXPECT_EQ(Call(table, wxT("GetLength"), { Number(0) }).GetNumber(), ibNumber(2));
	EXPECT_EQ(Call(table, wxT("GetLength"), { Number(1) }).GetNumber(), ibNumber(3));
	EXPECT_EQ(Call(table, wxT("GetLowerBound"), { Number(1) }).GetNumber(), ibNumber(0));
	EXPECT_EQ(Call(table, wxT("GetUpperBound"), { Number(1) }).GetNumber(), ibNumber(2));
	EXPECT_EQ(Call(table, wxT("GetType")).GetString(), ibString(wxT("VT_I4")));
	EXPECT_EQ(Call(table, wxT("GetValue"), { Number(1), Number(2) }).GetNumber(), ibNumber(0)) << "made empty: zero";

	ibValue sizes = SafeArray({ ibValue(ibString(wxT("vt_variant"))), ArrayOf({ Number(4) }) });
	EXPECT_EQ(Call(sizes, wxT("GetLength"), { Number(0) }).GetNumber(), ibNumber(4)) << "the sizes as one Array";
	EXPECT_EQ(Call(sizes, wxT("GetValue"), { Number(3) }).GetType(), ibValueTypes::TYPE_EMPTY);
}

TEST(ComSafeArray, AValueIsStoredAsTheElementType)
{
	ibValue numbers = SafeArray({ ibValue(ibString(wxT("VT_I4"))), Number(2) });
	Call(numbers, wxT("SetValue"), { Number(0), Number(23) });
	Call(numbers, wxT("SetValue"), { Number(1), ibValue(ibNumber(ibString(wxT("13.5")))) });
	EXPECT_EQ(Call(numbers, wxT("GetValue"), { Number(0) }).GetNumber(), ibNumber(23));
	EXPECT_EQ(Call(numbers, wxT("GetValue"), { Number(1) }).GetNumber(), ibNumber(14)) << "13.5 as VT_I4, rounded to even";

	const ibString refusal = RefusalOf([&]() { Call(numbers, wxT("SetValue"), { Number(0), ibValue(ibString(wxT("abc"))) }); });
	EXPECT_NE(refusal.Find(wxT("VT_I4")), wxNOT_FOUND) << refusal.ToStdString();
}

TEST(ComSafeArray, MadeFromAnArray)
{
	ibValue table = SafeArray({ TwoRowsOfThree(), ibValue(ibString(wxT("VT_R8"))), ArrayOf({ Number(2), Number(3) }) });
	EXPECT_EQ(Call(table, wxT("GetType")).GetString(), ibString(wxT("VT_R8")));
	EXPECT_EQ(Call(table, wxT("GetValue"), { Number(1), Number(0) }).GetNumber(), ibNumber(4));

	ibValue ownShape = SafeArray({ TwoRowsOfThree(), ibValue(ibString(wxT("VT_I2"))) });
	EXPECT_EQ(Call(ownShape, wxT("GetDimensions")).GetNumber(), ibNumber(2)) << "no sizes: the array's own shape";
	EXPECT_EQ(Call(ownShape, wxT("GetValue"), { Number(0), Number(2) }).GetNumber(), ibNumber(3));

	ibString refusal;
	SafeArray({ TwoRowsOfThree(), ibValue(ibString(wxT("VT_I4"))), Number(3), Number(2) }, &refusal);
	EXPECT_FALSE(refusal.IsEmpty()) << "two rows of three are not three rows of two";
}

TEST(ComSafeArray, AnIndexIsTheArraysOwn)
{
	ibValue table = SafeArray({ ibValue(ibString(wxT("VT_I4"))), Number(2), Number(3) });
	EXPECT_FALSE(RefusalOf([&]() { Call(table, wxT("GetValue"), { Number(1) }); }).IsEmpty()) << "one index for two dimensions";
	EXPECT_EQ(RefusalOf([&]() { Call(table, wxT("GetValue"), { Number(2), Number(0) }); }), ibString(_("Index goes beyond array")));
	EXPECT_FALSE(RefusalOf([&]() { Call(table, wxT("GetLength"), { Number(2) }); }).IsEmpty()) << "there is no dimension 2";

	ibString refusal;
	SafeArray({ ibValue(ibString(wxT("VT_NOPE"))), Number(1) }, &refusal);
	EXPECT_NE(refusal.Find(wxT("VT_NOPE")), wxNOT_FOUND) << refusal.ToStdString();
}

TEST(ComSafeArray, ResizeChangesTheRightMostDimension)
{
	ibValue table = SafeArray({ ibValue(ibString(wxT("VT_I4"))), Number(2), Number(3) });
	Call(table, wxT("SetValue"), { Number(1), Number(2), Number(7) });
	EXPECT_TRUE(Call(table, wxT("IsResizable")).GetBoolean());

	Call(table, wxT("Resize"), { Number(5) });
	EXPECT_EQ(Call(table, wxT("GetLength"), { Number(0) }).GetNumber(), ibNumber(2));
	EXPECT_EQ(Call(table, wxT("GetLength"), { Number(1) }).GetNumber(), ibNumber(5));
	EXPECT_EQ(Call(table, wxT("GetValue"), { Number(1), Number(2) }).GetNumber(), ibNumber(7)) << "what was there stays";
}

TEST(ComSafeArray, GoesToAMethodAsItIs)
{
	ibValue numbers = SafeArray({ ArrayOf({ Number(5), Number(6) }), ibValue(ibString(wxT("VT_I4"))) });

	VARIANT sent;
	ibValueCOM::FromValue(numbers, sent);
	EXPECT_EQ(sent.vt, VT_ARRAY | VT_I4) << "its own element type, not VARIANTs";
	::VariantClear(&sent);

	ibValue back = RoundTrip(numbers);
	EXPECT_EQ(Call(back, wxT("GetType")).GetString(), ibString(wxT("VT_I4")));
	EXPECT_EQ(Call(back, wxT("GetValue"), { Number(1) }).GetNumber(), ibNumber(6));
}

TEST(ComSafeArray, ACopyIsItsOwn)
{
	ibValue source = SafeArray({ ibValue(ibString(wxT("VT_BSTR"))), Number(1) });
	Call(source, wxT("SetValue"), { Number(0), ibValue(ibString(wxT("before"))) });
	ibValue copy = SafeArray({ source });
	Call(copy, wxT("SetValue"), { Number(0), ibValue(ibString(wxT("after"))) });
	EXPECT_EQ(Call(source, wxT("GetValue"), { Number(0) }).GetString(), ibString(wxT("before")));
	EXPECT_EQ(Call(copy, wxT("GetValue"), { Number(0) }).GetString(), ibString(wxT("after")));
}

#endif
