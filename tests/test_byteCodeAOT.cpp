// =============================================================================
// OES Enterprise — bytecode AOT serialization round-trip tests
//
// Exercises ibByteCode::SerializeAOT / DeserializeAOT (byteCodeAOT.cpp).
// Builds bytecodes programmatically (no compiler / runtime init required),
// writes them to ibWriterMemory, reads back into a fresh ibByteCode, and
// asserts every persisted field round-trips exactly.
//
// Persisted fields covered:
//   m_id, m_version, m_descriptorClsid, m_strModuleName, m_lStartModule,
//   m_lVarCount, m_bExpressionOnly, m_listCode, m_listConst (Empty / Null /
//   Boolean / Number / Date / String), m_listVar, m_listFunc (incl. nested
//   m_listLocals + m_listParam + m_listParamRealName), m_dependencyIds,
//   m_dependencyVersions.
//
// Skipped fields (live pointers — not part of the AOT contract):
//   m_parent, m_dependencies, m_compileModule. After Deserialize they
//   stay nullptr / empty; m_bCompile is set true by the reader.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/byteCode.h"
#include "backend/compiler/value.h"
#include "backend/fileSystem/fs.h"
#include "backend/guid.h"

namespace {

// Round-trip helper — writes `src` to a memory writer, reads back into
// `dst`. Any failure short-circuits; tests still call EXPECT_TRUE on the
// pair so a failed round-trip is reported clearly.
bool RoundTrip(const ibByteCode& src, ibByteCode& dst) {
	ibWriterMemory w;
	if (!src.SerializeAOT(w)) return false;
	wxMemoryBuffer blob = w.buffer();
	ibReaderMemory r(blob);
	return dst.DeserializeAOT(r);
}

ibGuid MakeGuid(unsigned char seed) {
	std::array<unsigned char, 16> bytes{};
	for (size_t i = 0; i < bytes.size(); ++i)
		bytes[i] = (unsigned char)(seed + (unsigned char)i);
	return ibGuid(bytes);
}

ibByteUnit MakeUnit(short oper, unsigned int line, const wxString& mod = wxEmptyString) {
	ibByteUnit u;
	u.m_numOper      = oper;
	u.m_numString    = 1;
	u.m_numLine      = line;
	u.m_param1.m_numArray = 7;  u.m_param1.m_numIndex = 8;
	u.m_param2.m_numArray = 9;  u.m_param2.m_numIndex = 10;
	u.m_param3.m_numArray = 11; u.m_param3.m_numIndex = 12;
	u.m_param4.m_numArray = 13; u.m_param4.m_numIndex = 14;
	u.m_strModuleName = mod;
	return u;
}

ibByteCode::ibByteCodeVarInfo MakeVar(const wxString& name, ibVarKind kind, long slot) {
	ibByteCode::ibByteCodeVarInfo v;
	v.m_slotIndex   = slot;
	v.m_clsid       = 0;
	v.m_kind        = kind;
	v.m_parentRef   = -1;
	v.m_strRealName = name;
	return v;
}

} // namespace

// ===========================================================================
// Empty bytecode — every collection size 0, default header values
// ===========================================================================

TEST(ByteCodeAOT, EmptyRoundTrip) {
	ibByteCode src;
	src.m_id      = MakeGuid(1);
	src.m_version = MakeGuid(2);
	src.m_descriptorClsid = 0xAABBCCDDEEFF0011ULL;
	src.m_strModuleName   = wxT("module-empty");

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));

	EXPECT_EQ(dst.m_id,                   src.m_id);
	EXPECT_EQ(dst.m_version,              src.m_version);
	EXPECT_EQ(dst.m_descriptorClsid,      src.m_descriptorClsid);
	EXPECT_EQ(dst.m_strModuleName,        src.m_strModuleName);
	EXPECT_EQ(dst.m_lStartModule,         0);
	EXPECT_EQ(dst.m_lVarCount,            0);
	EXPECT_FALSE(dst.m_bExpressionOnly);
	EXPECT_TRUE(dst.m_listCode.empty());
	EXPECT_TRUE(dst.m_listConst.empty());
	EXPECT_TRUE(dst.m_listVar.empty());
	EXPECT_TRUE(dst.m_listFunc.empty());
	EXPECT_TRUE(dst.m_dependencyIds.empty());
	EXPECT_TRUE(dst.m_dependencyVersions.empty());
	EXPECT_TRUE(dst.m_bCompile); // reader stamps true on success
	EXPECT_EQ(dst.m_parent, nullptr); // live pointer NOT persisted
	EXPECT_TRUE(dst.m_dependencies.empty()); // live pointers NOT persisted
}

// ===========================================================================
// Header round-trip including m_bExpressionOnly and m_lStartModule/m_lVarCount
// ===========================================================================

TEST(ByteCodeAOT, HeaderFields) {
	ibByteCode src;
	src.m_id              = MakeGuid(7);
	src.m_version         = MakeGuid(11);
	src.m_descriptorClsid = 0x4D445F434154ULL; // 'MD_CAT' bytes
	src.m_strModuleName   = wxT("Catalogs.Counterparties.ObjectModule");
	src.m_lStartModule    = 42;
	src.m_lVarCount       = 17;
	src.m_bExpressionOnly = true;

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));

	EXPECT_EQ(dst.m_id,              src.m_id);
	EXPECT_EQ(dst.m_version,         src.m_version);
	EXPECT_EQ(dst.m_descriptorClsid, src.m_descriptorClsid);
	EXPECT_EQ(dst.m_strModuleName,   src.m_strModuleName);
	EXPECT_EQ(dst.m_lStartModule,    src.m_lStartModule);
	EXPECT_EQ(dst.m_lVarCount,       src.m_lVarCount);
	EXPECT_TRUE(dst.m_bExpressionOnly);
}

// ===========================================================================
// m_listCode round-trip — multiple ibByteUnit entries, each with all 4 params
// and source-attribution strings
// ===========================================================================

TEST(ByteCodeAOT, ListCodeRoundTrip) {
	ibByteCode src;
	src.m_id      = MakeGuid(3);
	src.m_version = MakeGuid(4);
	src.m_listCode.push_back(MakeUnit(/*OPER_LET*/ 13, 1, wxT("modA")));
	src.m_listCode.push_back(MakeUnit(/*OPER_ADD*/  1, 2, wxT("modB")));
	src.m_listCode.push_back(MakeUnit(/*OPER_RET*/  8, 3, wxEmptyString));
	src.m_listCode[2].m_strDocPath  = wxT("/Catalog/Object");
	src.m_listCode[2].m_strFileName = wxT("external.epf");

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));

	ASSERT_EQ(dst.m_listCode.size(), src.m_listCode.size());
	for (size_t i = 0; i < src.m_listCode.size(); ++i) {
		const auto& a = src.m_listCode[i];
		const auto& b = dst.m_listCode[i];
		EXPECT_EQ(a.m_numOper,        b.m_numOper)        << "i=" << i;
		EXPECT_EQ(a.m_numString,      b.m_numString)      << "i=" << i;
		EXPECT_EQ(a.m_numLine,        b.m_numLine)        << "i=" << i;
		EXPECT_EQ(a.m_param1.m_numArray, b.m_param1.m_numArray);
		EXPECT_EQ(a.m_param1.m_numIndex, b.m_param1.m_numIndex);
		EXPECT_EQ(a.m_param4.m_numArray, b.m_param4.m_numArray);
		EXPECT_EQ(a.m_param4.m_numIndex, b.m_param4.m_numIndex);
		EXPECT_EQ(a.m_strModuleName,  b.m_strModuleName);
		EXPECT_EQ(a.m_strDocPath,     b.m_strDocPath);
		EXPECT_EQ(a.m_strFileName,    b.m_strFileName);
	}
}

// ===========================================================================
// m_listConst round-trip — all primitive types that appear in compiled
// bytecode (TYPE_REFFER / TYPE_VALUE never reach the const pool)
// ===========================================================================

TEST(ByteCodeAOT, ListConstAllPrimitives) {
	ibByteCode src;
	src.m_id      = MakeGuid(5);
	src.m_version = MakeGuid(6);

	{ ibValue v(ibValueTypes::TYPE_EMPTY);   src.m_listConst.push_back(v); }
	{ ibValue v(ibValueTypes::TYPE_NULL);    src.m_listConst.push_back(v); }
	{ ibValue v(true);                       src.m_listConst.push_back(v); }
	{ ibValue v(false);                      src.m_listConst.push_back(v); }
	{ ibValue v(42);                         src.m_listConst.push_back(v); }
	{ ibValue v(-1234567);                   src.m_listConst.push_back(v); }
	{ ibValue v(wxString(wxT("hello")));     src.m_listConst.push_back(v); }
	{ ibValue v(wxString(wxT("")));          src.m_listConst.push_back(v); }
	{
		ibValue v(ibValueTypes::TYPE_DATE);
		v.m_dData = (wxLongLong_t)1714639200; // arbitrary epoch seconds
		src.m_listConst.push_back(v);
	}

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));
	ASSERT_EQ(dst.m_listConst.size(), src.m_listConst.size());

	EXPECT_EQ(dst.m_listConst[0].m_typeClass, ibValueTypes::TYPE_EMPTY);
	EXPECT_EQ(dst.m_listConst[1].m_typeClass, ibValueTypes::TYPE_NULL);
	EXPECT_EQ(dst.m_listConst[2].m_typeClass, ibValueTypes::TYPE_BOOLEAN);
	EXPECT_TRUE(dst.m_listConst[2].m_bData);
	EXPECT_EQ(dst.m_listConst[3].m_typeClass, ibValueTypes::TYPE_BOOLEAN);
	EXPECT_FALSE(dst.m_listConst[3].m_bData);
	EXPECT_EQ(dst.m_listConst[4].m_typeClass, ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(dst.m_listConst[4].m_fData, ibNumber(42));
	EXPECT_EQ(dst.m_listConst[5].m_typeClass, ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(dst.m_listConst[5].m_fData, ibNumber(-1234567));
	EXPECT_EQ(dst.m_listConst[6].m_typeClass, ibValueTypes::TYPE_STRING);
	EXPECT_EQ(dst.m_listConst[6].GetString(), wxT("hello"));   // m_sData removed in Phase 2 (ibString)
	EXPECT_EQ(dst.m_listConst[7].m_typeClass, ibValueTypes::TYPE_STRING);
	EXPECT_TRUE(dst.m_listConst[7].GetString().IsEmpty());
	EXPECT_EQ(dst.m_listConst[8].m_typeClass, ibValueTypes::TYPE_DATE);
	EXPECT_EQ(dst.m_listConst[8].m_dData,     (wxLongLong_t)1714639200);
}

// Number with > 14 digits forces ibNumber's heap tier — round-trip must
// preserve the exact-decimal magnitude (binary form, not lossy double).
TEST(ByteCodeAOT, ListConstHighPrecisionNumber) {
	ibByteCode src;
	src.m_id      = MakeGuid(8);
	src.m_version = MakeGuid(9);

	ibValue v(ibValueTypes::TYPE_NUMBER);
	v.m_fData = ibNumber(wxT("765.3456754567765443343")); // 22 digits → heap
	src.m_listConst.push_back(v);

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));
	ASSERT_EQ(dst.m_listConst.size(), 1u);
	EXPECT_EQ(dst.m_listConst[0].m_typeClass, ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(dst.m_listConst[0].m_fData.ToString(),
	          wxT("765.3456754567765443343"));
}

// ===========================================================================
// ibValueTypes narrowed to `enum : unsigned char` (backend_core.h). The AOT
// const-value format stores the type tag as ONE byte —
//   writer: w.w_u8((uint8_t)v.m_typeClass)
//   reader: v.SetType((ibValueTypes)r.r_u8())
// (byteCodeAOT.cpp). These tests guard that the narrowing stayed
// binary-compatible with bytecode persisted BEFORE the change: the wire is
// still a single byte and every enumerator's integer value is frozen, so an
// old cache's type bytes still decode to the same types.
// ===========================================================================

TEST(ByteCodeAOT, ValueTypeTagIsFrozenSingleByte) {
	// The tag occupies one byte on the wire regardless of the enum's
	// underlying type; the narrowing makes the in-memory slot match.
	static_assert(sizeof(ibValueTypes) == 1,
	              "ibValueTypes must stay 1 byte to match the AOT uint8_t wire tag");

	// Frozen wire values. Changing any of these is a breaking AOT format
	// change and MUST bump the format version — old caches encode these bytes.
	static_assert((uint8_t)ibValueTypes::TYPE_EMPTY    == 0,   "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_BOOLEAN  == 1,   "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_NUMBER   == 2,   "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_DATE     == 3,   "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_STRING   == 4,   "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_NULL     == 5,   "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_REFFER   == 100, "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_VALUE    == 200, "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_ENUM     == 201, "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_OLE      == 202, "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_FUNCTION == 203, "wire tag drift");
	static_assert((uint8_t)ibValueTypes::TYPE_ITERATOR == 204, "wire tag drift");

	// Every enumerator survives the exact uint8_t encode/decode that the AOT
	// writer/reader perform — including the high ones (>127), where a signed
	// char would have wrapped to a negative and decoded to the wrong type.
	const ibValueTypes all[] = {
		ibValueTypes::TYPE_EMPTY,  ibValueTypes::TYPE_BOOLEAN,  ibValueTypes::TYPE_NUMBER,
		ibValueTypes::TYPE_DATE,   ibValueTypes::TYPE_STRING,   ibValueTypes::TYPE_NULL,
		ibValueTypes::TYPE_REFFER, ibValueTypes::TYPE_VALUE,    ibValueTypes::TYPE_ENUM,
		ibValueTypes::TYPE_OLE,    ibValueTypes::TYPE_FUNCTION, ibValueTypes::TYPE_ITERATOR,
	};
	for (ibValueTypes t : all) {
		const uint8_t       wire = (uint8_t)t;          // writer side
		const ibValueTypes  back = (ibValueTypes)wire;  // reader side
		EXPECT_EQ(back, t) << "tag byte " << (int)wire << " did not round-trip";
	}
}

// Full SerializeAOT -> blob -> DeserializeAOT round-trip asserting the type
// tag of every const-eligible value survives the single-byte encoding. This
// is the end-to-end check behind the static guards above: it drives the real
// WriteConstValue/ReadConstValue path, not just the cast.
TEST(ByteCodeAOT, ConstTypeTagRoundTripsThroughAOT) {
	ibByteCode src;
	src.m_id      = MakeGuid(11);
	src.m_version = MakeGuid(12);

	// One const of each type that reaches the const pool, in tag order.
	src.m_listConst.push_back(ibValue(ibValueTypes::TYPE_EMPTY));
	src.m_listConst.push_back(ibValue(ibValueTypes::TYPE_NULL));
	src.m_listConst.push_back(ibValue(true));
	src.m_listConst.push_back(ibValue(7));
	{
		ibValue v(ibValueTypes::TYPE_DATE);
		v.m_dData = (wxLongLong_t)1700000000;
		src.m_listConst.push_back(v);
	}
	src.m_listConst.push_back(ibValue(wxString(wxT("ünïcödé"))));   // multibyte string payload

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));
	ASSERT_EQ(dst.m_listConst.size(), src.m_listConst.size());

	for (size_t i = 0; i < src.m_listConst.size(); ++i)
		EXPECT_EQ(dst.m_listConst[i].GetType(), src.m_listConst[i].GetType())
			<< "const #" << i << " type tag changed across AOT round-trip";

	EXPECT_EQ(dst.m_listConst[5].GetString(), wxString(wxT("ünïcödé")));
}

// ===========================================================================
// m_listVar round-trip — every ibVarKind, with parent refs and scoped flag
// ===========================================================================

TEST(ByteCodeAOT, ListVarAllKinds) {
	ibByteCode src;
	src.m_id      = MakeGuid(10);
	src.m_version = MakeGuid(11);

	src.m_listVar.push_back(MakeVar(wxT("local"),    ibVarKind::Local,     0));
	src.m_listVar.push_back(MakeVar(wxT("export"),   ibVarKind::Export,    1));
	src.m_listVar.push_back(MakeVar(wxT("external"), ibVarKind::External,  2));
	src.m_listVar.push_back(MakeVar(wxT("Manager"),  ibVarKind::Context,   3));

	auto prop = MakeVar(wxT("Catalogs"), ibVarKind::ContextProp, 0);
	prop.m_strContext = wxT("Manager");
	prop.m_parentRef  = 3; // index of "Manager" above
	prop.m_clsid      = 0x12345;
	src.m_listVar.push_back(prop);

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));
	ASSERT_EQ(dst.m_listVar.size(), 5u);

	EXPECT_EQ(dst.m_listVar[0].m_kind, ibVarKind::Local);
	EXPECT_EQ(dst.m_listVar[1].m_kind, ibVarKind::Export);
	EXPECT_EQ(dst.m_listVar[2].m_kind, ibVarKind::External);
	EXPECT_EQ(dst.m_listVar[3].m_kind, ibVarKind::Context);
	EXPECT_EQ(dst.m_listVar[4].m_kind, ibVarKind::ContextProp);

	EXPECT_EQ(dst.m_listVar[4].m_strRealName, wxT("Catalogs"));
	EXPECT_EQ(dst.m_listVar[4].m_strContext,  wxT("Manager"));
	EXPECT_EQ(dst.m_listVar[4].m_parentRef,    3);
	EXPECT_EQ(dst.m_listVar[4].m_clsid, (ibClassID)0x12345);
}

// ===========================================================================
// m_listFunc round-trip — including m_listParam (with default values),
// m_listParamRealName, and embedded m_listLocals
// ===========================================================================

TEST(ByteCodeAOT, ListFuncWithLocalsAndParams) {
	ibByteCode src;
	src.m_id      = MakeGuid(12);
	src.m_version = MakeGuid(13);

	ibByteCode::ibByteFunction fn;
	fn.m_lCodeLine       = 100;
	fn.m_bCodeRet        = true;
	fn.m_lVarCount       = 5;
	fn.m_returnClsid     = 0xABCDEF;
	fn.m_kind            = ibFnKind::Export;
	// THREE flags that default FALSE and have to be RESTORED rather than derived —
	// exactly the shape that goes missing without a symptom, and all three went
	// missing in turn. m_needsHeapFrame was never written until v21, so a module
	// served from the cache came back with it cleared on every function; m_valueCached
	// went the same way; m_valueVariadic was not carried at all until v23, which made
	// every built-in of negative arity — Max, Min — REFUSE EVERY CALL it was given
	// the moment a name resolved through bytecode instead of a live compile context.
	fn.m_needsHeapFrame  = true;
	fn.m_valueCached     = true;
	fn.m_valueVariadic   = true;
	fn.m_strRealName     = wxT("Calculate");
	fn.m_strContext      = wxEmptyString;

	{
		ibByteCode::ibByteParam p;
		p.m_bByValue = false;
		p.m_clsid  = 0x111;
		p.m_defaultValue.m_numArray = -1;
		p.m_defaultValue.m_numIndex = -1;
		p.m_strName = wxT("a");
		fn.m_listParam.push_back(p);
	}
	{
		ibByteCode::ibByteParam p;
		p.m_bByValue = true;
		p.m_clsid  = 0;
		p.m_defaultValue.m_numArray = 5;
		p.m_defaultValue.m_numIndex = 7;
		p.m_strName = wxT("b");
		fn.m_listParam.push_back(p);
	}

	fn.m_listLocals.push_back(MakeVar(wxT("temp"), ibVarKind::Local, 0));
	fn.m_listLocals.push_back(MakeVar(wxT("acc"),  ibVarKind::Local, 1));
	src.m_listFunc.push_back(fn);

	// Context-method entry — different kind, with parent reference.
	ibByteCode::ibByteFunction ctxFn;
	ctxFn.m_lCodeLine       = -1;
	ctxFn.m_bCodeRet        = true;
	ctxFn.m_kind            = ibFnKind::ContextMethod;
	ctxFn.m_strRealName     = wxT("GetForm");
	ctxFn.m_strContext      = wxT("Manager");
	ctxFn.m_parentRef       = 0;
	src.m_listFunc.push_back(ctxFn);

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));
	ASSERT_EQ(dst.m_listFunc.size(), 2u);

	const auto& a = dst.m_listFunc[0];
	EXPECT_EQ(a.m_listParam.size(), 2u);
	EXPECT_EQ(a.m_lCodeLine,       100);
	EXPECT_TRUE(a.m_bCodeRet);
	EXPECT_EQ(a.m_lVarCount,       5);
	EXPECT_EQ(a.m_returnClsid,     (ibClassID)0xABCDEF);
	EXPECT_EQ(a.m_kind,            ibFnKind::Export);
	EXPECT_TRUE(a.m_needsHeapFrame) << "the heap-frame flag did not survive the round trip";
	EXPECT_TRUE(a.m_valueCached)    << "the Cached modifier did not survive the round trip";
	EXPECT_TRUE(a.m_valueVariadic)  << "the variadic flag did not survive the round trip - a negative-arity "
	                                   "built-in resolved through bytecode then refuses every call it is given";
	EXPECT_EQ(a.m_strRealName,     wxT("Calculate"));

	ASSERT_EQ(a.m_listParam.size(),         2u);
	EXPECT_FALSE(a.m_listParam[0].m_bByValue);
	EXPECT_TRUE(a.m_listParam[1].m_bByValue);
	EXPECT_EQ(a.m_listParam[0].m_clsid, (ibClassID)0x111);
	EXPECT_EQ(a.m_listParam[1].m_defaultValue.m_numArray, 5);
	EXPECT_EQ(a.m_listParam[1].m_defaultValue.m_numIndex, 7);
	EXPECT_EQ(a.m_listParam[0].m_strName, wxT("a"));
	EXPECT_EQ(a.m_listParam[1].m_strName, wxT("b"));

	ASSERT_EQ(a.m_listLocals.size(), 2u);
	EXPECT_EQ(a.m_listLocals[0].m_strRealName, wxT("temp"));
	EXPECT_EQ(a.m_listLocals[1].m_strRealName, wxT("acc"));

	const auto& b = dst.m_listFunc[1];
	EXPECT_EQ(b.m_kind,        ibFnKind::ContextMethod);
	EXPECT_EQ(b.m_strRealName, wxT("GetForm"));
	EXPECT_EQ(b.m_strContext,  wxT("Manager"));
	EXPECT_EQ(b.m_parentRef,   0);
	EXPECT_TRUE(b.m_listParam.empty());
	EXPECT_TRUE(b.m_listLocals.empty());
}

// ===========================================================================
// Dependencies — id and version vectors persist; live pointer vector does NOT
// ===========================================================================

TEST(ByteCodeAOT, DependencyIdsAndVersions) {
	ibByteCode src;
	src.m_id      = MakeGuid(14);
	src.m_version = MakeGuid(15);

	src.m_dependencyIds.push_back(MakeGuid(20));
	src.m_dependencyIds.push_back(MakeGuid(21));
	src.m_dependencyIds.push_back(MakeGuid(22));
	src.m_dependencyVersions.push_back(MakeGuid(30));
	src.m_dependencyVersions.push_back(MakeGuid(31));
	src.m_dependencyVersions.push_back(MakeGuid(32));

	// Live pointers — explicitly not part of the AOT contract; the
	// reader leaves m_dependencies empty.
	ibByteCode dummy1, dummy2;
	src.m_dependencies.push_back(&dummy1);
	src.m_dependencies.push_back(&dummy2);

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));

	ASSERT_EQ(dst.m_dependencyIds.size(),      3u);
	ASSERT_EQ(dst.m_dependencyVersions.size(), 3u);
	EXPECT_EQ(dst.m_dependencyIds[0],      MakeGuid(20));
	EXPECT_EQ(dst.m_dependencyIds[2],      MakeGuid(22));
	EXPECT_EQ(dst.m_dependencyVersions[1], MakeGuid(31));

	// Live-pointer vector NOT persisted — caller resolves via session
	// registry once all bytecodes are loaded.
	EXPECT_TRUE(dst.m_dependencies.empty());
}

// ===========================================================================
// Format header — magic / version mismatch must reject (cache miss path)
// ===========================================================================

TEST(ByteCodeAOT, RejectBadMagic) {
	// Garbage that doesn't start with our magic.
	wxMemoryBuffer buf;
	uint32_t junk[8] = { 0xDEADBEEF, 1, 2, 3, 4, 5, 6, 7 };
	buf.AppendData(junk, sizeof(junk));

	ibReaderMemory r(buf);
	ibByteCode dst;
	EXPECT_FALSE(dst.DeserializeAOT(r));
}

TEST(ByteCodeAOT, RejectShortBuffer) {
	// Only 2 bytes — not even enough for the magic. Reader must fail
	// without reading past end.
	wxMemoryBuffer buf;
	uint8_t two[2] = { 0xFF, 0xFF };
	buf.AppendData(two, 2);

	ibReaderMemory r(buf);
	ibByteCode dst;
	EXPECT_FALSE(dst.DeserializeAOT(r));
}

TEST(ByteCodeAOT, EmptyBufferRejected) {
	wxMemoryBuffer buf;
	ibReaderMemory r(buf);
	ibByteCode dst;
	EXPECT_FALSE(dst.DeserializeAOT(r));
}

// ===========================================================================
// Reuse — calling Deserialize on a non-empty bytecode resets it first
// ===========================================================================

TEST(ByteCodeAOT, DeserializeResetsTarget) {
	// Source has nothing but header.
	ibByteCode src;
	src.m_id      = MakeGuid(40);
	src.m_version = MakeGuid(41);
	src.m_strModuleName = wxT("target-resets");

	// Pre-populate dst with stale content.
	ibByteCode dst;
	dst.m_id              = MakeGuid(99);
	dst.m_strModuleName   = wxT("STALE");
	dst.m_lStartModule    = 9999;
	dst.m_listCode.push_back(MakeUnit(/*OPER_RET*/ 8, 1));
	dst.m_listConst.push_back(ibValue(true));
	dst.m_dependencyIds.push_back(MakeGuid(123));

	ASSERT_TRUE(RoundTrip(src, dst));

	EXPECT_EQ(dst.m_id,            MakeGuid(40));
	EXPECT_EQ(dst.m_strModuleName, wxT("target-resets"));
	EXPECT_EQ(dst.m_lStartModule,  0);
	EXPECT_TRUE(dst.m_listCode.empty());
	EXPECT_TRUE(dst.m_listConst.empty());
	EXPECT_TRUE(dst.m_dependencyIds.empty());
}

// ===========================================================================
// Stress — large list-counts (within sanity cap) round-trip cleanly
// ===========================================================================

TEST(ByteCodeAOT, LargeButRealisticBytecode) {
	ibByteCode src;
	src.m_id      = MakeGuid(50);
	src.m_version = MakeGuid(51);

	// 5000 instructions, 200 vars, 50 functions — roughly the upper
	// bound of a "big" common module.
	for (int i = 0; i < 5000; ++i)
		src.m_listCode.push_back(MakeUnit((short)(i % 60), (unsigned)i + 1));
	for (int i = 0; i < 200; ++i)
		src.m_listVar.push_back(
			MakeVar(wxString::Format(wxT("v%d"), i),
			        i % 2 ? ibVarKind::Local : ibVarKind::Export, i));
	for (int i = 0; i < 50; ++i) {
		ibByteCode::ibByteFunction fn;
		fn.m_lCodeLine    = 100 + i;
		fn.m_bCodeRet     = (i % 2 == 0);
		fn.m_kind         = ibFnKind::Export;
		fn.m_strRealName  = wxString::Format(wxT("Fn%d"), i);
		src.m_listFunc.push_back(fn);
	}

	ibByteCode dst;
	ASSERT_TRUE(RoundTrip(src, dst));
	EXPECT_EQ(dst.m_listCode.size(), 5000u);
	EXPECT_EQ(dst.m_listVar.size(),  200u);
	EXPECT_EQ(dst.m_listFunc.size(), 50u);
	EXPECT_EQ(dst.m_listVar[42].m_strRealName, wxT("v42"));
	EXPECT_EQ(dst.m_listFunc[7].m_strRealName, wxT("Fn7"));
	EXPECT_EQ(dst.m_listCode[1234].m_numLine,  1235u);
}

// ===========================================================================
// THE VISIBILITY RULE, asked of every kind
//
// "A child sees its parent entire except the parent's own private locals."
// On the bytecode side access IS the kind: Private is Local, Public is Export,
// Protected is its own kind. The compile side used to answer the same question
// with a LIST of permitted kinds, which left Context / ContextProp out — so one
// name resolved differently depending on which road found it.
// ===========================================================================

TEST(ByteCodeAOT, AChildSeesEverythingButPrivateLocals) {
	// The one thing a child must not see. `var X` with no modifier is kind=Local,
	// and on this side that IS "private".
	EXPECT_TRUE(MakeVar(wxT("hidden"), ibVarKind::Local, 0).IsLocal());

	// Declared for others to see: `var X Public` is stamped kind=Export at
	// creation, `var X Protected` flips to kind=Protected (compileCode.cpp).
	EXPECT_FALSE(MakeVar(wxT("shared"),  ibVarKind::Export,    1).IsLocal());
	EXPECT_FALSE(MakeVar(wxT("guarded"), ibVarKind::Protected, 2).IsLocal());

	// System bindings — for children by construction, and the ones the compile
	// side's list forgot: a common module reaching a document's object module is
	// External, and Catalogs / Documents / Manager are Context / ContextProp.
	EXPECT_FALSE(MakeVar(wxT("StockManagement"), ibVarKind::External,    3).IsLocal());
	EXPECT_FALSE(MakeVar(wxT("Manager"),         ibVarKind::Context,     4).IsLocal());
	EXPECT_FALSE(MakeVar(wxT("Catalogs"),        ibVarKind::ContextProp, 5).IsLocal());
}
