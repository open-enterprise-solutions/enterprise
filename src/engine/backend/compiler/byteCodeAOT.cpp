// Ahead-Of-Time (AOT) bytecode persistence — Step 1 of the AOT cache
// (docs/private/next-session-aot.md). Writes a compiled ibByteCode into a flat
// memory blob (later persisted as sys_bytecode_cache.blob) and reads
// it back. Cold sessions then skip recompilation by Deserialize-ing
// the blob whose source-hash + metadata-version + compiler-version
// still match.
//
// Format (host-endian; portable LE encoding is a follow-up — see the
// kAOTFlagPortable bit reserved in the header). Layout is linear, no
// fs.h chunk wrappers — every collection is `u32 count` + `count`
// entries laid out back-to-back. Strings are zero-terminated UTF-8
// via ibWriter::w_stringZ / ibReader::r_stringZ.
//
// Header (all little-endian on x86/x64):
//   u32  magic            = kAOTMagic ('OBC1')
//   u16  format_version   = kAOTFormatVersion
//   u16  flags            = 0  (bit 0 reserved for kAOTFlagPortable)
//   u8[16] m_id           — descriptor identity
//   u8[16] m_version      — per-compile fingerprint
//   u64  m_descriptorClsid
//   s32  m_lStartModule
//   s32  m_lVarCount
//   u8   m_bExpressionOnly
//   zstr m_strModuleName
//   <m_listCode>
//   <m_listConst>
//   <m_listVar>
//   <m_listFunc>
//   <m_dependencyIds>
//   <m_dependencyVersions>
//
// Skipped (live pointers — re-resolved on load): m_parent,
// m_dependencies, m_compileModule, m_bCompile (always set true on
// successful Deserialize).

#include "backend/compiler/byteCode.h"
#include "backend/compiler/byteCodeFormat.h"   // kAOTFormatVersion — also the cache key
#include "backend/compiler/codeDef.h"          // OPER_* — which operand is a slot, a jump, a constant
#include "backend/compiler/compileContext.h"   // DEF_VAR_CONST / DEF_VAR_SKIP — the operand tags
#include "core/fileSystem/fs.h"

#include <algorithm>

namespace {

constexpr uint32_t kAOTMagic         = 0x31434250u; // 'PBC1' little-endian
// v3 (2026-05-09 evening): lambda body model rewritten — no per-lambda
// ibByteFunction in m_listFunc, so ibByteFunction::m_lFinish (added in
// v2) was retired. Reading a v2 blob would mis-align subsequent fields
// because the m_lFinish s32 is gone; strict version check rejects v2
// as cache miss → safe recompile + repopulate.
//
// v4 (2026-05-10): OPER_CALL_LAMBDA now carries the caller's actual arg
// count in m_param2.m_numIndex (set by EmitCallVal). v3 cache rows had
// that field at 0, so dispatch would treat every dynamic call as
// "0 args supplied" and fall into the missing-required-argument throw
// even when the source explicitly passed args. Bumping the version
// forces recompile to re-run the stamping path. Payload layout
// unchanged — same field, just newly meaningful.
//
// v5 (2026-05-10 PM): lambda metadata model unified — anonymous bodies
// pushed into m_listFunc with kind = Lambda + synthetic name. OPER_LFUNC
// .m_param3.m_numIndex now stores the funcIndex; m_param3.m_numArray /
// m_param4.m_numIndex (was paramCount / bCodeRet) are unused. v4 readers
// would still load but dispatch from stale operands → bump rejects.
//
// v6 (2026-05-10 evening pass 2): per-IP scope tracking for CES `{ }`
// block scopes. Added s32 m_scopeId on ibByteCodeVarInfo + vector
// <ScopeRange> on ibByteFunction / ibByteCode. Reverted in v7.
//
// v7 (2026-05-10 evening pass 3): block-scope tracking removed —
// AddVariable RETURN_BLOCK now delegates to parent (mirroring
// CreateVariable), so block-scope vars share the host frame at compile
// time too. Per-IP filter became degenerate dead code. v6 readers
// trying to load v7 would over-read and corrupt the next field; v7
// readers trying to load v6 would under-read and leave the trailing
// scope vector data as garbage in subsequent fields. Strict version
// equality rejects both directions → safe recompile + repopulate.
//
// v8 (2026-05-10 evening pass 4): runtime block-scope cleanup —
// OPER_CTX_BEGIN/END are now emitted ONLY for RETURN_BLOCK ctxs
// (function/lambda body envelope no longer wraps them). OPER_CTX_END
// carries [startSlot, endSlot] in m_param1.{m_numIndex, m_numArray};
// runtime Resets those frame slots to TYPE_EMPTY so debugger Locals
// (filtered by isInitialised) stops showing block-local vars after
// control leaves the block.
//
// v9 (2026-05-11): scope-depth visibility. Each ibByteCodeVarInfo
// gets s32 m_scopeDepth — the compile-time nesting depth at decl.
// Runtime ibRunContext bumps m_currentScopeDepth on OPER_CTX_BEGIN,
// drops on CTX_END; SendLocalVariables emits entries where
// m_scopeDepth <= ctx.m_currentScopeDepth. Replaces v8's per-call
// body-scan + vector<bool> approach with a single integer compare.
// v8 readers loading v9 would mis-read the extra s32, mis-aligning
// downstream fields. Bump rejects v8 → safe recompile + repopulate.
//
// v10 (2026-05-12): OPER_CALL_LINQ added between OPER_CALL_CLOSURE and
// OPER_GET_ARRAY (codeDef.h). Every opcode from OPER_GET_ARRAY through
// OPER_END shifted +1 in numeric value. v9 blobs persist raw m_numOper
// integers, so a v9 reader loading a v10-written blob (or vice versa)
// would dispatch the wrong handler on every post-shift opcode. Bump
// rejects v9 → safe recompile + repopulate. Payload layout unchanged.
// v13 (2026-06-02): bind-kind access opcodes added before OPER_END
// (OPER_GET/SET_EXTERN, OPER_GET/SET_SCOPE, OPER_GET/SET_CONTEXT). OPER_END
// shifts +6, so TYPE_DELTA1 (= OPER_END+1) and every typed-op opcode
// (oper + k*DELTA) shift in numeric value; ContextProp also now emits
// OPER_GET_SCOPE/SET_SCOPE instead of OPER_GET_A/SET_A. v12 blobs persist raw
// m_numOper integers → a v12 reader would dispatch the wrong handler on every
// shifted opcode. Bump rejects v12 → safe recompile + repopulate.
// v14 (2026-06-11): ibByteFunction gained m_lambdaExprAst — the recorded lambda
// body as the L4 query AST (the LINQ-pushdown input). It IS serialised: AOT
// hits are the production norm, so an unserialised AST would silently kill the
// pushdown on every cached module. v13 blobs lack the trailing presence byte →
// bump rejects them → safe recompile + repopulate.
// v15 (2026-06-22): ibByteFunction shed two redundant fields — m_lCodeParamCount
// (a pure mirror of m_listParam.size()) and the parallel m_listParamRealName
// vector (folded into ibByteParam::m_strName). The function record no longer
// writes the leading param-count s32 nor the separate name block; each param's
// name now rides inline in WriteParam/ReadParam. v14 blobs mis-align on the
// dropped fields → bump rejects them → safe recompile + repopulate.
// v16 (2026-06-28): ibClassID now carries a KIND in its high byte (clsid.h). Dynamic
// metaobject ids changed from FNV("R_<metaID>"/…) to constructive (kind<<56 | metaID),
// so every persisted/cached CLSID differs from v15. Bump rejects v15 → safe recompile +
// repopulate (DB/config blobs holding old CLSIDs regenerate too).
// v17 (2026-07-10): the `restrict` clause (KEY_RESTRICT) now records a pushdown AST for its
// where / join-ON lambda (EmitRestrictBody span fix). This is a COMPILER-OUTPUT change, not a
// format change — the payload layout is identical to v16 — but Load() keys only on descriptor_id
// and ignores bytecode_version, so a v16 blob compiled by the pre-fix compiler (restrict lambda
// with an ABSENT AST → hasAst = 0) is served unchanged and the cached restrict silently loses the
// pushdown (Where/Join throws "cannot be lowered", swallowed by the module's try/except → no
// restriction). Bump rejects those v16 blobs → recompile records the AST → the filter applies.
// v18 (2026-07-19): the shortLet peephole (compileCode.cpp) was resurrected. A
// macro-precedence bug — `x % TYPE_DELTA1` expanded to `(x % 1) * N == 0` — had
// left it dead, so every compound assignment `x = a op b` emitted a redundant
// `OP tmp,a,b; LET x,tmp`. With TYPE_DELTAn parenthesised it fuses to `OP x,a,b`
// (one fewer opcode + one fewer ibValue copy; the string case additionally does an
// in-place append, O(n^2) -> O(n)). COMPILER-OUTPUT change, payload layout identical
// — bump so cached blobs recompile to the fused form (old blobs still execute
// correctly, just without the optimisation).
// v19 (2026-08-05): a parameter's default-value descriptor lost its type NAME —
// the bytecode already carries the type as a class id, and a name is spelled from
// that id only when a message needs one.
// v20 (2026-08-05): the global context gained a member — `SessionParameters` — and
// the configuration root gained a second module property, the session module. A blob
// compiled before either existed resolved its names against the smaller context, and
// a cached one is served without ever asking whether that context still holds. Bump
// so every module recompiles against the context as it is now.
// v21 (2026-09-04): a function record gained two bytes after m_kind —
// `m_needsHeapFrame`, which was never written and came back false on every
// cached module (see WriteFunction), and `m_valueCached`, the `Cached` modifier.
// A v20 blob read as v21 would take the parent ref two bytes late and every
// field after it with it, so a stale reader does not fail cleanly — the strict
// version check is what stops it.
// v22 (2026-09-05): NOT A LAYOUT CHANGE — the bytes are identical to v21. What moved is
// what the operands MEAN: the parent-chain visibility rule became one sentence ("a child
// sees its parent entire except the parent's own private locals", ibByteCodeVarInfo::
// IsLocal on both sides), and the compile side stopped answering it with a list that left
// Context / ContextProp out. Names that used to be found further up the chain are now
// found where they live, so their (depth, index) differs — and a v21 blob carries the
// old addresses. Served to this engine, it resolves a name against a frame that is not
// there ("Outer frame not bound at depth 3 / idx 69"), which is exactly what happened
// while this was being tested.
//
// ⭐ THE VERSION IS ABOUT WHETHER THIS ENGINE MAY USE THIS BLOB, not about byte layout.
// The cache key (byteCodeCache.cpp) is this number plus the configuration digest. The
// number lives in byteCodeFormat.h, which is what the compiler rebuilds; the old key
// used core's build stamp, and core is not rebuilt when an opcode changes. The check
// below still rejects a blob that was found under the wrong number.
// v23 (2026-09-05): a function record gained one byte after m_valueCached —
// `m_valueVariadic`, THE THIRD default-false flag this record has lost on the way back.
// A built-in registered with a negative arity (`AppendFunc(wxT("Max"), -1, …)`) declares
// no parameters, so the compiler's two arity checks refuse the caller's first argument
// unless this says "takes what it is given". The compile-context registration set it; the
// bytecode never carried it; so the moment a name resolved through bytecode instead of a
// live context — which is what a cache hit IS — `Max(3, 9)` answered "Too many parameters
// passed to 'Max'". For ANY count, including one, while the help published the signature
// it always had.
//
// ⭐ The same shape as m_needsHeapFrame (v21) and m_valueCached (v21) before it, and the
// question to ask of the NEXT field added here is theirs: not "does it compile" but "who
// writes it back, and what does its absence look like". Absence looks like a feature that
// was never reachable — and a feature nobody can call has no symptoms to report.
// v25 (2026-09-08): the lambda AST carries `In`. The recorder has always produced that kind and the
// serialiser did not accept it — and an unserialisable tree is stored as ABSENT, so
// `.Where(x => x.Status in (…))` pushed down on a fresh compile and silently reverted to a full scan
// on every cache hit. The absence looked exactly like "this predicate cannot be translated", which
// is the same lesson the block above states: ask what the FIELD'S ABSENCE looks like, because that
// is what a cache hit actually delivers.
// v26 (2026-09-08): a Param node of a LAMBDA tree carries the CAPTURE'S ADDRESS — the frame and the
// cell the compiler had already worked out — beside the name. The fold used to re-derive it at run
// time by walking every captured frame and comparing every local's name; now it reads the value
// where the invoked lambda itself would read it. Two s32 after the name, on Param only.
// ⚠ Ask this field the question the block above asks: its ABSENCE reads as "no coordinate", which
// falls back to the name search — correct, only slower — so a stale blob degrades rather than lies.
// The version still moves, because a v25 blob mis-aligns on the two new numbers.
// v27 (2026-09-08): the lambda query TREE is no longer part of the format at all. It was a second
// representation of a body the instructions already hold, and it is derived from them now
// (compiler/lambdaQueryAST.h) — so the blob carries what the runtime executes and nothing beside
// it. LINQ's own instruction set landed with it — NARROW, SEEN, KEEP, RESULT, BUCKET, BUCKET_GET,
// ROW, FIELD — and every added opcode moves TYPE_DELTA1, which renumbers every TYPED opcode: a v26
// blob would execute different instructions, not merely misread a field.
//
// ⭐⭐ AND THIS IS WHERE THE SLICE HAPPENS. Max, 2026-09-08: *"when you take the bytecode and throw
// it into the cache, THAT is when it gets sliced."* Every door here is typed on `ibByteCode` — the
// BASE — so handing it the compiler's `ibByteExtCode` writes the instructions, the constants and the
// symbol tables, and the LINQ section that object also carries is simply not part of what a base is.
// Nothing filters it out: the type does. The compiler keeps its tree (IntelliSense reads it there);
// the blob never had one.
// ⚠ 27 → 28 (2026-09-08): `orderby a, b` compiles to SEVERAL `OPER_LINQ_KEEP` — the first carrying
// the row with key 0, each further one carrying only its key, numbered in the fourth operand. The
// instructions are the same width and a cached blob from before this change is still readable, but
// it was written by a compiler that could not express a second key: a row kept from it would order
// by one key while the source text says two. The layout did not move; the MEANING did, which is
// exactly what this number is for.
// 🛑 28 → 29 (2026-09-09): `OPER_NEW` now carries the CLASS ID in `m_param3.m_numIndex`, decided at
// compile time, and the runtime creates from it instead of reading the class NAME back out of the
// const pool and resolving it again on every execution. A v28 blob has a ZERO there, and zero is
// not a class — it would raise on the first `New` rather than misbehave quietly, but a cache that
// cannot run is still a cache that must not be loaded. The layout did not move; a previously unused
// operand acquired a meaning, which is the same kind of change as the one above.
// 🛑 29 → 30 (2026-09-15): a function's parameter or local called like a binding of the module's object
// resolves to the parameter (compileCode.cpp, "A NAME DECLARED NEARER WINS"). A v29 blob was compiled
// with the binding reading it — the layout is the same and the blob loads, and it would go on counting
// the days of the document's month where the source says the argument's. A cache that answers
// differently from its source must not be loaded.
// 🛑 30 → 31 (2026-09-17): three compilations changed their answer. `New T(args);` written as a statement
// kept its arguments' instructions (a v30 blob built the object with none); a variable declared with a value
// class (`Array rows`) is gated on every assignment (OPER_SET_TYPE after the LET, which a v30 blob does not
// have); and a LINQ filter's comparisons and NOT / AND / OR carry LINQ_THREE_VALUED_NULL in m_param4, so NULL
// is not kept by `where` — a v30 blob's zero there keeps it; and `Not` no longer gates its own result cell
// before computing it (a v30 blob does, and raises on the second row of a filter); and `Not` reads its operand
// only up to the next And / Or (a v30 blob compiled `Not a And b` as `Not (a And b)`). The layout did not move.
// 🛑 31 → 32 (2026-09-21): an ordering key's WAY rides its own `OPER_LINQ_KEEP` (m_param4.m_numArray, 1 =
// descending), and `OPER_LINQ_RESULT` says only "by the keys" (2). A v31 blob wrote the way once, as 1 in the
// RESULT, and a zero in every KEEP - read now, a descending query would come back ascending, quietly.
// 🛑 32 -> 33 (2026-09-24): a COMPARISON'S RESULT IS BOOLEAN AGAIN, so the instruction above it
//    changes. "Is this a comparison" is a range over the operator numbers and it was asked after a
//    declared type had already moved the opcode by a tier, so a typed comparison answered no and
//    its result carried the OPERAND'S class - which made the If over it take the operand's tier and
//    read the operand's field. Cached bytecode written by the old compiler holds that If; the new
//    interpreter writes the comparison's answer with its tag, into another field, and the old If
//    would read the one nobody wrote. And / Or answer a boolean by the same rule and move with it.
// 🛑 33 -> 34 (2026-09-27): A DYNAMIC CLASS ID CARRIES ITS METACLASS (clsid.h: kind | metaclass | metaID), and
//    `AnyRef` / `CatalogRef` are spelled in those bits (any metaID) instead of a name hash. A blob keeps the
//    class ids its code was compiled against — a declared type, a `New` — and a v33 one names classes that
//    are registered under other ids now.
// 🛑 34 -> 35 (2026-09-30): A DATE CONSTANT IS AN ibDateTime (fdatetime.h) - a wall-clock reading counted from the
//    empty date - where a v34 blob holds an instant of the compiling machine's clock.
// 🛑 35 -> 36 (2026-10-09): OPER_ITER left the opcode list (codeDef.h). The ternary is If's shape
//    now — OPER_IF, the chosen branch writing the result, OPER_GOTO over the other — so both
//    branches are no longer computed before the choice. Dropping the enumerator moves every opcode
//    from OPER_GT through OPER_END, and TYPE_DELTA with them. A v35 blob stores the old numbers and
//    would dispatch the wrong handler on every instruction after that slot.
// The constant itself is kAOTFormatVersion in byteCodeFormat.h. It moved out of this
// file so the cache key can name the same number the blob header writes.
[[maybe_unused]] constexpr uint16_t kAOTFlagPortable = 0x0001;   // reserved — host-endian today, no reader yet

// Sentinel for an over-large collection — guards Deserialize against
// reading garbage that pre-allocates GB. Bytecodes have hundreds of
// entries in the worst case, not millions; a hard cap at 16M is well
// past anything realistic.
constexpr uint32_t kAOTSanityMax = 16u * 1024u * 1024u;

// A frame wider than this is not a procedure a compiler emits. It is a count
// that would allocate until the process dies. A million slots is already past
// any module this runtime has compiled.
constexpr long kMaxFrameSlots = 1000000;

// Parent-module depth stamped on an operand. Real chains are a handful of
// rungs; past this the number is not a frame this process could have built.
constexpr long long kMaxModuleDepth = 256;

bool WriteGuid(ibWriterMemory& w, const ibGuid& g) {
	const auto& b = g.bytes();
	w.w(b.data(), 16);
	return true;
}

bool ReadGuid(const ibReaderMemory& r, ibGuid& out) {
	std::array<unsigned char, 16> bytes{};
	r.r(bytes.data(), 16);
	out = ibGuid(bytes);
	return true;
}

bool WriteParamRun(ibWriterMemory& w, const ibParamRunUnit& p) {
	w.w_s64(p.m_numArray);
	w.w_s64(p.m_numIndex);
	return true;
}

void ReadParamRun(const ibReaderMemory& r, ibParamRunUnit& p) {
	p.m_numArray = r.r_s64();
	p.m_numIndex = r.r_s64();
}

bool WriteByteUnit(ibWriterMemory& w, const ibByteUnit& u) {
	w.w_s16(u.m_numOper);
	w.w_u32(u.m_numString);
	w.w_u32(u.m_numLine);
	WriteParamRun(w, u.m_param1);
	WriteParamRun(w, u.m_param2);
	WriteParamRun(w, u.m_param3);
	WriteParamRun(w, u.m_param4);
	w.w_stringZ(u.m_strModuleName);
	w.w_stringZ(u.m_strDocPath);
	w.w_stringZ(u.m_strFileName);
	return true;
}

void ReadByteUnit(const ibReaderMemory& r, ibByteUnit& u) {
	u.m_numOper   = r.r_s16();
	u.m_numString = r.r_u32();
	u.m_numLine   = r.r_u32();
	ReadParamRun(r, u.m_param1);
	ReadParamRun(r, u.m_param2);
	ReadParamRun(r, u.m_param3);
	ReadParamRun(r, u.m_param4);
	r.r_stringZ(u.m_strModuleName);
	r.r_stringZ(u.m_strDocPath);
	r.r_stringZ(u.m_strFileName);
}

// ibValue payload — only primitives appear in m_listConst at compile
// time (TYPE_REFFER / TYPE_VALUE / TYPE_ENUM never reach the const
// pool — they live as runtime references, not bytecode literals).
// Any unexpected type triggers a serialise failure → caller treats
// as cache miss.
bool WriteConstValue(ibWriterMemory& w, const ibValue& v) {
	const uint8_t tc = (uint8_t)v.m_typeClass;
	w.w_u8(tc);
	switch (v.m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
		return true;
	case ibValueTypes::TYPE_BOOLEAN:
		w.w_u8(v.m_bData ? 1 : 0);
		return true;
	case ibValueTypes::TYPE_NUMBER: {
		// Exact-decimal binary form via ibNumber's own buffer
		// (same bytes that go into Firebird SQL_INT128 columns).
		// Length-prefixed so the reader doesn't need a chunk
		// wrapper around it.
		wxMemoryBuffer buf = v.m_fData.GetBuffer();
		const uint32_t len = (uint32_t)buf.GetDataLen();
		w.w_u32(len);
		if (len > 0) w.w(buf.GetData(), len);
		return true;
	}
	case ibValueTypes::TYPE_DATE:
		w.w_s64(v.m_dData.GetValue());
		return true;
	case ibValueTypes::TYPE_STRING:
		w.w_stringZ(v.GetString());
		return true;
	default:
		return false; // TYPE_REFFER / TYPE_VALUE / TYPE_ENUM / TYPE_OLE
	}
}

bool ReadConstValue(const ibReaderMemory& r, ibValue& v) {
	const uint8_t tc = r.r_u8();
	v.SetType((ibValueTypes)tc);

	// Write the payload while the value is still mutable, THEN mark it
	// readonly. Order matters since Phase 2: the string payload moved into
	// the union and SetString() now Resets the value first to free the old
	// ibString — and Reset() rejects writes to a readonly value. Setting the
	// flag before SetString() therefore threw "Attempt to assign a value to a
	// write-denied variable" for every string const (AOT cache load of any
	// module with a string literal). The non-string cases write members
	// directly and were unaffected, which is why only strings tripped it.
	bool ok = true;
	switch ((ibValueTypes)tc) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		v.m_bData = (r.r_u8() != 0);
		break;
	case ibValueTypes::TYPE_NUMBER: {
		const uint32_t len = r.r_u32();
		if (len > kAOTSanityMax) { ok = false; break; }
		wxMemoryBuffer buf;
		if (len > 0) {
			r.r(buf.GetAppendBuf(len), (int)len);
			buf.UngetAppendBuf(len);
		}
		ok = v.m_fData.SetBuffer(buf);
		break;
	}
	case ibValueTypes::TYPE_DATE:
		v.m_dData = ibDateTime(r.r_s64());
		break;
	case ibValueTypes::TYPE_STRING:
		v.SetString(r.r_stringZ());
		break;
	default:
		ok = false;
		break;
	}

	// Const-pool entries are compile-time literals — readonly by definition.
	// Without this flag, runtime arg-binding on a literal arg falls through to
	// the ref-binding branch (procUnit.cpp:828) and triggers "Attempt to write
	// to a constant value" when the callee writes through the slot. Fresh-
	// compile path sets this on every const it pushes; the AOT format doesn't
	// carry the bit (it's implicit for the whole list).
	v.m_bReadOnly = true;
	return ok;
}

bool WriteVarInfo(ibWriterMemory& w, const ibByteCode::ibByteCodeVarInfo& v) {
	w.w_s32((int32_t)v.m_slotIndex);
	w.w_u64((uint64_t)v.m_clsid);
	w.w_u8((uint8_t)v.m_kind);
	w.w_s32((int32_t)v.m_parentRef);
	w.w_stringZ(v.m_strRealName);
	w.w_stringZ(v.m_strContext);
	w.w_s32((int32_t)v.m_scopeDepth); // v9
	return true;
}

void ReadVarInfo(const ibReaderMemory& r, ibByteCode::ibByteCodeVarInfo& v) {
	v.m_slotIndex = (long)r.r_s32();
	v.m_clsid     = (ibClassID)r.r_u64();
	v.m_kind      = (ibVarKind)r.r_u8();
	v.m_parentRef = (long)r.r_s32();
	r.r_stringZ(v.m_strRealName);
	r.r_stringZ(v.m_strContext);
	v.m_scopeDepth = (int)r.r_s32(); // v9
}

bool WriteParam(ibWriterMemory& w, const ibByteCode::ibByteParam& p) {
	w.w_u8(p.m_bByValue ? 1 : 0);
	w.w_u64((uint64_t)p.m_clsid);
	WriteParamRun(w, p.m_defaultValue);
	w.w_stringZ(p.m_strName);
	return true;
}

void ReadParam(const ibReaderMemory& r, ibByteCode::ibByteParam& p) {
	p.m_bByValue = (r.r_u8() != 0);
	p.m_clsid  = (ibClassID)r.r_u64();
	ReadParamRun(r, p.m_defaultValue);
	r.r_stringZ(p.m_strName);
}

// ⭐⭐ THE QUERY-TREE SERIALISER IS GONE, and with it a whole class of defect.
//
// It was a whitelist (AstSerializable) deciding which node kinds could travel, plus a recursive
// writer and reader. A kind the whitelist did not know made the tree UNSERIALISABLE, and an
// unserialisable tree was stored as ABSENT — which downstream reads as "this predicate cannot be
// translated". So a filter using such a kind pushed down on a fresh compile and silently reverted
// to a full scan on every cache hit, and a cache hit is the normal case.
//
// Nothing replaces it: the tree is DERIVED from the instructions wherever it is wanted
// (compiler/lambdaQueryAST.h), so there is no second representation to keep, to gate or to version.

bool WriteFunction(ibWriterMemory& w, const ibByteCode::ibByteFunction& f) {
	w.w_s32((int32_t)f.m_lCodeLine);
	w.w_u8(f.m_bCodeRet ? 1 : 0);
	w.w_s32((int32_t)f.m_lVarCount);
	w.w_u64((uint64_t)f.m_returnClsid);
	w.w_u8((uint8_t)f.m_kind);
	// m_needsHeapFrame was NOT written before v21, and a default-false bool that
	// the reconstruction path has to restore is exactly the shape of a silent
	// defect: a module served from the cache came back with the flag cleared on
	// every function, so a cross-module call to an exported function holding an
	// inner lambda emitted OPER_CALL instead of OPER_CALL_CLOSURE and the capture
	// dangled — only on the runs that hit the cache.
	w.w_u8(f.m_needsHeapFrame ? 1 : 0);
	w.w_u8(f.m_valueCached ? 1 : 0);
	w.w_u8(f.m_valueVariadic ? 1 : 0);   // v23 - see the note on the constant
	w.w_s32((int32_t)f.m_parentRef);
	w.w_stringZ(f.m_strRealName);
	w.w_stringZ(f.m_strContext);

	// m_listParam — count is the single source of truth (m_lCodeParamCount
	// is gone). Each entry carries its own name via WriteParam.
	w.w_u32((uint32_t)f.m_listParam.size());
	for (const auto& p : f.m_listParam)
		WriteParam(w, p);

	// m_listLocals — function-frame symbol table.
	w.w_u32((uint32_t)f.m_listLocals.size());
	for (const auto& v : f.m_listLocals)
		WriteVarInfo(w, v);

	// ⭐ NOTHING IS WRITTEN FOR THE QUERY TREE. It used to travel here as a presence byte plus a
	// recursive dump, gated by a whitelist — and an unserialisable tree was stored as ABSENT, which
	// read downstream as "this predicate cannot be translated". It is derived from the instructions
	// now, so the cache carries what the runtime executes and nothing beside it.

	return true;
}

bool ReadFunction(const ibReaderMemory& r, ibByteCode::ibByteFunction& f) {
	f.m_lCodeLine       = (long)r.r_s32();
	f.m_bCodeRet        = (r.r_u8() != 0);
	f.m_lVarCount       = (long)r.r_s32();
	f.m_returnClsid     = (ibClassID)r.r_u64();
	f.m_kind            = (ibFnKind)r.r_u8();
	f.m_needsHeapFrame  = (r.r_u8() != 0);
	f.m_valueCached     = (r.r_u8() != 0);
	f.m_valueVariadic   = (r.r_u8() != 0);   // v23
	f.m_parentRef       = (long)r.r_s32();
	r.r_stringZ(f.m_strRealName);
	r.r_stringZ(f.m_strContext);

	uint32_t paramCount = r.r_u32();
	if (paramCount > kAOTSanityMax) return false;
	f.m_listParam.resize(paramCount);
	for (uint32_t i = 0; i < paramCount; ++i)
		ReadParam(r, f.m_listParam[i]);

	uint32_t localsCount = r.r_u32();
	if (localsCount > kAOTSanityMax) return false;
	f.m_listLocals.resize(localsCount);
	for (uint32_t i = 0; i < localsCount; ++i)
		ReadVarInfo(r, f.m_listLocals[i]);


	return true;
}

// The base opcode, or -1 when the number is not one this interpreter runs.
// A typed tier is `base + n * (OPER_END + 1)` for n in 1..4; the operands
// stay the base opcode's operands.
int BaseOpcode(int oper)
{
	if (oper < 0)
		return -1;
	const int span = OPER_END + 1;
	if (oper < span)
		return oper;
	const int tier = oper / span;
	if (tier < 1 || tier > 4)
		return -1;
	return oper % span;
}

// A value operand. A current-frame tag must name a slot the frame has; a
// constant tag must name a pool entry. An outer-frame tag is a depth, and
// the chain that depth walks is built at run time — only an absurd depth
// is refused here.
bool ValueSlotOk(const ibParamRunUnit& p, long frame, long nConst)
{
	const long long tag = p.m_numArray;
	const long long idx = p.m_numIndex;
	if (tag == DEF_VAR_CONST)
		return idx >= 0 && idx < nConst;
	if (tag == DEF_VAR_SKIP || tag == DEF_VAR_DEFAULT || tag == DEF_VAR_NORET)
		return true;
	if (tag <= 0)
		return idx >= 0 && idx < frame;
	return tag > 0 && tag <= kMaxModuleDepth;
}

bool ConstIndexOk(long long idx, long nConst)
{
	return idx >= 0 && idx < nConst;
}

// The interpreter sets the line to `target - 1` and then steps. A negative
// target steps into an index the tape does not have. A target past the end
// simply leaves the loop.
bool JumpOk(long long target, long nCode)
{
	return target >= 0 && target <= nCode;
}

bool InstructionFits(const ibByteUnit& u, long frame, long ip, long nCode, long nConst, long nFunc)
{
	const int op = BaseOpcode(u.m_numOper);
	// A number this interpreter does not run is not a module. Passing it
	// used to publish the blob and then miss the cache on every start.
	if (op < 0)
		return false;

	const ibParamRunUnit& p1 = u.m_param1;
	const ibParamRunUnit& p2 = u.m_param2;
	const ibParamRunUnit& p3 = u.m_param3;
	const ibParamRunUnit& p4 = u.m_param4;

	switch (op) {
	case OPER_ADD: case OPER_SUB: case OPER_DIV: case OPER_MULT: case OPER_MOD:
	case OPER_AND: case OPER_OR:
	case OPER_EQ: case OPER_NE: case OPER_GT: case OPER_LS: case OPER_GE: case OPER_LE:
	case OPER_GET_ARRAY: case OPER_SET_ARRAY:
	case OPER_LINQ_SEEN: case OPER_LINQ_BUCKET: case OPER_LINQ_BUCKET_GET:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst) && ValueSlotOk(p3, frame, nConst);

	case OPER_NOT: case OPER_INVERT: case OPER_LET:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst);

	case OPER_CONST:
		return ValueSlotOk(p1, frame, nConst) && ConstIndexOk(p2.m_numIndex, nConst);

	case OPER_CONSTN:
		return ValueSlotOk(p1, frame, nConst);

	case OPER_IF:
		return ValueSlotOk(p1, frame, nConst) && JumpOk(p2.m_numIndex, nCode);

	case OPER_FOR:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst) && JumpOk(p3.m_numIndex, nCode);

	case OPER_FOREACH:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst)
			&& ValueSlotOk(p3, frame, nConst) && JumpOk(p4.m_numIndex, nCode);

	case OPER_NEXT: case OPER_NEXT_ITER:
		return ValueSlotOk(p1, frame, nConst) && JumpOk(p2.m_numIndex, nCode);

	case OPER_RET:
		if (p1.m_numIndex == DEF_VAR_NORET)
			return true;
		return ValueSlotOk(p1, frame, nConst);

	case OPER_RAISE_T:
		return ValueSlotOk(p1, frame, nConst);

	case OPER_GOTO: case OPER_ENDTRY: case OPER_TRY:
		return JumpOk(p1.m_numIndex, nCode);

	case OPER_SET: case OPER_SETREF:
		return ValueSlotOk(p1, frame, nConst);

	case OPER_SETCONST:
		return p1.m_numIndex < 0 || ConstIndexOk(p1.m_numIndex, nConst);

	case OPER_SET_A: case OPER_SET_SCOPE:
		return ValueSlotOk(p1, frame, nConst) && ConstIndexOk(p2.m_numIndex, nConst) && ValueSlotOk(p3, frame, nConst);

	case OPER_GET_A: case OPER_GET_SCOPE: case OPER_CALL_METHOD:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst) && ConstIndexOk(p3.m_numIndex, nConst);

	case OPER_GET_EXTERN: case OPER_GET_CONTEXT:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst);

	case OPER_SET_EXTERN: case OPER_SET_CONTEXT:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p3, frame, nConst);

	case OPER_SET_TYPE: case OPER_FUNC_PARAM: case OPER_FUNC_LOCAL:
		return ValueSlotOk(p1, frame, nConst);

	case OPER_NEW:
		return ValueSlotOk(p1, frame, nConst)
			&& p2.m_numArray >= 0 && p2.m_numArray <= kMaxFrameSlots;

	case OPER_LFUNC:
		// The end address must land on an instruction (the matching ENDLFUNC),
		// and the third operand is the function's own entry in m_listFunc.
		return ValueSlotOk(p1, frame, nConst)
			&& p2.m_numIndex > ip && p2.m_numIndex < nCode
			&& p3.m_numIndex >= 0 && p3.m_numIndex < nFunc;

	case OPER_CALL: case OPER_CALL_CLOSURE:
		if (!ValueSlotOk(p1, frame, nConst))
			return false;
		if (p2.m_numArray < 0 || p2.m_numArray > kMaxModuleDepth)
			return false;
		if (p2.m_numArray == 0 && !JumpOk(p2.m_numIndex, nCode))
			return false;
		if (p3.m_numArray < 0 || p3.m_numIndex < 0)
			return false;
		if (p3.m_numIndex > kMaxFrameSlots || p3.m_numArray > kMaxFrameSlots)
			return false;
		// The frame has to cover every argument the call then writes into it.
		return p3.m_numIndex >= p3.m_numArray;

	case OPER_CALL_LAMBDA: case OPER_CALL_LINQ:
		if (!ValueSlotOk(p1, frame, nConst))
			return false;
		if (op == OPER_CALL_LAMBDA && !ValueSlotOk(p4, frame, nConst))
			return false;
		if (op == OPER_CALL_LINQ && !ValueSlotOk(p2, frame, nConst))
			return false;
		if (op == OPER_CALL_LINQ)
			return p3.m_numArray >= 0 && p3.m_numArray <= kMaxFrameSlots;
		return p2.m_numIndex >= 0 && p2.m_numIndex <= kMaxFrameSlots;

	case OPER_LINQ_NARROW:
		if (!ValueSlotOk(p1, frame, nConst) || !ValueSlotOk(p3, frame, nConst) || !ValueSlotOk(p4, frame, nConst))
			return false;
		if (p2.m_numIndex == 0)
			return true;
		return JumpOk(p2.m_numIndex, nCode) && JumpOk(p2.m_numArray, nCode) && p2.m_numIndex <= p2.m_numArray;

	case OPER_LINQ_KEEP:
		if (!ValueSlotOk(p1, frame, nConst))
			return false;
		if (p2.m_numArray != DEF_VAR_SKIP && !ValueSlotOk(p2, frame, nConst))
			return false;
		if (p3.m_numArray != DEF_VAR_SKIP && !ValueSlotOk(p3, frame, nConst))
			return false;
		return p4.m_numIndex >= 0;

	case OPER_LINQ_RESULT:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst);

	case OPER_LINQ_ROW:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst) && ConstIndexOk(p3.m_numIndex, nConst);

	case OPER_LINQ_FIELD:
		return ValueSlotOk(p1, frame, nConst) && ValueSlotOk(p2, frame, nConst) && p3.m_numIndex >= 0;

	// Markers. They name no slot. A typed tier of one of them is not a
	// marker (BaseOpcode already folded those back to the base).
	case OPER_NOP:
	case OPER_RAISE:
	case OPER_FUNC:
	case OPER_ENDFUNC:
	case OPER_ENDLFUNC:
	case OPER_END:
	case OPER_CTX_BEGIN:
	case OPER_CTX_END:
		return true;

	default:
		// A known number with no layout here is still unknown to this check.
		// Refuse it. A new opcode has to say which of its operands are slots.
		return false;
	}
}

// A blob can be a well-framed byte string and still name a slot the frame
// does not have. The interpreter used that index as a native subscript.
// Refuse it here, before the cache publishes the bytecode as compiled.
bool OperandsFitTheFrames(const ibByteCode& bc)
{
	const long nCode = (long)bc.m_listCode.size();
	const long nConst = (long)bc.m_listConst.size();
	const long nVar = (long)bc.m_listVar.size();
	const long nFunc = (long)bc.m_listFunc.size();

	if (bc.m_lStartModule < 0 || bc.m_lStartModule > nCode)
		return false;
	if (bc.m_lVarCount < 0 || bc.m_lVarCount > kMaxFrameSlots)
		return false;
	if (bc.m_dependencyIds.size() != bc.m_dependencyVersions.size())
		return false;

	for (const auto& v : bc.m_listVar) {
		if (v.m_parentRef < -1 || v.m_parentRef >= nVar)
			return false;
	}

	// Which frame an instruction runs in. Module body uses the module's
	// count; a function body uses that function's count. An inner lambda
	// is painted after the function that contains it, so it keeps its own.
	std::vector<long> frameAt((size_t)nCode, bc.m_lVarCount);
	std::vector<size_t> order((size_t)nFunc);
	for (size_t i = 0; i < order.size(); ++i)
		order[i] = i;
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		return bc.m_listFunc[a].m_lCodeLine < bc.m_listFunc[b].m_lCodeLine;
	});

	for (size_t fi : order) {
		const ibByteCode::ibByteFunction& fn = bc.m_listFunc[fi];
		if (fn.m_lVarCount < 0 || fn.m_lVarCount > kMaxFrameSlots)
			return false;
		if ((long)fn.m_listParam.size() > fn.m_lVarCount)
			return false;
		for (const auto& loc : fn.m_listLocals) {
			if (loc.m_slotIndex < 0 || loc.m_slotIndex >= fn.m_lVarCount)
				return false;
		}
		for (const auto& p : fn.m_listParam) {
			if (p.m_defaultValue.m_numArray == DEF_VAR_CONST
				&& !ConstIndexOk(p.m_defaultValue.m_numIndex, nConst))
				return false;
		}
		if (fn.m_parentRef < -1 || fn.m_parentRef >= nVar)
			return false;
		if (fn.m_lCodeLine < -1 || (fn.m_lCodeLine >= 0 && fn.m_lCodeLine >= nCode))
			return false;
		if (fn.m_lCodeLine < 0)
			continue;

		const int entry = BaseOpcode(bc.m_listCode[(size_t)fn.m_lCodeLine].m_numOper);
		if (entry != OPER_FUNC && entry != OPER_LFUNC)
			continue;
		const int open = entry;
		const int close = (entry == OPER_LFUNC) ? OPER_ENDLFUNC : OPER_ENDFUNC;
		int depth = 0;
		bool closed = false;
		for (long ip = fn.m_lCodeLine; ip < nCode; ++ip) {
			const int at = BaseOpcode(bc.m_listCode[(size_t)ip].m_numOper);
			if (at == open)
				++depth;
			frameAt[(size_t)ip] = fn.m_lVarCount;
			if (at == close) {
				--depth;
				if (depth <= 0) {
					closed = true;
					break;
				}
			}
		}
		if (!closed)
			return false;
	}

	for (long ip = 0; ip < nCode; ++ip) {
		if (!InstructionFits(bc.m_listCode[(size_t)ip], frameAt[(size_t)ip], ip, nCode, nConst, nFunc))
			return false;
	}
	return true;
}

} // anonymous

bool ibByteCode::SerializeAOT(ibWriterMemory& w) const
{
	// Header.
	w.w_u32(kAOTMagic);
	w.w_u16(kAOTFormatVersion);
	w.w_u16(0); // flags

	WriteGuid(w, m_id);
	WriteGuid(w, m_version);
	w.w_u64((uint64_t)m_descriptorClsid);

	w.w_s32((int32_t)m_lStartModule);
	w.w_s32((int32_t)m_lVarCount);
	w.w_u8(m_bExpressionOnly ? 1 : 0);
	w.w_stringZ(m_strModuleName);

	// m_listCode.
	w.w_u32((uint32_t)m_listCode.size());
	for (const auto& u : m_listCode) {
		if (!WriteByteUnit(w, u)) return false;
	}

	// m_listConst.
	w.w_u32((uint32_t)m_listConst.size());
	for (const auto& v : m_listConst) {
		if (!WriteConstValue(w, v)) return false;
	}

	// m_listVar.
	w.w_u32((uint32_t)m_listVar.size());
	for (const auto& v : m_listVar) {
		if (!WriteVarInfo(w, v)) return false;
	}

	// m_listFunc.
	w.w_u32((uint32_t)m_listFunc.size());
	for (const auto& f : m_listFunc) {
		if (!WriteFunction(w, f)) return false;
	}

	// Dependencies — id + version vectors. Live pointers are not
	// persisted; the loader populates m_dependencies by looking each
	// id up in a session-level registry.
	w.w_u32((uint32_t)m_dependencyIds.size());
	for (const auto& g : m_dependencyIds)
		WriteGuid(w, g);

	w.w_u32((uint32_t)m_dependencyVersions.size());
	for (const auto& g : m_dependencyVersions)
		WriteGuid(w, g);

	return true;
}

bool ibByteCode::DeserializeAOT(const ibReaderMemory& r)
{
	// Empty out anything that was already in *this — safer than
	// asserting on a clean target. AOT load is a fresh-construct
	// path in the cache code, but defensive Reset() means callers
	// can also use this on an existing bc to refresh-from-cache.
	Reset();

	// Header.
	if (r.elapsed() < (int)(sizeof(uint32_t) + sizeof(uint16_t) * 2))
		return false;

	const uint32_t magic = r.r_u32();
	if (magic != kAOTMagic) return false;

	const uint16_t formatVersion = r.r_u16();
	if (formatVersion != kAOTFormatVersion) return false;

	const uint16_t flags = r.r_u16();
	(void)flags; // kAOTFlagPortable not yet honoured

	if (!ReadGuid(r, m_id))      return false;
	if (!ReadGuid(r, m_version)) return false;
	m_descriptorClsid = (ibClassID)r.r_u64();

	m_lStartModule    = (long)r.r_s32();
	m_lVarCount       = (long)r.r_s32();
	m_bExpressionOnly = (r.r_u8() != 0);
	r.r_stringZ(m_strModuleName);

	// m_listCode.
	uint32_t codeCount = r.r_u32();
	if (codeCount > kAOTSanityMax) return false;
	m_listCode.resize(codeCount);
	for (uint32_t i = 0; i < codeCount; ++i)
		ReadByteUnit(r, m_listCode[i]);

	// m_listConst.
	uint32_t constCount = r.r_u32();
	if (constCount > kAOTSanityMax) return false;
	m_listConst.resize(constCount);
	for (uint32_t i = 0; i < constCount; ++i) {
		if (!ReadConstValue(r, m_listConst[i])) return false;
	}

	// m_listVar.
	uint32_t varCount = r.r_u32();
	if (varCount > kAOTSanityMax) return false;
	m_listVar.resize(varCount);
	for (uint32_t i = 0; i < varCount; ++i)
		ReadVarInfo(r, m_listVar[i]);

	// m_listFunc.
	uint32_t funcCount = r.r_u32();
	if (funcCount > kAOTSanityMax) return false;
	m_listFunc.resize(funcCount);
	for (uint32_t i = 0; i < funcCount; ++i) {
		if (!ReadFunction(r, m_listFunc[i])) return false;
	}

	// Dependency id / version vectors. m_dependencies (live
	// pointers) stays empty — caller resolves via session
	// registry once all bytecodes are loaded.
	uint32_t depIdCount = r.r_u32();
	if (depIdCount > kAOTSanityMax) return false;
	m_dependencyIds.resize(depIdCount);
	for (uint32_t i = 0; i < depIdCount; ++i)
		ReadGuid(r, m_dependencyIds[i]);

	uint32_t depVerCount = r.r_u32();
	if (depVerCount > kAOTSanityMax) return false;
	m_dependencyVersions.resize(depVerCount);
	for (uint32_t i = 0; i < depVerCount; ++i)
		ReadGuid(r, m_dependencyVersions[i]);

	// The bytes were all there. They are still not a module until every
	// index they use is one the frame, the constant pool, or the tape has.
	// A cache row that fails this is a miss: the caller compiles the source.
	if (!OperandsFitTheFrames(*this)) {
		Reset();
		return false;
	}

	m_bCompile = true;
	return true;
}
