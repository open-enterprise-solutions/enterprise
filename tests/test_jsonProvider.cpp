////////////////////////////////////////////////////////////////////////////
//	Description : ibJsonProvider Write -> Read tests. The JSON view is LOSSY by
//	              design, so these pin down BOTH halves: what survives the trip,
//	              and — just as deliberately — what does not (see the header:
//	              Fields/Properties flatten, Date degrades to String, TypeDesc is
//	              synthetic). Pure structure layer — no DB, no metadata bring-up.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>
#include <cstring>
#include <functional>
#include <string>

#include "core/serialize/jsonProvider.h"
#include "backend/backend_exception.h"

// After wx: windows.h first would leave its macros in wx's way.
#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#else
#	include <pthread.h>
#endif

namespace {

wxMemoryBuffer Blob(const char* s) {
	wxMemoryBuffer b;
	b.AppendData(s, (size_t)strlen(s));
	return b;
}

// Emit a node as JSON text.
wxMemoryBuffer Emit(const ibDataNode& node) {
	ibWriterMemory w;
	ibJsonProvider().Write(node, w);
	return w.buffer();
}

// Parse JSON text back into a node. `lookup` maps a type NAME to a clsid — the
// inverse of the emitter's resolver; unset means a name-form NodeType stays 0.
ibDataNode Parse(const wxMemoryBuffer& text,
	std::function<ibClassID(const wxString&)> lookup = nullptr) {
	ibJsonProvider provider;
	if (lookup) provider.SetTypeLookup(std::move(lookup));
	ibReaderMemory reader(text);
	ibDataNode out;
	provider.Read(reader, out);
	return out;
}

} // namespace

// ---------------------------------------------------------------------------
// What survives
// ---------------------------------------------------------------------------

// Scalars keep their kind AND their value. Number goes through ibNumber's exact
// decimal on both sides, so a value wider than a double survives untouched.
TEST(JsonProvider, Scalars_SurviveRoundTrip) {
	ibDataNode n;
	n.AddField(wxT("name"), ibDataValue::String(wxT("RootName")));
	n.AddField(wxT("flag"), ibDataValue::Bool(true));
	n.AddField(wxT("off"),  ibDataValue::Bool(false));
	n.AddField(wxT("id"),   ibDataValue::Int(-42));
	n.AddField(wxT("wide"), ibDataValue::Number(ibNumber(wxT("123456789012345678901234567890.125"))));
	n.AddField(wxT("none"), ibDataValue());

	const ibDataNode back = Parse(Emit(n));

	EXPECT_EQ(back.GetValue<wxString>(wxT("name")), wxT("RootName"));
	EXPECT_TRUE (back.GetValue<bool>(wxT("flag")));
	EXPECT_FALSE(back.GetValue<bool>(wxT("off")));
	EXPECT_EQ(back.GetValue<s32>(wxT("id")), -42);

	const ibDataValue* wide = back.FindField(wxT("wide"));
	ASSERT_TRUE(wide != nullptr);
	EXPECT_EQ(wide->AsNumber().ToString(), wxT("123456789012345678901234567890.125"));

	const ibDataValue* none = back.FindField(wxT("none"));
	ASSERT_TRUE(none != nullptr);
	EXPECT_TRUE(none->IsEmpty());
}

// Strings carrying JSON's own metacharacters must come back byte-identical.
TEST(JsonProvider, String_EscapesSurviveRoundTrip) {
	const wxString tricky = wxT("quote:\" back\\slash tab:\t nl:\n unicode:é中");
	ibDataNode n;
	n.AddField(wxT("s"), ibDataValue::String(tricky));

	EXPECT_EQ(Parse(Emit(n)).GetValue<wxString>(wxT("s")), tricky);
}

// Binary rides as "base64:…" — the one string shape that reads back as Binary.
TEST(JsonProvider, Binary_SurvivesAsBinaryNotString) {
	ibDataNode n;
	n.AddField(wxT("blob"), ibDataValue::Binary(Blob("bytes\0with\0nulls")));

	// Hold the parsed node in a NAMED local: FindField hands back a pointer INTO it, so
	// `Parse(...).FindField(...)` would leave that pointer dangling at the end of the
	// full expression.
	const ibDataNode back = Parse(Emit(n));
	const ibDataValue* v = back.FindField(wxT("blob"));
	ASSERT_TRUE(v != nullptr);
	ASSERT_EQ(v->Kind(), ibDataKind::Binary);
	EXPECT_EQ(v->AsBinary().GetDataLen(), (size_t)strlen("bytes"));
}

// The node's own opaque remainder uses the same base64 shape under NodeRaw.
TEST(JsonProvider, RawData_SurvivesRoundTrip) {
	ibDataNode n;
	n.SetRawData(Blob("ROOT-DATA"));

	const ibDataNode back = Parse(Emit(n));
	ASSERT_TRUE(back.HasRawData());
	ASSERT_EQ(back.RawData().GetDataLen(), strlen("ROOT-DATA"));
	EXPECT_EQ(0, memcmp(back.RawData().GetData(), "ROOT-DATA", strlen("ROOT-DATA")));
}

// Arrays keep order and per-item kind, including nested objects.
TEST(JsonProvider, Array_KeepsOrderAndKinds) {
	std::vector<ibDataValue> items;
	items.push_back(ibDataValue::Int(1));
	items.push_back(ibDataValue::String(wxT("two")));
	items.push_back(ibDataValue::Bool(true));

	ibDataNode n;
	n.AddField(wxT("list"), ibDataValue::Array(items));
	n.AddField(wxT("empty"), ibDataValue::Array({}));

	const ibDataNode back = Parse(Emit(n));   // named — FindField points into it

	const ibDataValue* list = back.FindField(wxT("list"));
	ASSERT_TRUE(list != nullptr);
	ASSERT_EQ(list->AsArray().size(), (size_t)3);
	EXPECT_EQ(list->AsArray()[0].AsInt(), (s64)1);
	EXPECT_EQ(list->AsArray()[1].AsString(), wxT("two"));
	EXPECT_TRUE(list->AsArray()[2].AsBool());

	const ibDataValue* empty = back.FindField(wxT("empty"));
	ASSERT_TRUE(empty != nullptr);
	EXPECT_TRUE(empty->AsArray().empty());
}

// Metaobject children recurse, keep order, and keep their identity — the numeric
// NodeType form needs no lookup, so clsid survives on its own.
TEST(JsonProvider, Children_RecurseWithIdentity) {
	ibDataNode root(1001, 1);
	root.AddChild(2002, 2).SetRawData(Blob("child-A"));
	root.AddChild(2002, 3).SetRawData(Blob("child-B"));
	root.Children()[0].AddChild(3003, 4).SetRawData(Blob("grandchild"));

	const ibDataNode back = Parse(Emit(root));

	EXPECT_EQ(back.GetMetaId(), (ibMetaID)1);
	ASSERT_EQ(back.Children().size(), (size_t)2);
	EXPECT_EQ(back.Children()[0].GetMetaId(), (ibMetaID)2);
	EXPECT_EQ(back.Children()[1].GetMetaId(), (ibMetaID)3);
	ASSERT_EQ(back.Children()[0].Children().size(), (size_t)1);
	EXPECT_EQ(back.Children()[0].Children()[0].GetMetaId(), (ibMetaID)4);

	const wxMemoryBuffer& rd = back.Children()[1].RawData();
	ASSERT_EQ(rd.GetDataLen(), strlen("child-B"));
	EXPECT_EQ(0, memcmp(rd.GetData(), "child-B", rd.GetDataLen()));
}

// A Child value (ibDataNode::Child — a composite, stored in the PROPERTY area)
// comes back into the property area, so FindChild finds it again.
TEST(JsonProvider, ChildValue_ReturnsToPropertyArea) {
	ibDataNode n(1001, 1);
	n.Child(wxT("Type")).AddField(wxT("typeName"), ibDataValue::String(wxT("String")));

	const ibDataNode back = Parse(Emit(n));
	const ibDataNode* sub = back.FindChild(wxT("Type"));
	ASSERT_TRUE(sub != nullptr);
	EXPECT_EQ(sub->GetValue<wxString>(wxT("typeName")), wxT("String"));
}

// A name-form NodeType resolves only through the injected inverse lookup.
TEST(JsonProvider, NodeType_NameFormNeedsLookup) {
	ibJsonProvider provider;
	provider.SetTypeResolver([](ibClassID) { return wxString(wxT("Catalog")); });
	ibWriterMemory w;
	ibDataNode n(1001, 1);
	provider.Write(n, w);

	// no lookup -> the name cannot become a number again
	EXPECT_EQ(Parse(w.buffer()).GetClsid(), (ibClassID)0);

	// with the inverse -> identity restored
	const ibDataNode back = Parse(w.buffer(),
		[](const wxString& name) -> ibClassID { return name == wxT("Catalog") ? 1001 : 0; });
	EXPECT_EQ(back.GetClsid(), (ibClassID)1001);
}

// ---------------------------------------------------------------------------
// What deliberately does NOT survive — pinned so a future change is a decision,
// not an accident.
// ---------------------------------------------------------------------------

// Date is emitted as a readable ISO string and reads back as a String: on the
// wire it is indistinguishable from a string that looks like a date.
TEST(JsonProvider, Date_DegradesToString_ByDesign) {
	ibDataNode n;
	n.AddField(wxT("when"), ibDataValue::Date(ibDateTime(2023, 11, 14, 22, 13, 20)));

	const ibDataNode back = Parse(Emit(n));   // named — FindField points into it
	const ibDataValue* v = back.FindField(wxT("when"));
	ASSERT_TRUE(v != nullptr);
	EXPECT_EQ(v->Kind(), ibDataKind::String);
	EXPECT_FALSE(v->AsString().IsEmpty());
}

// Fields and Properties flatten into one key set, so a scalar PROPERTY comes back
// as a FIELD. (A Child property is the exception — see the test above.)
TEST(JsonProvider, ScalarProperty_ComesBackAsField_ByDesign) {
	ibDataNode n;
	n.SetProperty(wxT("Name"), ibDataValue::String(wxT("Nomenclature")));

	const ibDataNode back = Parse(Emit(n));
	EXPECT_TRUE(back.FindProperty(wxT("Name")) == nullptr);
	const ibDataValue* asField = back.FindField(wxT("Name"));
	ASSERT_TRUE(asField != nullptr);
	EXPECT_EQ(asField->AsString(), wxT("Nomenclature"));
}

// TypeDesc is injected beside a typeId with no node entry behind it — the parser
// drops it rather than inventing a key the writer never had.
TEST(JsonProvider, TypeDesc_IsDroppedNotMaterialised) {
	ibJsonProvider provider;
	provider.SetTypeResolver([](ibClassID) { return wxString(wxT("String")); });
	ibDataNode n;
	n.AddField(wxT("typeId"), ibDataValue::UInt(777));
	ibWriterMemory w;
	provider.Write(n, w);

	const ibDataNode back = Parse(w.buffer());
	EXPECT_TRUE(back.FindField(wxT("TypeDesc")) == nullptr);
	EXPECT_TRUE(back.FindProperty(wxT("TypeDesc")) == nullptr);
	const ibDataValue* typeId = back.FindField(wxT("typeId"));
	ASSERT_TRUE(typeId != nullptr);
	EXPECT_EQ(typeId->AsUInt(), (u64)777);
}

// ---------------------------------------------------------------------------
// Failure is loud
// ---------------------------------------------------------------------------

// Malformed input throws rather than handing back a half-built tree.
TEST(JsonProvider, MalformedInput_Throws) {
	wxMemoryBuffer bad;
	const char* text = "{ \"a\": [1, 2 ";
	bad.AppendData(text, strlen(text));

	ibReaderMemory reader(bad);
	ibDataNode out;
	EXPECT_THROW(ibJsonProvider().Read(reader, out), ibCoreException);
}

// An empty buffer is "nothing to read", not a parse error.
TEST(JsonProvider, EmptyInput_ReturnsFalse) {
	wxMemoryBuffer empty;
	ibReaderMemory reader(empty);
	ibDataNode out;
	EXPECT_FALSE(ibJsonProvider().Read(reader, out));
}

// ---------------------------------------------------------------------------
// Nesting. The reader recurses, and a request is parsed before authentication.
// A few thousand brackets overflow the stack (measured on a Linux Debug build
// of this reader, N ≈ 5000–6000, about 12 KB). The ceiling is one number, and
// both containers count: the root object is one, each nested array or object
// is one more.
// ---------------------------------------------------------------------------

namespace {

wxMemoryBuffer Text(const std::string& text)
{
	wxMemoryBuffer buf;
	buf.AppendData(text.data(), text.size());
	return buf;
}

// A root object whose field "v" is `arrays` nested arrays around the number 1.
wxMemoryBuffer NestedArrays(int arrays)
{
	std::string text;
	text.reserve(static_cast<size_t>(arrays) * 2 + 16);
	text += "{\"v\":";
	text.append(static_cast<size_t>(arrays), '[');
	text += '1';
	text.append(static_cast<size_t>(arrays), ']');
	text += '}';
	return Text(text);
}

// `levels` nested objects, the outermost being the root. The innermost field is n = 1.
wxMemoryBuffer NestedObjects(int levels)
{
	std::string text;
	text.reserve(static_cast<size_t>(levels) * 8);
	for (int i = 1; i < levels; ++i)
		text += "{\"c\":";
	text += "{\"n\":1}";
	text.append(static_cast<size_t>(levels > 0 ? levels - 1 : 0), '}');
	return Text(text);
}

// Runs `body` on a thread whose stack is `bytes`, and waits for it.
void RunOnStack(size_t bytes, const std::function<void()>& body)
{
#if defined(_WIN32)
	struct Call { const std::function<void()>* body; };
	Call call{ &body };
	HANDLE thread = ::CreateThread(nullptr, bytes,
		[](LPVOID arg) -> DWORD { (*static_cast<Call*>(arg)->body)(); return 0; },
		&call, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
	ASSERT_NE(thread, nullptr);
	::WaitForSingleObject(thread, INFINITE);
	::CloseHandle(thread);
#else
	pthread_attr_t attr;
	ASSERT_EQ(pthread_attr_init(&attr), 0);
	ASSERT_EQ(pthread_attr_setstacksize(&attr, bytes), 0);
	pthread_t thread;
	auto entry = [](void* arg) -> void* { (*static_cast<const std::function<void()>*>(arg))(); return nullptr; };
	ASSERT_EQ(pthread_create(&thread, &attr, entry, const_cast<std::function<void()>*>(&body)), 0);
	pthread_join(thread, nullptr);
	pthread_attr_destroy(&attr);
#endif
}

const ibDataValue* InnermostArray(const ibDataNode& node, int arrays)
{
	const ibDataValue* v = node.FindField(wxT("v"));
	for (int i = 0; i < arrays; ++i) {
		if (v == nullptr || v->Kind() != ibDataKind::Array || v->AsArray().size() != 1)
			return nullptr;
		v = &v->AsArray()[0];
	}
	return v;
}

} // namespace

// The deepest legal text is still a value: the leaf is the number that was written.
TEST(JsonProvider, Nesting_AtTheLimit_IsRead) {
	// Root plus (limit - 1) arrays is exactly `limit` containers.
	const int limit = ibJsonProvider::kMaxNesting;
	const ibDataNode arrays = Parse(NestedArrays(limit - 1));
	const ibDataValue* leaf = InnermostArray(arrays, limit - 1);
	ASSERT_NE(leaf, nullptr);
	EXPECT_EQ(leaf->Kind(), ibDataKind::Number);
	EXPECT_EQ(leaf->AsInt(), (s64)1);

	const ibDataNode objects = Parse(NestedObjects(limit));
	const ibDataNode* inner = &objects;
	for (int i = 1; i < limit; ++i) {
		inner = inner->FindChild(wxT("c"));
		ASSERT_NE(inner, nullptr);
	}
	EXPECT_EQ(inner->GetValue<s32>(wxT("n")), 1);
}

// One container past the ceiling is a parse error, not a deeper call.
TEST(JsonProvider, Nesting_OnePastTheLimit_IsRefused) {
	const int limit = ibJsonProvider::kMaxNesting;
	EXPECT_THROW(Parse(NestedArrays(limit)), ibCoreException);
	EXPECT_THROW(Parse(NestedObjects(limit + 1)), ibCoreException);
}

// The request that took the application server down: thousands of brackets, a few kilobytes.
TEST(JsonProvider, Nesting_TheReportedCrashDepth_IsRefused) {
	EXPECT_THROW(Parse(NestedArrays(8000)), ibCoreException);
}

// Two containers at the ceiling side by side. A level that was not released on the way out
// would count the second one as deeper and refuse it.
TEST(JsonProvider, Nesting_SiblingsAtTheLimit_AreBothRead) {
	const int arrays = ibJsonProvider::kMaxNesting - 1;
	std::string text = "{\"a\":";
	for (int side = 0; side < 2; ++side) {
		if (side == 1) text += ",\"b\":";
		text.append(static_cast<size_t>(arrays), '[');
		text += '1';
		text.append(static_cast<size_t>(arrays), ']');
	}
	text += '}';
	const ibDataNode node = Parse(Text(text));
	EXPECT_NE(node.FindField(wxT("a")), nullptr);
	EXPECT_NE(node.FindField(wxT("b")), nullptr);
}

// The deepest legal text on the stack a worker thread has on macOS (512 KB). Each level moves
// its subtree up rather than copying it, so the depth costs the descent and nothing more.
TEST(JsonProvider, Nesting_AtTheLimit_FitsASmallThreadStack) {
	bool read = false;
	RunOnStack(512 * 1024, [&read] {
		try {
			const ibDataNode node = Parse(NestedArrays(ibJsonProvider::kMaxNesting - 1));
			read = InnermostArray(node, ibJsonProvider::kMaxNesting - 1) != nullptr;
		}
		catch (...) {
			read = false;
		}
	});
	EXPECT_TRUE(read);
}
