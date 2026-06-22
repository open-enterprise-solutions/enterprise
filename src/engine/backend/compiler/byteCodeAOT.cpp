// Ahead-Of-Time (AOT) bytecode persistence — Step 1 of the AOT cache
// (docs/next-session-aot.md). Writes a compiled ibByteCode into a flat
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
#include "backend/query/queryAst.h"      // ibQueryAstExpr — m_lambdaExprAst payload (v14)
#include "backend/fileSystem/fs.h"

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
constexpr uint16_t kAOTFormatVersion = 15;
constexpr uint16_t kAOTFlagPortable  = 0x0001;       // unused — host-endian today

// Sentinel for an over-large collection — guards Deserialize against
// reading garbage that pre-allocates GB. Bytecodes have hundreds of
// entries in the worst case, not millions; a hard cap at 16M is well
// past anything realistic.
constexpr uint32_t kAOTSanityMax = 16u * 1024u * 1024u;

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
		w.w_s64((int64_t)v.m_dData);
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
		v.m_dData = (wxLongLong_t)r.r_s64();
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
	w.w_u8(v.m_bScoped ? 1 : 0);
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
	v.m_bScoped   = (r.r_u8() != 0);
	v.m_kind      = (ibVarKind)r.r_u8();
	v.m_parentRef = (long)r.r_s32();
	r.r_stringZ(v.m_strRealName);
	r.r_stringZ(v.m_strContext);
	v.m_scopeDepth = (int)r.r_s32(); // v9
}

bool WriteParam(ibWriterMemory& w, const ibByteCode::ibByteParam& p) {
	w.w_u8(p.m_bByRef ? 1 : 0);
	w.w_u64((uint64_t)p.m_clsid);
	WriteParamRun(w, p.m_defaultValue);
	w.w_stringZ(p.m_defaultValue.m_strType);
	w.w_stringZ(p.m_strName);
	return true;
}

void ReadParam(const ibReaderMemory& r, ibByteCode::ibByteParam& p) {
	p.m_bByRef = (r.r_u8() != 0);
	p.m_clsid  = (ibClassID)r.r_u64();
	ReadParamRun(r, p.m_defaultValue);
	r.r_stringZ(p.m_defaultValue.m_strType);
	r.r_stringZ(p.m_strName);
}

// --- m_lambdaExprAst (L4-2) — the recorded lambda body as the L4 query AST.
// The recorder's subset is closed and SMALL (Column / Literal / Param / Arith /
// Compare / Logical / Not; literals are primitives only), so the encoding is a
// straight recursive kind-tagged dump. A node outside the subset (future
// recorder growth before this list is extended) makes the AST UNSERIALISABLE —
// the writer then stores "absent" (presence byte 0): the cached module loses
// only the pushdown (RAM floor), never correctness. The reader validates kinds
// and depth; any mismatch fails the whole load → ordinary cache miss.

constexpr uint32_t kAOTMaxAstDepth = 256;

bool AstSerializable(const ibQueryAstExpr& e, uint32_t depth = 0)
{
	if (depth > kAOTMaxAstDepth) return false;
	switch (e.m_kind) {
	case ibQueryAstExprKind::Column:
	case ibQueryAstExprKind::Param:
		return true;
	case ibQueryAstExprKind::Literal:
		switch (e.m_literal.GetType()) {
		case ibValueTypes::TYPE_EMPTY: case ibValueTypes::TYPE_NULL:
		case ibValueTypes::TYPE_BOOLEAN: case ibValueTypes::TYPE_NUMBER:
		case ibValueTypes::TYPE_DATE: case ibValueTypes::TYPE_STRING:
			return true;
		default:
			return false;
		}
	case ibQueryAstExprKind::Not:
		return e.m_lhs && AstSerializable(*e.m_lhs, depth + 1);
	case ibQueryAstExprKind::Arith:
	case ibQueryAstExprKind::Compare:
	case ibQueryAstExprKind::Logical:
		return e.m_lhs && e.m_rhs
			&& AstSerializable(*e.m_lhs, depth + 1) && AstSerializable(*e.m_rhs, depth + 1);
	default:
		return false;
	}
}

void WriteLambdaAst(ibWriterMemory& w, const ibQueryAstExpr& e)
{
	w.w_u8((uint8_t)e.m_kind);
	w.w_u32(e.m_line);
	w.w_u32(e.m_col);
	switch (e.m_kind) {
	case ibQueryAstExprKind::Column:
		w.w_u32((uint32_t)e.m_path.size());
		for (const wxString& s : e.m_path) w.w_stringZ(s);
		break;
	case ibQueryAstExprKind::Literal:
		WriteConstValue(w, e.m_literal);
		break;
	case ibQueryAstExprKind::Param:
		w.w_stringZ(e.m_paramName);
		break;
	case ibQueryAstExprKind::Not:
		WriteLambdaAst(w, *e.m_lhs);
		break;
	case ibQueryAstExprKind::Arith:
		w.w_u8((uint8_t)e.m_arith);
		WriteLambdaAst(w, *e.m_lhs); WriteLambdaAst(w, *e.m_rhs);
		break;
	case ibQueryAstExprKind::Compare:
		w.w_u8((uint8_t)e.m_cmp);
		WriteLambdaAst(w, *e.m_lhs); WriteLambdaAst(w, *e.m_rhs);
		break;
	case ibQueryAstExprKind::Logical:
		w.w_u8(e.m_isOr ? 1 : 0);
		WriteLambdaAst(w, *e.m_lhs); WriteLambdaAst(w, *e.m_rhs);
		break;
	default:
		break;   // unreachable — AstSerializable gates the call
	}
}

ibQueryAstExprPtr ReadLambdaAst(const ibReaderMemory& r, uint32_t depth = 0)
{
	if (depth > kAOTMaxAstDepth) return nullptr;
	const ibQueryAstExprKind kind = (ibQueryAstExprKind)r.r_u8();
	ibQueryAstExprPtr e = ibQueryAstExpr::Make(kind);
	e->m_line = r.r_u32();
	e->m_col  = r.r_u32();
	switch (kind) {
	case ibQueryAstExprKind::Column: {
		const uint32_t count = r.r_u32();
		if (count == 0 || count > kAOTSanityMax) return nullptr;
		e->m_path.resize(count);
		for (uint32_t i = 0; i < count; ++i) r.r_stringZ(e->m_path[i]);
		return e;
	}
	case ibQueryAstExprKind::Literal:
		return ReadConstValue(r, e->m_literal) ? e : nullptr;
	case ibQueryAstExprKind::Param:
		r.r_stringZ(e->m_paramName);
		return e->m_paramName.IsEmpty() ? nullptr : e;
	case ibQueryAstExprKind::Not:
		e->m_lhs = ReadLambdaAst(r, depth + 1);
		return e->m_lhs ? e : nullptr;
	case ibQueryAstExprKind::Arith:
		e->m_arith = (ibQueryArithOp)r.r_u8();
		e->m_lhs = ReadLambdaAst(r, depth + 1);
		e->m_rhs = e->m_lhs ? ReadLambdaAst(r, depth + 1) : nullptr;
		return e->m_rhs ? e : nullptr;
	case ibQueryAstExprKind::Compare:
		e->m_cmp = (ibQueryCompareOp)r.r_u8();
		e->m_lhs = ReadLambdaAst(r, depth + 1);
		e->m_rhs = e->m_lhs ? ReadLambdaAst(r, depth + 1) : nullptr;
		return e->m_rhs ? e : nullptr;
	case ibQueryAstExprKind::Logical:
		e->m_isOr = (r.r_u8() != 0);
		e->m_lhs = ReadLambdaAst(r, depth + 1);
		e->m_rhs = e->m_lhs ? ReadLambdaAst(r, depth + 1) : nullptr;
		return e->m_rhs ? e : nullptr;
	default:
		return nullptr;   // unknown kind — corrupt / future format
	}
}

bool WriteFunction(ibWriterMemory& w, const ibByteCode::ibByteFunction& f) {
	w.w_s32((int32_t)f.m_lCodeLine);
	w.w_u8(f.m_bCodeRet ? 1 : 0);
	w.w_s32((int32_t)f.m_lVarCount);
	w.w_u64((uint64_t)f.m_returnClsid);
	w.w_u8((uint8_t)f.m_kind);
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

	// m_lambdaExprAst (v14) — presence byte + the recursive dump. An AST outside
	// the serialisable subset stores as ABSENT: the cached module degrades to the
	// RAM pipeline for that lambda, correctness unaffected.
	const bool hasAst = f.m_lambdaExprAst && AstSerializable(*f.m_lambdaExprAst);
	w.w_u8(hasAst ? 1 : 0);
	if (hasAst)
		WriteLambdaAst(w, *f.m_lambdaExprAst);

	return true;
}

bool ReadFunction(const ibReaderMemory& r, ibByteCode::ibByteFunction& f) {
	f.m_lCodeLine       = (long)r.r_s32();
	f.m_bCodeRet        = (r.r_u8() != 0);
	f.m_lVarCount       = (long)r.r_s32();
	f.m_returnClsid     = (ibClassID)r.r_u64();
	f.m_kind            = (ibFnKind)r.r_u8();
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

	// m_lambdaExprAst (v14) — presence byte + the recursive dump; a malformed
	// payload fails the whole load (ordinary cache miss → recompile).
	if (r.r_u8() != 0) {
		f.m_lambdaExprAst = ReadLambdaAst(r);
		if (!f.m_lambdaExprAst) return false;
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

	m_bCompile = true;
	return true;
}
