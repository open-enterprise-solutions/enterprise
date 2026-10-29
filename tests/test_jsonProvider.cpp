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

#include "core/serialize/provider/jsonProvider.h"
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

ibMemoryBuffer Blob(const char* s) {
	return ibMemoryBuffer(s, strlen(s));
}

// Emit a node as JSON text.
ibMemoryBuffer Emit(const ibDataNode& node) {
	ibWriterMemory w;
	ibJsonProvider().Write(node, w);
	return w.buffer();
}

// Parse JSON text back into a node. `lookup` maps a type NAME to a clsid — the
// inverse of the emitter's resolver; unset means a name-form NodeType stays 0.
ibDataNode Parse(const ibMemoryBuffer& text,
	std::function<ibClassID(const ibString&)> lookup = nullptr) {
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

	EXPECT_EQ(back.GetValue<ibString>(ibT("name")), wxT("RootName"));
	EXPECT_TRUE (back.GetValue<bool>(ibT("flag")));
	EXPECT_FALSE(back.GetValue<bool>(ibT("off")));
	EXPECT_EQ(back.GetValue<std::int32_t>(ibT("id")), -42);

	auto wide = back.FindField(ibT("wide"));
	ASSERT_TRUE(wide);
	EXPECT_EQ(wide->AsNumber().ToString(), wxT("123456789012345678901234567890.125"));

	auto none = back.FindField(ibT("none"));
	ASSERT_TRUE(none);
	EXPECT_TRUE(none->IsEmpty());
}

// Strings carrying JSON's own metacharacters must come back byte-identical.
TEST(JsonProvider, String_EscapesSurviveRoundTrip) {
	const ibString tricky = wxT("quote:\" back\\slash tab:\t nl:\n unicode:é中");
	ibDataNode n;
	n.AddField(wxT("s"), ibDataValue::String(tricky));

	EXPECT_EQ(Parse(Emit(n)).GetValue<ibString>(ibT("s")), tricky);
}

// Binary rides as "base64:…" — the one string shape that reads back as Binary.
TEST(JsonProvider, Binary_SurvivesAsBinaryNotString) {
	ibDataNode n;
	n.AddField(wxT("blob"), ibDataValue::Binary(Blob("bytes\0with\0nulls")));

	// Hold the parsed node in a NAMED local: FindField hands back a pointer INTO it, so
	// `Parse(...).FindField(...)` would leave that pointer dangling at the end of the
	// full expression.
	const ibDataNode back = Parse(Emit(n));
	auto v = back.FindField(ibT("blob"));
	ASSERT_TRUE(v);
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

	auto list = back.FindField(ibT("list"));
	ASSERT_TRUE(list);
	ASSERT_EQ(list->AsArray().size(), (size_t)3);
	EXPECT_EQ(list->AsArray()[0].AsInt(), (std::int64_t)1);
	EXPECT_EQ(list->AsArray()[1].AsString(), wxT("two"));
	EXPECT_TRUE(list->AsArray()[2].AsBool());

	auto empty = back.FindField(ibT("empty"));
	ASSERT_TRUE(empty);
	EXPECT_TRUE(empty->AsArray().empty());
}

// Metaobject children recurse, keep order, and keep their identity — the numeric
// NodeType form needs no lookup, so clsid survives on its own.
TEST(JsonProvider, Children_RecurseWithIdentity) {
	ibDataNode root(1001, 1);
	root.AddChild(2002, 2).SetRawData(Blob("child-A"));
	root.AddChild(2002, 3).SetRawData(Blob("child-B"));
	root.Handle(root.Children()[0]).AddChild(3003, 4).SetRawData(Blob("grandchild"));

	const ibDataNode back = Parse(Emit(root));

	EXPECT_EQ(back.GetMetaId(), (ibMetaID)1);
	ASSERT_EQ(back.Children().size(), (size_t)2);
	EXPECT_EQ(back.Children()[0].GetMetaId(), (ibMetaID)2);
	EXPECT_EQ(back.Children()[1].GetMetaId(), (ibMetaID)3);
	ASSERT_EQ(back.Children()[0].Children().size(), (size_t)1);
	EXPECT_EQ(back.Children()[0].Children()[0].GetMetaId(), (ibMetaID)4);

	const ibMemoryBuffer& rd = back.Children()[1].RawData();
	ASSERT_EQ(rd.GetDataLen(), strlen("child-B"));
	EXPECT_EQ(0, memcmp(rd.GetData(), "child-B", rd.GetDataLen()));
}

// A Child value (ibDataNode::Child — a composite, stored in the PROPERTY area)
// comes back into the property area, so FindChild finds it again.
TEST(JsonProvider, ChildValue_ReturnsToPropertyArea) {
	ibDataNode n(1001, 1);
	n.Child(ibT("Type")).AddField(wxT("typeName"), ibDataValue::String(wxT("String")));

	const ibDataNode back = Parse(Emit(n));
	auto sub = back.FindChild(ibT("Type"));
	ASSERT_TRUE(sub);
	EXPECT_EQ(sub->GetValue<ibString>(ibT("typeName")), wxT("String"));
}

// A name-form NodeType resolves only through the injected inverse lookup.
TEST(JsonProvider, NodeType_NameFormNeedsLookup) {
	ibJsonProvider provider;
	provider.SetTypeResolver([](ibClassID) { return ibString(wxT("Catalog")); });
	ibWriterMemory w;
	ibDataNode n(1001, 1);
	provider.Write(n, w);

	// no lookup -> the name cannot become a number again
	EXPECT_EQ(Parse(w.buffer()).GetClsid(), (ibClassID)0);

	// with the inverse -> identity restored
	const ibDataNode back = Parse(w.buffer(),
		[](const ibString& name) -> ibClassID { return name == wxT("Catalog") ? 1001 : 0; });
	EXPECT_EQ(back.GetClsid(), (ibClassID)1001);
}

// …and without one the word is kept, for whoever holds the configuration to look up after (LookupTypes) — a call is
// read before its caller is known, and not read again. All the way down; the word gone once it is answered.
TEST(JsonProvider, NodeType_KeptWordLookedUpAfter) {
	ibJsonProvider provider;
	provider.SetTypeResolver([](ibClassID) { return ibString(wxT("Catalog")); });
	ibWriterMemory w;
	ibDataNode n(1001, 1);
	ibDataNode& inner = n.Child(ibT("Inner"));
	inner.SetClsid(1001);
	inner.SetMetaId(2);
	provider.Write(n, w);

	ibDataNode back = Parse(w.buffer());
	EXPECT_EQ(back.GetClsid(), (ibClassID)0);
	EXPECT_EQ(back.GetValue<ibString>(ibT("NodeType")), wxT("Catalog"));

	ibJsonProvider lookup;
	lookup.SetTypeLookup([](const ibString& name) -> ibClassID { return name == wxT("Catalog") ? 1001 : 0; });
	lookup.LookupTypes(back);
	EXPECT_EQ(back.GetClsid(), (ibClassID)1001);
	EXPECT_FALSE(back.Has(ibT("NodeType")));
	const auto innerBack = back.FindChild(ibT("Inner"));
	ASSERT_TRUE(innerBack);
	EXPECT_EQ(innerBack->GetClsid(), (ibClassID)1001);
	EXPECT_FALSE(innerBack->Has(ibT("NodeType")));
}

// The rule of a key, for a tree another reader made of the text (the thin client's): the one the text reader keeps — the
// structural keys the node's own, a node a property, the rest fields; a structural key holding what it cannot refused.
TEST(JsonProvider, ReadEntry_PlacesKeysAsTheTextReaderDoes) {
	ibJsonProvider provider;
	ibDataNode node;
	EXPECT_TRUE(provider.ReadEntry(node, wxT("NodeType"), ibDataValue::UInt(1001)));
	EXPECT_TRUE(provider.ReadEntry(node, wxT("NodeId"), ibDataValue::Int(7)));
	EXPECT_TRUE(provider.ReadEntry(node, wxT("TypeDesc"), ibDataValue::String(wxT("String"))));
	EXPECT_TRUE(provider.ReadEntry(node, wxT("Name"), ibDataValue::String(wxT("Nomenclature"))));
	EXPECT_TRUE(provider.ReadEntry(node, wxT("Type"), ibDataValue::Child(std::make_shared<ibDataNode>())));
	EXPECT_FALSE(provider.ReadEntry(node, wxT("NodeId"), ibDataValue::String(wxT("seven"))));

	EXPECT_EQ(node.GetClsid(), (ibClassID)1001);
	EXPECT_EQ(node.GetMetaId(), (ibMetaID)7);
	EXPECT_FALSE(node.Has(ibT("TypeDesc")));
	EXPECT_EQ(node.GetValue<ibString>(ibT("Name")), wxT("Nomenclature"));
	EXPECT_TRUE(node.FindChild(ibT("Type")));

	// …and a string's: a binary only where Write wrote one.
	EXPECT_EQ(ibJsonProvider::ReadString(wxT("base64:AQI=")).Kind(), ibDataKind::Binary);
	EXPECT_EQ(ibJsonProvider::ReadString(wxT("base64 is a word")).Kind(), ibDataKind::String);
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
	auto v = back.FindField(ibT("when"));
	ASSERT_TRUE(v);
	EXPECT_EQ(v->Kind(), ibDataKind::String);
	EXPECT_FALSE(v->AsString().IsEmpty());
}

// Guid likewise: its text, read back as a String - which whoever asks for a guid reads as one (AsGuid).
TEST(JsonProvider, Guid_DegradesToString_ByDesign) {
	const ibGuid key(ibString(wxT("0f8fad5b-d9cb-469f-a165-70867728950e")));
	ibDataNode n;
	n.AddField(wxT("key"), ibDataValue::Guid(key));

	const ibDataNode back = Parse(Emit(n));   // named — FindField points into it
	auto v = back.FindField(ibT("key"));
	ASSERT_TRUE(v);
	EXPECT_EQ(v->Kind(), ibDataKind::String);
	EXPECT_EQ(v->AsString(), key.str());
	EXPECT_EQ(v->AsGuid(), key);
}

// TypeDesc is injected beside a typeId with no node entry behind it — the parser
// drops it rather than inventing a key the writer never had.
TEST(JsonProvider, TypeDesc_IsDroppedNotMaterialised) {
	ibJsonProvider provider;
	provider.SetTypeResolver([](ibClassID) { return ibString(wxT("String")); });
	ibDataNode n;
	n.AddField(wxT("typeId"), ibDataValue::UInt(777));
	ibWriterMemory w;
	provider.Write(n, w);

	const ibDataNode back = Parse(w.buffer());
	EXPECT_TRUE(!back.FindField(ibT("TypeDesc")));
	EXPECT_TRUE(!back.FindProperty(ibT("TypeDesc")));
	auto typeId = back.FindField(ibT("typeId"));
	ASSERT_TRUE(typeId);
	EXPECT_EQ(typeId->AsUInt(), (std::uint64_t)777);
}

// ---------------------------------------------------------------------------
// Failure is loud
// ---------------------------------------------------------------------------

// Malformed input throws rather than handing back a half-built tree.
TEST(JsonProvider, MalformedInput_Throws) {
	const char* text = "{ \"a\": [1, 2 ";
	const ibMemoryBuffer bad(text, strlen(text));

	ibReaderMemory reader(bad);
	ibDataNode out;
	EXPECT_THROW(ibJsonProvider().Read(reader, out), ibCoreException);
}

// An empty buffer is "nothing to read", not a parse error.
TEST(JsonProvider, EmptyInput_ReturnsFalse) {
	ibMemoryBuffer empty;
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

ibMemoryBuffer Text(const std::string& text)
{
	return ibMemoryBuffer(text.data(), text.size());
}

// A root object whose field "v" is `arrays` nested arrays around the number 1.
ibMemoryBuffer NestedArrays(int arrays)
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
ibMemoryBuffer NestedObjects(int levels)
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

std::optional<ibDataValue> InnermostArray(const ibDataNode& node, int arrays)
{
	auto v = node.FindField(ibT("v"));
	for (int i = 0; i < arrays; ++i) {
		if (!v || v->Kind() != ibDataKind::Array || v->AsArray().size() != 1)
			return std::nullopt;
		v = v->AsArray()[0];
	}
	return v;
}

} // namespace

// The deepest legal text is still a value: the leaf is the number that was written.
TEST(JsonProvider, Nesting_AtTheLimit_IsRead) {
	// Root plus (limit - 1) arrays is exactly `limit` containers.
	const int limit = ibJsonProvider::kMaxNesting;
	const ibDataNode arrays = Parse(NestedArrays(limit - 1));
	const std::optional<ibDataValue> leaf = InnermostArray(arrays, limit - 1);
	ASSERT_TRUE(leaf);
	EXPECT_EQ(leaf->Kind(), ibDataKind::Number);
	EXPECT_EQ(leaf->AsInt(), (std::int64_t)1);

	const ibDataNode objects = Parse(NestedObjects(limit));
	std::optional<ibDataNode> inner = objects;
	for (int i = 1; i < limit; ++i) {
		inner = inner->FindChild(ibT("c"));
		ASSERT_TRUE(inner);
	}
	EXPECT_EQ(inner->GetValue<std::int32_t>(ibT("n")), 1);
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
	EXPECT_TRUE(node.Has(ibT("a")));
	EXPECT_TRUE(node.Has(ibT("b")));
}

// The deepest legal text on the stack a worker thread has on macOS (512 KB). Each level moves
// its subtree up rather than copying it, so the depth costs the descent and nothing more.
TEST(JsonProvider, Nesting_AtTheLimit_FitsASmallThreadStack) {
	bool read = false;
	RunOnStack(512 * 1024, [&read] {
		try {
			const ibDataNode node = Parse(NestedArrays(ibJsonProvider::kMaxNesting - 1));
			read = InnermostArray(node, ibJsonProvider::kMaxNesting - 1).has_value();
		}
		catch (...) {
			read = false;
		}
	});
	EXPECT_TRUE(read);
}
