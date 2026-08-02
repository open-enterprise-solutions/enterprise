////////////////////////////////////////////////////////////////////////////
//	Description : ibJsonProvider Write -> Read tests. The JSON view is LOSSY by
//	              design, so these pin down BOTH halves: what survives the trip,
//	              and — just as deliberately — what does not (see the header:
//	              Fields/Properties flatten, Date degrades to String, TypeDesc is
//	              synthetic). Pure structure layer — no DB, no metadata bring-up.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>
#include <cstring>

#include "backend/serialize/jsonProvider.h"
#include "backend/backend_exception.h"

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
	n.AddField(wxT("when"), ibDataValue::Date(1700000000000LL));

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
	EXPECT_THROW(ibJsonProvider().Read(reader, out), ibBackendException);
}

// An empty buffer is "nothing to read", not a parse error.
TEST(JsonProvider, EmptyInput_ReturnsFalse) {
	wxMemoryBuffer empty;
	ibReaderMemory reader(empty);
	ibDataNode out;
	EXPECT_FALSE(ibJsonProvider().Read(reader, out));
}
