////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : compile module — the LINQ half
////////////////////////////////////////////////////////////////////////////
//
// SPLIT OUT OF compileCode.cpp, 2026-09-08, at seven and a half thousand lines — of which THIS was
// three and a half. Not a boundary of design: it is the same class, the same compile, the same
// single pass, and every function here is an `ibCompileCode::` member. A translation unit is a unit
// of BUILDING, and one that big is rebuilt whole for a comment.
//
// ⭐ WHAT MAKES THE CUT CLEAN, and it is worth saying because it is what a reader will check first:
// the file-local helpers this half uses — the chain reader, the verb predicates, the body-shape
// tests, the column namer, the parameter binders — were ALREADY in one anonymous namespace and had
// no caller outside the LINQ functions. Nothing was made public to allow the move, and nothing
// crossed the line in the other direction: the one thing this half read from the other file's scope
// was the code style, which the class has always had an accessor for (`GetCodeStyle`).
//
// So the two halves talk through the class and through the bytecode, exactly as they did when they
// shared a file. What lives here:
//
//   * the BLOCK road   — `from … where … select …`, compiled to a foreach and its clauses
//                        (CompileLinqExpression / CompileLinqBlock / CompileLinqJoin);
//   * the RESTRICT road — an access policy folding joins and a filter into somebody else's query
//                        (CompileRestrictExpression / EmitRestrictBody);
//   * the CHAIN road   — `src.Where(…).Select(…).ToArray()` compiled to the same loop, and the
//                        FOLD of a chain into a foreach the person wrote themselves
//                        (CompileLinqChain / TryFoldLinqChainSource / EmitLinqChainClauses);
//   * `EmitLambdaBody`, which is how a lambda written at a call site becomes instructions of the
//     loop it was written for rather than a frame of its own.
//
// The tree those roads build is a SECTION OF THE BYTECODE (byteCodeLINQ.h) and outlives every scope
// that reads it — see docs/linq.md §0.2h-ter.
//
////////////////////////////////////////////////////////////////////////////

#include "compileCode.h"
#include "codeDef.h"

#include "backend/diagnostics/journal.h"   // what a chain folded, and where the loop was emitted

#include <algorithm>                       // std::any_of — the column namer asks a taken list

#pragma warning(push)
#pragma warning(disable : 4018)

// ============================================================
// LINQ block compile path — eager inline foreach + array build.
//
// Recognised at the start of any assignment RHS (CompileDeclaration's
// `=` branch hooks IsLinqBlockStart and routes here). Materialises
// the LINQ result into a temp Array slot which the caller's OPER_LET
// copies into the destination variable. Surface: `from <id> in <expr>`
// + where / let / skip / take / select / distinct / orderby / group /
// join (all extensions hang off the same shared compile-state struct).
//
// Diagnostics — previous LinqCompileLog/LinqLog `linq.log` streams
// were stripped 2026-05-12 after the LINQ surface stabilised. If
// compile-side tracing is needed again, prefer wxLogDebug at coarse
// entry points rather than per-row writes.
// ============================================================

// === LINQ compile-state types ===
// Definitions live in byteCodeLINQ.h — a SECTION OF THE BYTECODE, because the tape is the tree and
// what a query is made of belongs to the tape. The compile context only names the query it is in.

// Where the query's text ends — see the declaration. A pure look: nothing is consumed and nothing
// is emitted, so both roads that bracket a query can ask it at the moment they finish reading one.
unsigned int ibCompileCode::FindQueryTextEnd() const
{
	if (m_numCurrentCompile < 0 || (size_t)m_numCurrentCompile >= m_listLexem.size())
		return 0;

	// The first token the query did not read — INCLUDING the end-of-text marker. It stands where the
	// text ran out, which for a query being typed into is exactly the far end of its own span: the
	// whitespace after the last clause is where the next one is about to be written, and a query
	// that stops one character short of it disowns the caret standing there. Measured 2026-09-08:
	// `var q = from o in Data.Catalogs.Goods |` ended at 37 with the caret at 38, so the clause
	// keywords that position exists for were not offered.
	const size_t next = (size_t)m_numCurrentCompile + 1;
	if (next < m_listLexem.size())
		return m_listLexem[next].m_numString;

	// ⚠ AT LEAST ONE CHARACTER WIDE. A token's text is its own for an identifier or a literal and
	// EMPTY for a delimiter — which is what a query often ends on — so a span taken from the length
	// alone would stop one character short of the last `)` it read.
	const ibLexem& last = m_listLexem[(size_t)m_numCurrentCompile];
	const size_t width = last.m_strData.length();
	return last.m_numString + (unsigned int)(width > 0 ? width : 1);
}

// Outer entry — consumes KEY_FROM itself, allocates the shared LINQ
// state on stack, sets up the RETURN_BLOCK-kind context, wires the
// back-pointer, and dives into CompileLinqBlock. Callers step back
// the lexem cursor (mirrors CompileLambdaExpression idiom) so this
// function owns the KEY_FROM consumption uniformly.
ibParamUnit ibCompileCode::CompileLinqExpression(ibCompileContext* context)
{
	GETKeyWord(KEY_FROM);

	// Allocate the fake LINQ context — child of caller, RETURN_BLOCK
	// kind so bindings register in this child's m_listVariable but
	// the actual slots land in the host frame via CreateVariable's
	// chain-delegation logic. LINQ-distinction is the non-null
	// m_linqQuery on this context; no RETURN_LINQ enum tag needed.
	auto linqCtxOwner = std::shared_ptr<ibCompileContext>(
		context->CreateContext(RETURN_BLOCK));
	// ⭐ THE QUERY IS ENTERED IN THE BYTECODE, and the scope merely points at it — so the query
	// outlives every scope that reads it, and what ends its life is the SLICE at the end of
	// compilation rather than a brace closing.
	m_cByteCode.m_listLinq.emplace_back();
	ibLinqQuery& data = m_cByteCode.m_listLinq.back();
	linqCtxOwner->m_linqQuery = &data;

	// Where the query starts in the text — the `from` token itself, which the cursor is standing on.
	// See ibLinqQuery::m_textFrom for why the compiler writes this down.
	if (m_numCurrentCompile >= 0 && (size_t)m_numCurrentCompile < m_listLexem.size())
		data.m_textFrom = m_listLexem[(size_t)m_numCurrentCompile].m_numString;


	// Diagnostic: walk parent chain to see how far visibility reaches.
	int chainDepth = 0;
	for (ibCompileContext* c = linqCtxOwner->m_parentContext; c; c = c->m_parentContext) {
		++chainDepth;
		if (chainDepth > 10) break;
	}

	// Result accumulator + counters — allocate in CALLER's context
	// (not the linq scope) so slot references survive after the
	// linq context destructs — the query itself lives in the bytecode.
	// ⭐⭐ LINQ'S OWN COLLECTION, not a script `Array`. It is made in this slot by the first
	// instruction that uses it (procUnitLINQ.cpp, ibValueLinqRows) — so there is no `New` here, no
	// class resolved through a NAME at run time, and no user-visible object standing in for
	// machinery nobody can see. The rows and the keys they will be ordered by live in the same
	// place, which is what stops them coming apart when a row is dropped between them.
	data.m_resultArray = context->CreateVariable();

	data.m_skipCounter = context->CreateVariable();
	data.m_takeCounter = context->CreateVariable();
	data.m_constOne    = context->CreateVariable();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = data.m_skipCounter;
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = data.m_takeCounter;
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = data.m_constOne;
		c.m_param2.m_numIndex = 1;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// Allocate orderby parallel-keys array. Empty unless ORDERBY clause
	// fires inside CompileLinqBlock; in that case rows are pushed in
	// lock-step with __r.Add(addValue).

	CompileLinqBlock(linqCtxOwner.get());

	// Post-block GROUP BY expansion — runs ONCE after the outermost foreach (and all its nested
	// levels) exhausts. data.m_groupsContainer holds the buckets, filled by one instruction per row
	// at whichever level the `group X by K` keyword appeared.
	//
	// Two flavours:
	//   * Terminal `group X by K` (no `into`) — the groups ARE the answer, so there is no loop at
	//     all: one instruction turns the buckets into it.
	//   * Non-terminal `group X by K into g` — open a new foreach over the groups, bind `g` to one
	//     of them, and re-enter CompileLinqBlock to parse the continuation clauses (where / select /
	//     orderby on g). The cursor is still positioned at the continuation's first token (outer
	//     CompileLinqBlock left it there).
	if (data.m_hasGroup) {

		const ibParamUnit expIn   = linqCtxOwner->GetVariable(wxT("@group_exp_in"),  true, false, false, true);
		const ibParamUnit expIt   = linqCtxOwner->GetVariable(wxT("@group_exp_it"),  true, false, false, true);

		if (data.m_hasGroupInto) {
			// Non-terminal: open a new foreach over the groups and re-enter CompileLinqBlock for the
			// continuation clauses. The continuation's leaf parser uses the same WHERE / SELECT /
			// orderby / distinct machinery as the original LINQ body, keeps addValue in
			// data.m_resultArray, then NEXT_ITER + back-patches itself.

			// ⭐⭐ THE GROUPS THEMSELVES, not the pairs of a Container. The collection turns its
			// buckets into one LIGHT group per key — `Key` and `Values`, addressed by ORDINAL — in
			// the order the keys first appeared. So the loop below binds `g` to a group directly:
			// there is nothing to take apart and nothing to build back up.
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LINQ_RESULT;
				c.m_param1 = expIn;
				c.m_param2 = data.m_groupsContainer;
				c.m_param3.m_numIndex = 2;              // 2 = the groups
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			ibLinqBinding bg;
			bg.name       = data.m_groupIntoName;
			bg.origin     = ibLinqBinding::FromGroup;
			bg.valueSlot  = linqCtxOwner->GetVariable(data.m_groupIntoName);
			bg.iterInSlot = expIn;
			bg.iterItSlot = expIt;

			// The loop variable IS `g` — a group, straight from the collection.
			const int expForeachIp = (int)m_cByteCode.m_listCode.size();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_FOREACH;
				c.m_param1 = bg.valueSlot;
				c.m_param2 = expIn;
				c.m_param3 = expIt;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			bg.foreachStartIp = expForeachIp;

			// ⭐ NOTHING IS TAKEN APART HERE. The loop variable IS the group: `g.Key` and `g.Values`
			// resolve to ordinals 0 and 1 through its member table, at compile time. What this
			// replaces read `pair.Key` and `pair.Value` BY NAME and then built a `Structure` by name,
			// with its field list as a string — three name lookups and a construction, per group.

			// Clear the hasGroup / hasGroupInto flags BEFORE re-entering —
			// CompileLinqBlock's leaf parser uses them to suppress SELECT /
			// Add; for the continuation we want those to fire normally
			// against g. m_groupsContainer slot stays valid (foreach reads
			// it via expIn).
			data.m_hasGroup     = false;
			data.m_hasGroupInto = false;

			// Re-enter CompileLinqBlock with the synthetic binding.
			CompileLinqBlock(linqCtxOwner.get(), bg);
		}
		else {
			// ⭐⭐ A TERMINAL `group` HAS NO LOOP TO RUN. What it answers with is the groups, and the
			// collection already holds them: one instruction turns its buckets into light groups —
			// `Key` and `Values` by ORDINAL — in the order the keys first appeared.
			//
			// What this replaces walked the pairs of a Container, read `.Key` and `.Value` BY NAME,
			// built a `Structure` by name with its field list as a STRING, and appended it — five
			// name lookups and a construction, per group, inside a loop that existed only to do that.
			//
			// The groups land in the RESULT slot, so the instruction that ends every query (just
			// below) is what turns them into the Array the person receives — the same single place
			// every other shape of query passes through, not a second exit of its own.
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LINQ_RESULT;
			c.m_param1 = data.m_resultArray;
			c.m_param2 = data.m_groupsContainer;
			c.m_param3.m_numIndex = 2;              // 2 = the groups
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
	}

	// ⭐ WHAT THE BLOCK ANSWERS WITH — one instruction, which orders the rows first when the query
	// asked for it. Ordering is the only work a streaming loop cannot finish, because the first row
	// of an ordered result is not knowable until the last row has been seen; the collection has both
	// the rows and their keys, so it does it in one step. This is also the single place where what it
	// holds becomes a value the language can hold.
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LINQ_RESULT;
		c.m_param1 = data.m_resultArray;
		c.m_param2 = data.m_resultArray;
		c.m_param3.m_numIndex = 0;                     // an Array of the rows
		c.m_param3.m_numArray = data.m_hasOrderBy ? (data.m_orderByDescending ? 1 : 2) : 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// …and where it ends: the pair brackets the text the compiler actually read, which is what a
	// caller writing a query gets handed back beside its structure.
	data.m_textTo = FindQueryTextEnd();

	const ibParamUnit resultSlot = data.m_resultArray;   // capture before scope ends
	return resultSlot;
}

// Access-policy restriction (RLS query patch) —
//     restrict <s> in <source> [ join <a> in <T> on <lk> <op> <rk> ]* [ where <cond> ]
// Each join / where FOLDS INTO the <source> query via the decorator's Join / Where
// (OPER_CALL_LINQ -> ibValueQueryDecorator::DispatchLinqMethod -> the database), so the source
// comes back narrowed — nothing is materialised. There is no `select`: a restriction carries
// conditions only. Keys / conditions are real expressions recorded as pushdown ASTs. Returns
// the patched source (the chained decorator). Entered on its own `restrict` keyword, by
// analogy with KEY_FROM -> CompileLinqExpression.
ibParamUnit ibCompileCode::CompileRestrictExpression(ibCompileContext* context)
{
	// ⭐⭐ A RESTRICTION IS ENTERED IN THE TREE TOO. It binds names and reads clauses like any query,
	// and a reader asking "what does this text do" should get the same kind of answer whichever of
	// the two was written. The entry is made before anything is read so the `restrict` token's own
	// position is what brackets it.
	m_cByteCode.m_listLinq.emplace_back();
	ibLinqQuery& data = m_cByteCode.m_listLinq.back();
	data.m_restrict = true;
	if (m_numCurrentCompile >= 0 && (size_t)m_numCurrentCompile < m_listLexem.size())
		data.m_textFrom = m_listLexem[(size_t)m_numCurrentCompile].m_numString;

	GETKeyWord(KEY_RESTRICT);
	const wxString srcAlias = GETIdentifier(true);   // the source row alias (`s`)
	GETKeyWord(KEY_IN);
	ibParamUnit receiver = GetExpression(context);    // the source query — a decorator to patch

	{
		// The row being narrowed. It has NO cell of the frame — inside the clauses it is a lambda
		// parameter, and each clause gets its own — so what is recorded is the NAME and where it
		// came from, which is what a reader writing a restriction needs.
		ibLinqBinding bound;
		bound.name   = srcAlias;
		bound.origin = ibLinqBinding::FromRestrict;
		data.m_bindings.push_back(bound);
	}

	// Emit `<target>.<method>(args…)` as OPER_CALL_LINQ + one OPER_SET per argument — the Join and
	// Where folds below both go through it (only used here, so it lives here).
	const auto emitLinqCall = [&](ibParamUnit target, const wxString& method,
	                              const std::vector<ibParamUnit>& args) -> ibParamUnit {
		ibByteUnit code;
		AddLineInfo(code);
		code.m_numOper = OPER_CALL_LINQ;
		code.m_param2 = target;
		code.m_param3.m_numIndex = ibValue::FindLinqMethodByName(method);
		code.m_param3.m_numArray = (long)args.size();
		const ibParamUnit result = context->CreateVariable();
		code.m_param1 = result;
		m_cByteCode.m_listCode.emplace_back(std::move(code));
		for (const ibParamUnit& arg : args) {
			ibByteUnit set;
			AddLineInfo(set);
			set.m_numOper = OPER_SET;
			set.m_param1 = arg;
			m_cByteCode.m_listCode.emplace_back(std::move(set));
		}
		return result;
	};

	// joins: `join <a> in <T> on <s.k> <op> <a.k>` -> receiver.Join(T, (s, a) => s.k <op> a.k),
	// folding the join into the query. The ON is ONE predicate — the operator comes from the shared
	// expression grammar (no hand-read), and the decorator's Join splits the Compare into the
	// left / right key column + op. The inner `<T>` may be a Data source OR a computed value table.
	while (IsNextKeyWord(KEY_JOIN)) {
		GETKeyWord(KEY_JOIN);
		const wxString joinAlias = GETIdentifier(true);          // the joined-table alias (`a`)
		GETKeyWord(KEY_IN);
		const ibParamUnit inner = GetExpression(context);        // the joined table
		GETKeyWord(KEY_ON);
		const ibParamUnit onCond = EmitRestrictBody(context, srcAlias, joinAlias);   // (s, a) => s.k <op> a.k

		{
			ibLinqBinding bound;
			bound.name   = joinAlias;
			bound.origin = ibLinqBinding::FromJoin;
			data.m_bindings.push_back(bound);
		}

		std::vector<ibParamUnit> args{ inner, onCond };
		receiver = emitLinqCall(receiver, wxT("Join"), args);
	}

	// where: `where <cond>` -> receiver.Where(s => cond), folding the filter into the query.
	if (IsNextKeyWord(KEY_WHERE)) {
		GETKeyWord(KEY_WHERE);
		const ibParamUnit pred = EmitRestrictBody(context, srcAlias, wxEmptyString);   // s => cond
		std::vector<ibParamUnit> args{ pred };
		receiver = emitLinqCall(receiver, wxT("Where"), args);
	}

	// …and where the restriction ends — the same pair of positions a query gets, so a caller editing
	// one replaces exactly what was read. See ibLinqQuery::m_textFrom.
	data.m_textTo = FindQueryTextEnd();

	return receiver;
}

ibParamUnit ibCompileCode::EmitRestrictBody(ibCompileContext* context,
	const wxString& paramName, const wxString& paramName2)
{
	// A synthetic lambda `Function(<param> [, <param2>]){ Return <expr>; }` for the clause at the
	// cursor: ONE param = a where predicate (`s => cond`), TWO params = a join ON predicate
	// (`(s, a) => s.k <op> a.k`; captured locals -> Param). There is no `Function(param)` in the
	// source, so the signature is built by hand — then EmitFunctionBody emits the OPER_LFUNC frame +
	// bare `Return <expr>` body + m_listFunc entry + capture, and the pushdown AST is recorded,
	// exactly as CompileLambdaExpression does for a real lambda. No hand-rolled frame.
	const bool twoParams = !paramName2.IsEmpty();
	std::unique_ptr<ibCompileContext> fnCtxOwner(context->CreateContext(RETURN_LAMBDA_FUNCTION));
	ibCompileContext* fnCtx = fnCtxOwner.get();
	fnCtx->m_parentContext = context;

	auto createdFunction = std::make_shared<ibCompileContext::ibFunction>(
		wxString::Format(wxT("<restrict@%d>"), m_numCurrentCompile), fnCtx);
	createdFunction->m_bCodeRet = true;
	{
		ibCompileContext::ibFunction::ibParamVariable param;
		param.m_strName = paramName;
		createdFunction->m_listParam.emplace_back(std::move(param));
	}
	fnCtx->AddVariable(paramName);                    // slot 0 — first row parameter
	if (twoParams) {
		ibCompileContext::ibFunction::ibParamVariable param;
		param.m_strName = paramName2;
		createdFunction->m_listParam.emplace_back(std::move(param));
		fnCtx->AddVariable(paramName2);               // slot 1 — second row parameter (join ON)
	}

	if (!EmitFunctionBody(context, createdFunction, fnCtx, /*bareExprBody*/true))
		return ibParamUnit();

	// The same tail CompileLambdaExpression runs after EmitFunctionBody: back-patch OPER_LFUNC
	// (dest slot / end IP / func index) + record the L4 pushdown AST on the new m_listFunc entry.
	const long lfuncIp    = (long)createdFunction->m_nStart;
	const long endlfuncIp = (long)createdFunction->m_nFinish;
	const long funcIndex  = (long)m_cByteCode.m_listFunc.size() - 1;

	const ibParamUnit target = context->CreateVariable();
	ibByteUnit& lfunc = m_cByteCode.m_listCode[lfuncIp];
	lfunc.m_param1 = target;
	lfunc.m_param2.m_numIndex = endlfuncIp;
	lfunc.m_param3.m_numIndex = funcIndex;

	// ⭐⭐ READ OFF THE INSTRUCTIONS, like everything else. A `restrict` clause is not a grammar of its
	// own — it is the same `where` / join ON over a source, and its body has just been compiled to
	// the same instructions any other body compiles to. Nothing is stored here and nothing needs to
	// be: whoever wants the tree derives it from these instructions (lambdaQueryAST.h), which is what
	// removed the last caller of the lexeme reader and with it a second grammar wider than the
	// language.
	//
	// 🛑 A BLOCK THAT BUILT A NAME RESOLVER AND THEN THREW IT AWAY stood here — `(void)nameOfOuter;`
	// after fifteen lines of walking the context. It was left when the storing was removed, and it
	// read like a mechanism while doing nothing at all.
	return target;
}

////////////////////////////////////////////////////////////////////////////
//	The chain syntax, compiled as a LOOP — see the header for why.
//
//	⭐⭐ THE BYTECODE IS THE TREE. There is no abstract syntax tree in this compiler, and for LINQ
//	there does not need to be one: the instructions ARE the structure — a `Where` is a branch, a
//	source is a foreach, a projection is an assignment. Everything this arc has done rests on that
//	one fact (Max, 2026-09-08: *"the role of the abstract syntax tree, for LINQ, is played by the
//	bytecode"*): the query tree the pushdown uses is READ off the instructions, the caret answer is
//	read off the instructions, and now the pipeline is WRITTEN as instructions.
////////////////////////////////////////////////////////////////////////////

// A pure read over the lexemes — see the header. Both roads decide from this and neither consumes a
// token doing so; the chain is then read for real with the ordinary GET* helpers.
namespace {

// One `.Verb( … )` link, located in the lexeme stream by the look-ahead below. File-static: it is
// the READING of a chain, not state, and nobody outside this file has a use for it.
struct ibLinqChainLink {
	long   m_numVerb     = -1;    // ibLinqMethod
	size_t m_numLexArg   = 0;     // first lexeme INSIDE the parentheses
	size_t m_numLexClose = 0;     // the matching ')'
	bool   m_noArguments = false; // `()` — no arguments at all
};

// A pure read over the lexemes from `at`: every `.Verb( … )` link that follows, with balanced
// parentheses. Consumes nothing and emits nothing — both roads decide from this and then read the
// chain for real with the ordinary GET* helpers, so positions and diagnostics stay what they were.
void FindLinqChain(const std::vector<ibLexem>& lexems, size_t at, std::vector<ibLinqChainLink>& out)
{
	out.clear();

	const auto isDelim = [&](size_t i, wxUniChar c) {
		return i < lexems.size() && lexems[i].m_lexType == DELIMITER
			&& (wxUniChar)lexems[i].m_numData == c;
	};

	size_t p = at;

	while (isDelim(p, wxT('.'))) {
		const size_t nameAt = p + 1;
		if (nameAt >= lexems.size())
			return;
		const short kind = lexems[nameAt].m_lexType;
		if (kind != IDENTIFIER && kind != KEYWORD)
			return;

		// A pipeline verb by NAME, off the one table the emitter resolves against — the same
		// lookup OPER_CALL_LINQ does, so this cannot drift away from what the chain means.
		const long verb = ibValue::FindLinqMethodByName(lexems[nameAt].m_strData);
		if (verb < 0)
			return;

		if (!isDelim(nameAt + 1, wxT('(')))
			return;

		// Balanced parentheses. A close-paren inside a string is a CONSTANT lexeme, not a
		// delimiter, so the count cannot be fooled by text; the braces of a lambda body do not
		// enter it at all.
		size_t q = nameAt + 1;
		int depth = 0;
		for (; q < lexems.size(); ++q) {
			if (lexems[q].m_lexType != DELIMITER)
				continue;
			const wxUniChar c = (wxUniChar)lexems[q].m_numData;
			if (c == wxT('(')) ++depth;
			else if (c == wxT(')') && --depth == 0) break;
		}
		if (q >= lexems.size())
			return;

		ibLinqChainLink link;
		link.m_numVerb     = verb;
		link.m_numLexArg   = nameAt + 2;
		link.m_numLexClose = q;
		link.m_noArguments = (link.m_numLexArg == q);
		out.push_back(link);
		p = q + 1;
	}
}

// Which verbs take a lambda WRITTEN HERE (and so must be checked for one), and which do not.
// `Take` / `Skip` take a count, `Distinct` takes nothing at all — and none of the three needs a
// scope of its own, which is why they are not in the name check either.
bool IsLinqVerbLambda(long verb)
{
	return verb == (long)ibValue::ibLinqMethod::Where
	    || verb == (long)ibValue::ibLinqMethod::Select
	    || verb == (long)ibValue::ibLinqMethod::OrderBy
	    || verb == (long)ibValue::ibLinqMethod::OrderByDescending
	    || verb == (long)ibValue::ibLinqMethod::SkipWhile
	    || verb == (long)ibValue::ibLinqMethod::TakeWhile
	    || verb == (long)ibValue::ibLinqMethod::WhereIndexed
	    || verb == (long)ibValue::ibLinqMethod::SelectIndexed
	    // ⭐⭐ AND THE ONE WHOSE LAMBDA ANSWERS WITH A COLLECTION. `SelectMany` is a projection like
	    // `Select` up to the last step: what it works out is not the row but a source of rows, and
	    // the rest of the chain runs once per element of it. In a loop that is a SECOND FOREACH
	    // inside the body — the same shape the block road already emits for a join's bucket.
	    || verb == (long)ibValue::ibLinqMethod::SelectMany;
}

// The two that hand the body a SECOND name — the row's position among the ones that reached them.
bool IsLinqVerbIndexed(long verb)
{
	return verb == (long)ibValue::ibLinqMethod::WhereIndexed
	    || verb == (long)ibValue::ibLinqMethod::SelectIndexed;
}

// ⭐⭐ THE VERBS THAT TAKE A SECOND SOURCE — and they split in two by WHAT THEY DO WITH IT, which is
// also what decides whether a loop can carry them.
//
//   * `Intersect` / `Except` only ASK the second source questions: is this row in it? So it is
//     walked ONCE, before the loop, into a set — and inside the loop each row is one lookup and one
//     branch. Nothing of the chain runs over it.
//   * `Concat` / `Union` YIELD its rows as well, which means every verb written after them has to
//     run over the second source too — the same instructions a second time. That is why they are
//     taken only at the END of a chain (see the gate): written there, there is nothing after them,
//     and the second walk is a walk that keeps rows.
bool IsLinqVerbSetTest(long verb)
{
	return verb == (long)ibValue::ibLinqMethod::Intersect
	    || verb == (long)ibValue::ibLinqMethod::Except;
}

bool IsLinqVerbSetYield(long verb)
{
	return verb == (long)ibValue::ibLinqMethod::Concat
	    || verb == (long)ibValue::ibLinqMethod::Union;
}

// 🛑 AND THE TWO THAT STAY WHERE THEY ARE, WHICH IS A DECISION AND NOT A GAP.
//
// `Join` has a road no loop can match: a RAM receiver joined with a DATABASE argument is pushed
// whole to the server (`ibValueQueryable::TryJoinThroughL3`, valueQueryable.cpp) and the composer
// promotes the in-memory side to a temp table there. Compiled as a loop it would stream the inner
// source into memory to build a hash — correct, and slower than what already happens. It is the
// same reason `Count` and `Any` are left alone below: the door answers better than the loop would.
// Written as `from … join … on … equals …`, the block road DOES compile it, because there the
// source is named rather than handed over as a value.
//
// `SequenceEqual` walks TWO sources in lockstep, one row each per step, and stops at the first
// difference. That is not a loop over one source with instructions in its body; it is two cursors,
// which is what an iterator state is for.



bool IsLinqVerbCompiled(long verb)
{
	return IsLinqVerbLambda(verb)
	    || verb == (long)ibValue::ibLinqMethod::Take
	    || verb == (long)ibValue::ibLinqMethod::Skip
	    || verb == (long)ibValue::ibLinqMethod::Distinct
	    || verb == (long)ibValue::ibLinqMethod::Reverse
	    || IsLinqVerbSetTest(verb)
	    || IsLinqVerbSetYield(verb);
}

// ⭐⭐ A LAMBDA BODY THE LOOP CAN CARRY — ANY BLOCK, and not only one `return <expr>`.
//
// The fold used to demand a body of exactly `{ return <expr>; }`; a body with so much as an `if` in
// it fell to the road that builds state objects. That was never a statement about the loop — a body
// is a block like any other, and this compiler has compiled blocks since its first day. What made
// the single `return` special was only WHERE ITS VALUE WENT, out of a frame the fold does not open.
// ibReturnCapture (compileContext.h) settles that, so what is left here is the token shape.
//
// Asked of the tokens because there is nothing else to ask yet: a chain is judged before a single
// instruction of it is emitted. Two answers come back — can it be folded at all, and is it the one
// `return` shape, which needs no cell and no jump and stays exactly the instructions it always was.
//
// 🛑 WHAT IS REFUSED, AND WHY IT HAS TO BE. `break` / `continue` / `goto` written inside the body
// would bind to the PERSON'S loop once folded into it — the fold's context is a child of the
// loop's, so the walk up finds it — while the same tokens on the dispatcher road are a refusal
// ("outside a loop"), a lambda being its own function there. One text, two meanings, and the faster
// road the one that quietly changes it. Refused here, the text keeps the road that says no.
bool IsFoldableLambdaBody(const std::vector<ibLexem>& lexems, size_t brace, bool& singleReturn)
{
	singleReturn = false;

	const auto isDelim = [&](size_t i, wxUniChar c) {
		return i < lexems.size() && lexems[i].m_lexType == DELIMITER
			&& (wxUniChar)lexems[i].m_numData == c;
	};
	const auto isKey = [&](size_t i, long key) {
		return i < lexems.size() && lexems[i].m_lexType == KEYWORD && lexems[i].m_numData == key;
	};

	if (!isDelim(brace, wxT('{')))
		return false;                        // the braces dialect folds; the other never reaches here

	size_t end = 0, firstSemi = 0, semis = 0;
	for (size_t i = brace + 1, depth = 0; i < lexems.size(); ++i) {
		if (lexems[i].m_lexType == ENDPROGRAM)
			break;
		if (isDelim(i, wxT('{')) || isDelim(i, wxT('(')) || isDelim(i, wxT('['))) { ++depth; continue; }
		if (isDelim(i, wxT(')')) || isDelim(i, wxT(']'))) { if (depth > 0) --depth; continue; }
		if (isDelim(i, wxT('}'))) {
			if (depth == 0) { end = i; break; }
			--depth;
			continue;
		}
		if (isKey(i, KEY_BREAK) || isKey(i, KEY_CONTINUE) || isKey(i, KEY_GOTO))
			return false;                    // see above — these would mean the caller's loop
		if (depth == 0 && isDelim(i, wxT(';'))) {
			if (semis++ == 0) firstSemi = i;
		}
	}
	if (end == 0)
		return false;                        // unbalanced — leave it to the parser to say so

	// One `return <expr>`, with or without its `;`, and then the brace: the body IS its value.
	singleReturn = isKey(brace + 1, KEY_RETURN)
	            && (semis == 0 || (semis == 1 && firstSemi + 1 == end));
	return true;
}

// `Function(<name>) { … }` written right here, at `at` — the shape a one-parameter lambda has when
// it is text rather than a value held in a variable. Only text can be folded; a variable has no body
// to fold, which is what the dispatcher road is for.
bool IsLinqLambdaHere(const std::vector<ibLexem>& lexems, size_t at)
{
	const auto isDelim = [&](size_t i, wxUniChar c) {
		return i < lexems.size() && lexems[i].m_lexType == DELIMITER
			&& (wxUniChar)lexems[i].m_numData == c;
	};

	if (!(at + 3 < lexems.size()
		&& lexems[at].m_lexType == KEYWORD && lexems[at].m_numData == KEY_FUNCTION
		&& isDelim(at + 1, wxT('('))
		&& lexems[at + 2].m_lexType == IDENTIFIER
		&& isDelim(at + 3, wxT(')'))))
		return false;

	bool singleReturn = false;              // the fold reads the body itself; here only "can it"
	return IsFoldableLambdaBody(lexems, at + 4, singleReturn);
}

// `Aggregate(seed, Function(acc, row) { return … })` — the shape, asked of the tokens because there
// is nothing else to ask yet: this decides whether to compile the chain as a loop at all, and no
// instruction has been emitted. The seed can be any expression, so the body is looked for after the
// first comma that is not inside brackets of its own.
bool IsAggregateBodyInline(const std::vector<ibLexem>& lexems, const ibLinqChainLink& tail)
{
	const auto isDelim = [&](size_t i, wxUniChar c) {
		return i < lexems.size() && lexems[i].m_lexType == DELIMITER
			&& (wxUniChar)lexems[i].m_numData == c;
	};

	size_t comma = 0;
	for (size_t i = tail.m_numLexArg, depth = 0; i < tail.m_numLexClose && i < lexems.size(); ++i) {
		if (isDelim(i, wxT('(')) || isDelim(i, wxT('['))) { ++depth; continue; }
		if (isDelim(i, wxT(')')) || isDelim(i, wxT(']'))) { if (depth > 0) --depth; continue; }
		if (depth == 0 && isDelim(i, wxT(','))) { comma = i; break; }
	}
	if (comma == 0)
		return false;                        // one argument only — not this verb's shape

	const size_t f = comma + 1;
	if (!(f + 6 < lexems.size()
		&& lexems[f].m_lexType == KEYWORD && lexems[f].m_numData == KEY_FUNCTION
		&& isDelim(f + 1, wxT('('))
		&& lexems[f + 2].m_lexType == IDENTIFIER
		&& isDelim(f + 3, wxT(','))
		&& lexems[f + 4].m_lexType == IDENTIFIER
		&& isDelim(f + 5, wxT(')'))))
		return false;

	bool singleReturn = false;              // the fold reads the body itself; here only "can it"
	return IsFoldableLambdaBody(lexems, f + 6, singleReturn);
}

// 🛑 WHAT CAN BE FOLDED INTO A FOREACH THE PERSON ALREADY WROTE — a narrower question than what the
// chain road compiles, and it must be asked separately.
//
// A chain that OWNS its loop can carry per-loop state: `Take` gets a counter, `Distinct` gets the
// collection, `OrderBy` gets the rows and their keys, and the loop is emitted around them. Folding
// into `foreach (r in src.Where(…))` has none of that — there is one loop, written by the person,
// and each verb becomes instructions INSIDE its body. A filter and a projection are exactly that: a
// branch and an assignment, decided by the row in hand.
//
// The others are not. `Take` has to leave the loop early, `Skip` and `Distinct` need state that
// survives the iteration, and `Reverse` cannot stream at all — its first row is not knowable until
// the last has been seen. Those keep the road where the chain builds its own state objects, which
// answers the same and is what "refused here still works" means.
bool IsLinqVerbFoldedIntoForeach(long verb)
{
	return verb == (long)ibValue::ibLinqMethod::Where
	    || verb == (long)ibValue::ibLinqMethod::Select;
}

// Only a lambda WRITTEN HERE can become the loop's own instructions — a lambda held in a variable
// has no text to fold, and that is what the dispatcher road is for. The BODY may be any block:
// IsFoldableLambdaBody says which ones, and why the few it refuses have to be refused.
bool IsLinqChainInline(const std::vector<ibLexem>& lexems,
	const std::vector<ibLinqChainLink>& links, size_t count)
{
	const auto isDelim = [&](size_t i, wxUniChar c) {
		return i < lexems.size() && lexems[i].m_lexType == DELIMITER
			&& (wxUniChar)lexems[i].m_numData == c;
	};

	for (size_t i = 0; i < count; ++i) {
		if (!IsLinqVerbCompiled(links[i].m_numVerb))
			return false;
		if (links[i].m_numVerb == (long)ibValue::ibLinqMethod::Distinct)
			continue;                        // takes nothing; nothing to check
		if (!IsLinqVerbLambda(links[i].m_numVerb))
			continue;                        // `Take(n)` / `Skip(n)` — an ordinary expression
		const size_t a = links[i].m_numLexArg;
		if (a >= lexems.size()
			|| lexems[a].m_lexType != KEYWORD
			|| lexems[a].m_numData != KEY_FUNCTION)
			return false;
		if (!isDelim(a + 1, wxT('(')) || a + 2 >= lexems.size()
			|| lexems[a + 2].m_lexType != IDENTIFIER)
			return false;                    // a first parameter, named

		// ⭐ THE INDEXED PAIR TAKES A SECOND PARAMETER — `Function(row, i)` — and that is the whole
		// difference between them and Where / Select: the loop already counts the rows it has let
		// through, so the second name is bound to that counter and the body reads it like any other
		// cell.
		const size_t body = IsLinqVerbIndexed(links[i].m_numVerb) ? a + 6 : a + 4;
		if (IsLinqVerbIndexed(links[i].m_numVerb)) {
			if (!isDelim(a + 3, wxT(',')) || a + 4 >= lexems.size()
				|| lexems[a + 4].m_lexType != IDENTIFIER
				|| !isDelim(a + 5, wxT(')')))
				return false;                // exactly two parameters, both named
		}
		else if (!isDelim(a + 3, wxT(')')))
			return false;                    // exactly one parameter, named

		bool singleReturn = false;           // decided again at emit time, from the same tokens
		if (!IsFoldableLambdaBody(lexems, body, singleReturn))
			return false;
	}
	return true;
}

// ⭐⭐ WHAT TO CALL A COLUMN THE AUTHOR DID NOT NAME — ASKED OF THE INSTRUCTIONS, NOT OF THE TOKENS.
//
// `queryRewrite.h` settled the RULE once for the query language: an alias when the author gave one;
// a WALK, glued (`o.Ref.Code` is `RefCode`), because the leaf alone collides the moment two walks
// end in the same word; the leading SOURCE name never part of it; and anything with no natural name
// — an expression, a literal, an aggregate — is `Field1`, `Field2`, …
//
// 🛑 THE FIRST VERSION OF THIS WALKED THE LEXEMES AGAIN, and Max named it for what it was: a second
// parser over text we had already compiled, next to a mechanism built precisely to answer this. It
// cost a defect on the way — the token span's end is one wide of the last token when an expression
// ends at a statement boundary, so `select p.Name` came back `Field1` on the live base.
//
// The instructions do not have that problem, because they are not text. The pattern is the one this
// compiler already uses everywhere it has to know what it just wrote — Max named it: FIX THE
// BYTECODE POSITION, RUN THE BLOCK, TAKE THE ANSWER OUT OF THE ARRAY AT THAT POSITION. The caller
// notes where the projection will start, lets it emit, and this reads back what was emitted.
//
// What it reads is numbers. `OPER_GET_A` carries the member's name as an index into the constant
// pool and the OWNER it read from as a cell, so a walk is a short def-use chain: the answer's
// producer, then its owner's producer, until the owner is the ROW — at which point the walk stops,
// which is also why the source's own name never enters a column name.
//
// One road, and the tape is the tree.
wxString ColumnNameOfProjection(const ibByteCode& byteCode, int from, int to,
	const ibParamRunUnit& rowSlot, const ibParamRunUnit& resultSlot)
{
	if (to <= from || to > (int)byteCode.m_listCode.size())
		return wxEmptyString;               // nothing was emitted — a literal folded at compile time

	const auto sameSlot = [](const ibParamRunUnit& a, const ibParamRunUnit& b) {
		return a.m_numArray == b.m_numArray && a.m_numIndex == b.m_numIndex;
	};

	// 🛑 NOTHING IS SEARCHED FOR. A member walk emits one instruction per step, in order, so the
	// steps ARE the last instructions of the range — read them off backwards from the end, one after
	// another, and stop at the row. Scanning the range for a producer would be going back over the
	// tape; this compiler is single-pass, and reading what it has just written is not.
	std::vector<wxString> steps;
	ibParamRunUnit cell = resultSlot;
	for (int ip = to - 1; ip >= from; --ip) {

		const ibByteUnit& code = byteCode.m_listCode[(size_t)ip];
		if (code.m_numOper != OPER_GET_A || !sameSlot(code.m_param1, cell))
			return wxEmptyString;           // an addition, a call, a literal — no natural name

		const long nameIndex = (long)code.m_param3.m_numIndex;
		if (nameIndex < 0 || nameIndex >= (long)byteCode.m_listConst.size())
			return wxEmptyString;
		steps.insert(steps.begin(), byteCode.m_listConst[(size_t)nameIndex].GetString());

		if (sameSlot(code.m_param2, rowSlot))
			break;                          // reached the row: the walk is the whole expression
		cell = code.m_param2;               // the owner was written by the instruction before it
	}
	if (steps.empty())
		return wxEmptyString;

	wxString glued;
	for (const wxString& step : steps)
		glued << step;
	return glued;
}

// ⭐⭐ A FOLDED PARAMETER GETS A CELL OF ITS OWN — so there is nothing to search for and nothing to
// refuse.
//
// Max, 2026-09-08: *"just make it a temporary variable and stop watching the name."* A temp is
// unique by construction (`@lnq_N`), so it cannot be anybody else's cell; the NAME the body was
// written with is then bound to that cell in the scope handed in, which is the fold's own and dies
// with it. `GetVariable` looks in its own scope before it walks parents, so inside the body the
// parameter shadows an outer variable of the same name — exactly as it would if the lambda had kept
// its own frame — and the caller's variable is never written to.
//
// Asked twice for one name in one scope it answers with the same cell: two verbs that both call
// their parameter `x` are two assignments into one cell, which is what a sequence of verbs over one
// row is. The look-up is in the fold's own scope only — a handful of entries, and never the tree.
ibParamUnit BindParamToOwnCell(ibCompileContext* scope, const wxString& paramName)
{
	std::shared_ptr<ibCompileContext::ibVariable> already;
	if (scope->FindVariable(paramName, already) && already) {
		ibParamUnit cell;
		cell.m_numIndex = already->m_numVariable;
		return cell;                        // m_numArray 0 — the running frame, same as a temp's -3
	}

	const ibParamUnit cell = scope->CreateVariable(wxT("@lnq_"));
	scope->PushVariable(paramName, wxEmptyString, (unsigned int)cell.m_numIndex,
		/*typeVar*/ 0, /*exportVar*/ false, /*contextVar*/ false, /*tempVar*/ true);
	return cell;
}

// The same binding, to a cell that ALREADY EXISTS — an accumulator lives across rows, so `Aggregate`
// does not want a fresh cell per row, it wants the answer's own cell under the name its body uses.
void BindParamToCell(ibCompileContext* scope, const wxString& paramName, const ibParamUnit& cell)
{
	std::shared_ptr<ibCompileContext::ibVariable> already;
	if (scope->FindVariable(paramName, already) && already)
		return;                             // this scope already says what that name means
	scope->PushVariable(paramName, wxEmptyString, (unsigned int)cell.m_numIndex,
		/*typeVar*/ 0, /*exportVar*/ false, /*contextVar*/ false, /*tempVar*/ true);
}

// ⭐⭐ DOES THIS EXPRESSION READ A ROW OF AN OUTER LEVEL? — asked of the instructions it emitted.
//
// A join's hash is built once, from the inner source T. That is only sound while T means the same
// thing on every outer row; if T reads an OUTER binding (`from o in a from x in o.Items join …`),
// the hash built on the first row is stale on the second, and the lookup has to reset and rebuild.
//
// 🛑 THIS USED TO SCAN THE TOKENS OF T FOR AN IDENTIFIER SPELLED LIKE A BINDING. Two things are
// wrong with that and both are the same thing: it asks the TEXT a question about MEANING — a name
// in a string literal, a field that happens to share a binding's spelling, a shadowed local all
// answer wrongly — and it does it by walking back over lexemes the compiler had already turned into
// instructions.
//
// The instructions say it exactly: T has just been emitted, and a binding's row lives in a CELL, so
// the question is whether any operand of those instructions IS that cell. No names, no second pass.
bool ReadsAnOuterBinding(const ibByteCode& byteCode, int from, int to,
	const std::vector<ibLinqBinding>& bindings)
{
	if (bindings.size() < 2 || to <= from || to > (int)byteCode.m_listCode.size())
		return false;                       // one level: there is no outer row to be stale against

	const auto sameSlot = [](const ibParamRunUnit& a, const ibParamRunUnit& b) {
		return a.m_numArray == b.m_numArray && a.m_numIndex == b.m_numIndex;
	};

	// Every binding but the last — the last one IS this level's own row.
	for (int ip = from; ip < to; ++ip) {
		const ibByteUnit& code = byteCode.m_listCode[(size_t)ip];
		for (size_t i = 0; i + 1 < bindings.size(); ++i) {
			const ibParamUnit& row = bindings[i].valueSlot;
			if (sameSlot(code.m_param2, row) || sameSlot(code.m_param3, row)
				|| sameSlot(code.m_param4, row))
				return true;
		}
	}
	return false;
}

// EVERY COLUMN GETS A NAME AND NO TWO GET THE SAME ONE — the second half of the same rule. A column
// nobody can name is a column nobody can order by or total; two columns with one name is a table
// where one of them cannot be reached at all.
wxString UniqueColumnName(const std::vector<wxString>& taken, const wxString& proposed)
{
	const auto isTaken = [&taken](const wxString& name) {
		return std::any_of(taken.begin(), taken.end(), [&name](const wxString& one) {
			return stringUtils::CompareString(one, name); });
	};

	wxString name = proposed.IsEmpty()
		? wxString::Format(wxT("Field%d"), (int)taken.size() + 1)
		: proposed;
	for (int n = 1; isTaken(name); ++n)
		name = wxString::Format(wxT("%s%d"), proposed.IsEmpty() ? wxT("Field") : proposed, n);
	return name;
}

} // namespace

// ⭐⭐ THE BODY OF A LAMBDA THAT IS BECOMING SOMEBODY ELSE'S INSTRUCTIONS.
//
// Called with the cursor just before the body's `{`, and it leaves the cursor on the matching `}`.
// The answer is the cell the body's value ended up in — which is all any caller here wants: a
// filter branches on it, a projection assigns from it, a fold writes it back into the accumulator.
//
// TWO ROADS, ONE ANSWER. A body of one `return <expr>` stays EXACTLY the instructions it always
// was: the expression's own cell, no copy, no jump, no scope opened around it. That is the body
// nearly every chain is written with, it runs once per row, and paying two instructions a row for
// a generality it does not use would be a real cost for nothing. Anything wider goes through
// CompileBlock — the same road every `{ }` in the language takes — with one thing arranged first:
// where a `return` inside it puts its value (ibReturnCapture, compileContext.h).
//
// `outSingleReturn`, where a caller asks for it, is which road was taken. Only one caller cares,
// and for a precise reason: what a source is offered to run for itself is a RANGE OF INSTRUCTIONS
// read as a condition, and a body with branches in it is a program rather than a condition.
ibParamUnit ibCompileCode::EmitLambdaBody(ibCompileContext* context, bool* outSingleReturn)
{
	// ⚠ The shape is asked of the token AFTER the cursor, and a cursor of -1 ("before the first
	// token") would make `(size_t)(-1) + 1` read as 0 — the top of the module. Unreachable here, and
	// one comparison is cheaper than being sure it stays unreachable.
	bool singleReturn = false;
	const bool foldable = m_numCurrentCompile >= 0
		&& IsFoldableLambdaBody(m_listLexem, (size_t)m_numCurrentCompile + 1, singleReturn);
	if (outSingleReturn != nullptr)
		*outSingleReturn = singleReturn || !foldable;

	if (!foldable || singleReturn) {
		GETDelimeter('{');
		GETKeyWord(KEY_RETURN);
		const ibParamUnit value = GetExpression(context);
		if (IsNextDelimeter(';')) GETDelimeter(';');
		GETDelimeter('}');
		return value;
	}

	ibReturnCapture capture;
	capture.m_valueCell = context->CreateVariable(wxT("@lret_"));
	capture.m_scopeDepth = m_compileScopeDepth;    // where the body's own scope will open FROM

	// A SCOPE OF ITS OWN, like any block: what the body declares is the body's, and the next verb's
	// body in the same chain starts clean. The names bound above it — the row, the index, the
	// accumulator — are still read through the parent, exactly as a nested `{ }` reads its enclosing
	// function's locals.
	auto bodyOwner = std::shared_ptr<ibCompileContext>(context->CreateContext(RETURN_BLOCK));
	ibCompileContext* const bodyCtx = bodyOwner.get();
	bodyCtx->m_returnCapture = &capture;
	CompileBlock(bodyCtx);

	// The trampoline lands here — every `return` in the body jumps to whatever comes after it.
	const long afterBody = (long)m_cByteCode.m_listCode.size();
	for (const int ip : capture.m_jumps)
		m_cByteCode.m_listCode[(size_t)ip].m_param1.m_numIndex = afterBody;

	return capture.m_valueCell;
}

bool ibCompileCode::TryFoldLinqChainSource(ibCompileContext* context, const ibParamUnit& receiver)
{
	// No foreach source is being read (see ibLinqSourceScope) — and the cursor has to be inside the
	// text for the scan below to mean anything: `(size_t)(-1) + 1` is 0, a valid index, so a cursor
	// that is "before the first token" would silently scan from the top of the module.
	if (m_numLinqSourceEnd < 0 || m_numCurrentCompile < 0)
		return false;

	std::vector<ibLinqChainLink> links;
	FindLinqChain(m_listLexem, (size_t)m_numCurrentCompile + 1, links);
	if (links.empty())
		return false;

	// 🛑 THE CHAIN MUST BE THE WHOLE SOURCE, not a chain somewhere inside it. `foreach (r in f(a.Where(g)))`
	// reaches this while the same scope is open, and folding THAT chain would put the filter on the
	// wrong collection: `a` would go to `f` unfiltered and `g` would be applied to the rows of `f(a)`.
	//
	// ⚠ THE TEST USED TO BE THE CLOSING TOKEN — "what follows a source is `)`, or `Do` in the other
	// dialect" — which is true and does not settle it: in the example above the token after
	// `Where(g)` is a `)` too, the one that closes `f(`. So the chain was claimed and a different
	// program compiled, silently. What settles it is the POSITION: the header's own closer was found
	// once, with brackets balanced, and the chain is the source only when it ends exactly there.
	if ((int)links.back().m_numLexClose + 1 != m_numLinqSourceEnd)
		return false;

	// Every link is a verb here — there is no terminal to end on, which is precisely why the chain
	// cannot own a loop and folds into one instead. And because the loop belongs to the PERSON, only
	// the verbs that are an instruction about the row in hand can be folded into it: a filter is a
	// branch, a projection is an assignment, and everything else needs state or a second pass.
	for (const ibLinqChainLink& link : links)
		if (!IsLinqVerbFoldedIntoForeach(link.m_numVerb))
			return false;

	if (!IsLinqChainInline(m_listLexem, links, links.size()))
		return false;


	// ⭐ ONE NUMBER TRAVELS, not the links: where the chain begins. The verbs are read again from
	// there when the body is emitted — a scan over lexemes already in memory, which costs nothing and
	// keeps no second copy of the program anywhere.
	//
	// ⚠ THE ARMING IS NOT CLEARED HERE ANY MORE, and it must not be: the source read is still going
	// on (this returns into the postfix walker, which returns into GetExpression), and the scope that
	// armed it is what disarms it. There is nothing to decide twice — one source has one closer, and
	// a second chain ending at that same closer cannot exist.
	m_numLinqChainAt    = m_numCurrentCompile + 1;
	m_numCurrentCompile = (int)links.back().m_numLexClose;    // the chain is consumed, and not read
	return true;
}

void ibCompileCode::EmitLinqChainClauses(ibCompileContext* context, const ibParamUnit& rowSlot,
	std::vector<int>& outSkipIps)
{
	if (m_numLinqChainAt < 0)
		return;

	std::vector<ibLinqChainLink> links;
	FindLinqChain(m_listLexem, (size_t)m_numLinqChainAt, links);
	m_numLinqChainAt = -1;                     // one statement's worth, and then gone
	if (links.empty())
		return;

	// ⚠ THE TOKENS ARE READ OUT OF ORDER, AND ONLY HERE. A verb's body sits in the HEADER, before
	// `do`, while the instructions it becomes belong INSIDE the body. So the cursor steps back into
	// each recorded span, the ordinary GET* helpers read it, and the cursor returns to where the
	// header ended. It is a reordering within a span already scanned, not a jump: nothing outside
	// [first verb, last `)`] is touched, and every instruction still stamps the token it came from.
	const int resume = m_numCurrentCompile;
	ibParamUnit current = rowSlot;

	// ⭐⭐ THE PARAMETER GETS A CELL OF ITS OWN, AND NOBODY HAS TO WATCH ITS NAME.
	//
	// A folded parameter used to be bound with `GetVariable(paramName)`, which hands back the
	// PROCEDURE's variable when one is already called that — there is no block scope in this
	// language — so folding `Where(Function(total) …)` would write into somebody else's `total`
	// once per row and quietly change what the program means. A gate stood in front of that,
	// refusing the fold whenever the name was taken; it cost every such chain the slow road.
	//
	// Max, 2026-09-08: *"just make it a temporary variable and stop watching the name."* A temp is
	// unique by construction, so there is nothing to search for and nothing to refuse: the cell is
	// new, and the NAME the body uses is bound to that cell in a scope of the fold's own, which dies
	// with the fold. The caller's `total` is never touched, and a chain folds whatever it calls its
	// parameter.
	auto foldCtxOwner = std::shared_ptr<ibCompileContext>(context->CreateContext(RETURN_BLOCK));
	ibCompileContext* const foldCtx = foldCtxOwner.get();

	for (const ibLinqChainLink& link : links) {
		const size_t a = link.m_numLexArg;

		const wxString paramName = m_listLexem[a + 2].m_valData.GetString();
		const ibParamUnit bound  = BindParamToOwnCell(foldCtx, paramName);
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = bound;
			c.m_param2  = current;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		m_numCurrentCompile = (int)(a + 3);       // the next token read is the body's `{`
		const ibParamUnit value = EmitLambdaBody(foldCtx);  // the parameter's name lives in here

		if (link.m_numVerb == (long)ibValue::ibLinqMethod::Where) {
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_IF;                // false -> the next row, the branch `if` uses
			c.m_param1  = value;
			c.m_param2.m_numIndex = 0;            // back-patched by the caller to the NEXT_ITER
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			outSkipIps.push_back((int)m_cByteCode.m_listCode.size() - 1);
		}
		else {
			// A projection is written back into the LOOP VARIABLE, so the body the person wrote
			// sees the projected value under the name they gave the row. The iterator refills that
			// slot at every NEXT_ITER, so overwriting it inside the body costs nothing.
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = rowSlot;
			c.m_param2  = value;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			current = rowSlot;
		}
	}

	m_numCurrentCompile = resume;

	ibJournalInfo(wxT("linq"), wxT("chain of %d verb(s) folded into a foreach, in the caller's frame"),
		(int)links.size());
}

bool ibCompileCode::CompileLinqChain(ibCompileContext* context,
	const ibParamUnit& receiver, ibParamUnit& outResult)
{
	// ---- 1. LOOK, DECIDE NOTHING YET -------------------------------------------------------
	std::vector<ibLinqChainLink> links;
	FindLinqChain(m_listLexem, (size_t)m_numCurrentCompile + 1, links);

	// ---- 2. IS IT A SHAPE THIS COMPILES? ---------------------------------------------------
	// Deliberately narrow. Everything refused here still works — it takes the OPER_CALL_LINQ road,
	// which builds the same answer out of state objects. Widening this is adding verbs, not
	// changing the machine.
	if (links.size() < 2)
		return false;                       // a bare terminal is not a pipeline

	// The terminals this compiles. Each is a few ordinary instructions over the row: an accumulator,
	// a collection, or a value plus the language's own `break` (an OPER_GOTO to the loop exit).
	// Anything else declines and keeps the road that builds state objects.
	const ibLinqChainLink& tail = links.back();
	const bool terminalCounts = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Count);
	const bool terminalArray  = (tail.m_numVerb == (long)ibValue::ibLinqMethod::ToArray);
	const bool terminalSum    = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Sum);
	const bool terminalAny    = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Any);
	const bool terminalFirst  = (tail.m_numVerb == (long)ibValue::ibLinqMethod::First
	                          || tail.m_numVerb == (long)ibValue::ibLinqMethod::FirstOrDefault);
	// ⭐ THE REST OF THE TERMINALS ARE THE SAME TWO SHAPES. `Last` keeps whatever came through and
	// lets the next row overwrite it; `Min` / `Max` / `Average` are accumulators like `Sum`, one
	// with a comparison instead of an addition. None of them needs a state object, a collection or
	// an early exit.
	const bool terminalLast   = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Last
	                          || tail.m_numVerb == (long)ibValue::ibLinqMethod::LastOrDefault);
	const bool terminalMin    = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Min);
	const bool terminalMax    = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Max);
	const bool terminalAvg    = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Average);

	// ⭐⭐ AND THE THREE THAT TAKE SOMETHING — the first terminals on this road with an argument.
	//
	// `Contains(v)` is a comparison and an early exit; `ElementAt(n)` is a counter and an early
	// exit. What makes them different from the ones above is not the loop, it is WHEN the argument
	// is worked out: it is one value for the whole run, so it is computed BEFORE the loop opens —
	// the position is fixed, the expression is read out of its own span, and the loop that follows
	// only compares against the cell it landed in.
	//
	// ⚠ `ElementAt` and not `ElementAtOrDefault` is the one that RAISES when the row is not there,
	// and a raise is a different shape from a loop — it keeps the road that already raises. Asking
	// for a row that may not be there and saying so with an empty answer is this one.
	const bool terminalContains = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Contains);
	const bool terminalAt       = (tail.m_numVerb == (long)ibValue::ibLinqMethod::ElementAtOrDefault);
	const bool takesOneArgument = terminalContains || terminalAt;

	// ⭐⭐ AGGREGATE — the fold itself, and the only terminal that takes TWO things: a seed, which is
	// the answer before any row has been seen, and a body that says how one row changes it. Both
	// mechanisms are already here: the seed is worked out before the loop like any argument, and the
	// body is a two-parameter lambda like the indexed pair — except the first name is bound to the
	// ANSWER'S OWN CELL, because an accumulator is what survives between rows.
	const bool terminalFold = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Aggregate);

	// ⭐ THE ONLY ONE — `Single` and `SingleOrDefault`. Both keep the first row and count what came;
	// they differ in what an EMPTY source means: to `Single` it is a mistake, to the other it is an
	// empty answer. More than one row is a mistake to both, and that refusal is the point of the
	// verb — asking for "the one" is a statement that there is exactly one.
	const bool terminalOnly       = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Single
	                              || tail.m_numVerb == (long)ibValue::ibLinqMethod::SingleOrDefault);
	const bool terminalOnlyStrict = (tail.m_numVerb == (long)ibValue::ibLinqMethod::Single);

	// ⭐⭐ GROUPING IS ALREADY THIS COLLECTION'S SECOND JOB, and the block road has used it since
	// `group … by` was compiled: a row goes under its key in ONE instruction, and after the loop the
	// same collection turns its buckets into one light group per key. A chain asking for the same
	// thing had no reason to go through a state object for it — the only difference between the two
	// syntaxes is where the key expression is written.
	const bool terminalGroup = (tail.m_numVerb == (long)ibValue::ibLinqMethod::GroupBy);

	if (!(terminalCounts || terminalArray || terminalSum || terminalAny || terminalFirst
		|| terminalLast || terminalMin || terminalMax || terminalAvg
		|| takesOneArgument || terminalFold || terminalOnly || terminalGroup))
		return false;
	if ((takesOneArgument || terminalFold || terminalGroup) ? tail.m_noArguments : !tail.m_noArguments)
		return false;                       // the shape has to match the verb, either way

	// The key is a lambda written HERE, like every other body this road compiles.
	if (terminalGroup && !IsLinqLambdaHere(m_listLexem, tail.m_numLexArg))
		return false;

	// The fold's body has to be written HERE, like every other body this road compiles — a lambda
	// held in a variable has no text to fold, and that is the road the dispatcher is for.
	if (terminalFold && !IsAggregateBodyInline(m_listLexem, tail))
		return false;

	if (!IsLinqChainInline(m_listLexem, links, links.size() - 1))
		return false;

	// ---- 2a. A TERMINAL THE DOOR ANSWERS BY ITSELF — LEAVE IT ALONE ------------------------
	//
	// The old gate was wide: a chain became a loop only when the source was PROVABLY in memory,
	// because a loop over a database source would have streamed every row here to filter it. That
	// reason is gone — the loop OFFERS its filter to the source (OPER_LINQ_NARROW), which reads it
	// off the loop's own instructions and runs it server-side, so the filter travels either way.
	//
	// What survives is not about filtering. `Count()` over a queryable is one `SELECT COUNT(*)` and
	// no rows cross the wire at all; no loop competes with that. `Any` and `First` are the same
	// shape — one row, or none.
	if (terminalCounts || terminalAny || terminalFirst)
		return false;

	// ⭐ WHAT THE CHAIN CARRIES decides what the loop needs before it opens. Asked once, here, so the
	// body below emits only what this particular chain actually uses — a chain without `Skip` pays
	// for no counter, one without `Distinct` allocates nothing to remember rows in.
	bool hasSkip = false, hasTake = false, hasDistinct = false, hasOrderBy = false, orderDesc = false;
	bool hasSkipWhile = false, hasReverse = false;
	for (size_t i = 0; i + 1 < links.size(); ++i) {
		const long v = links[i].m_numVerb;
		if (v == (long)ibValue::ibLinqMethod::SkipWhile) hasSkipWhile = true;
		if (v == (long)ibValue::ibLinqMethod::Reverse)   hasReverse   = true;
		if (v == (long)ibValue::ibLinqMethod::Skip)     hasSkip = true;
		if (v == (long)ibValue::ibLinqMethod::Take)     hasTake = true;
		if (v == (long)ibValue::ibLinqMethod::Distinct) hasDistinct = true;
		if (v == (long)ibValue::ibLinqMethod::OrderBy)  { hasOrderBy = true; }
		if (v == (long)ibValue::ibLinqMethod::OrderByDescending) { hasOrderBy = true; orderDesc = true; }
	}

	// ⭐⭐ A SECOND SOURCE, AND WHERE IT MAY STAND. `Intersect` / `Except` only ask it questions, so
	// they stand anywhere: the set is built before the loop and each row is a lookup.
	//
	// `Concat` / `Union` yield ITS rows too, which means every verb written after them would have to
	// run over it as well — the same instructions emitted a second time. Taken at the END of a chain
	// there are none, and the second walk is three instructions that keep a row. This is the same
	// rule, and the same reason, as the one drawn just above for ordering: written the way people
	// write it, it compiles; written otherwise, the chain keeps the road that buffers for itself.
	bool hasSetTest = false, hasSetYield = false;
	for (size_t i = 0; i + 1 < links.size(); ++i) {

		const long v = links[i].m_numVerb;
		if (!IsLinqVerbSetTest(v) && !IsLinqVerbSetYield(v))
			continue;
		if (links[i].m_noArguments)
			return false;                   // a set verb with nothing to be a set OF

		if (IsLinqVerbSetTest(v)) { hasSetTest = true; continue; }
		if (i + 2 != links.size())
			return false;                   // not the last verb before the terminal
		hasSetYield = true;
	}

	// 🛑 ORDER CHANGES SOME ANSWERS AND NOT OTHERS. A sort cannot alter a Count, a Sum or an Any, so
	// where the terminal is one of those the ordering is simply not emitted — that is not a shortcut,
	// it is what the answer means. `First` and `ToArray` DO depend on it, and they get the real
	// thing: keys collected beside the rows and one sort after the loop, exactly as the block road
	// does it. `First` therefore stops streaming when a sort is present — the first row of a sorted
	// result is not knowable until the last row has been seen.
	// 🛑 THE KEYS AND THE ROWS ARE SORTED TOGETHER, so there must be exactly one key per kept row.
	// A verb AFTER the ordering can still drop a row (`…OrderBy(k).Where(p)`), and then the two
	// collections are of different lengths and the sort pairs the wrong things. Ordering last is how
	// it is written anyway; written otherwise, the chain keeps the road that buffers for itself.
	if (hasOrderBy && links.size() >= 2
		&& links[links.size() - 2].m_numVerb != (long)ibValue::ibLinqMethod::OrderBy
		&& links[links.size() - 2].m_numVerb != (long)ibValue::ibLinqMethod::OrderByDescending)
		return false;

	// ⭐ ORDER AND REVERSAL ARE THE SAME KIND OF FACT: neither can be answered while streaming,
	// because the first row of a reordered result is not knowable until the last has been seen. Both
	// are also irrelevant to Count, Sum and Any — those cannot change with order — so there they are
	// simply not emitted.
	const bool reorders      = hasOrderBy || hasReverse;

	// 🛑 THE N-TH ROW OF WHAT? `ElementAtOrDefault` counts rows as they arrive, and a chain that
	// REORDERS makes that a different row from the one asked for — the n-th of the sorted sequence
	// is not knowable until the last row has been seen. Such a chain keeps the road that buffers.
	// (`Contains` is immune: whether a value is there does not depend on the order rows come in.)
	if (terminalAt && reorders)
		return false;

	// 🛑 GROUPS COME OUT IN THE ORDER THEIR KEYS FIRST APPEARED — that is what the collection does,
	// and it is what the block road's `group … by` answers with. A sort written before a grouping is
	// asking for something else, and the road that buffers each side answers it.
	if (terminalGroup && reorders)
		return false;

	// A table is built out of the rows that were kept, so it collects and it sorts, exactly as
	// `ToArray` does — the only thing it decides differently is what the collection becomes.
	const bool sortMatters   = reorders && (terminalArray || terminalFirst);
	const bool collectsRows  = terminalArray || (terminalFirst && sortMatters);
	const bool earlyExitOk   = (terminalAny || terminalFirst) && !sortMatters;

	// 🛑 …AND A CHAIN THAT YIELDS A SECOND SOURCE HAS TO HAVE SOMEWHERE TO PUT IT. The second walk
	// keeps rows into the collection, so the answer must be built out of that collection — which is
	// what `ToArray` does. An accumulating terminal (`Sum`, `Aggregate`) would need its
	// per-row work emitted a second time as well; that chain keeps the road that already does it.
	if (hasSetYield && !terminalArray)
		return false;
	// 🛑 AND IT CANNOT BE REORDERED HERE. A sort or a reversal is applied to the whole collection at
	// the end, and by then the second source's rows are in it — so `a.Reverse().Concat(b)` would
	// reverse both, which is not what it says. Reordering a concatenation is a chain that keeps the
	// road that buffers each side for itself.
	if (hasSetYield && reorders)
		return false;

	// ---- 3. EMIT — the same instructions `from x in src where P select E` emits ---------------
	// The bindings live in a child scope so the lambda parameters do not outlive the loop, while
	// the SLOTS come from the enclosing frame (CreateVariable delegates up the chain) — which is
	// the whole point: the row is a local of the frame that is already running.
	auto loopCtxOwner = std::shared_ptr<ibCompileContext>(context->CreateContext(RETURN_BLOCK));
	ibCompileContext* const loopCtx = loopCtxOwner.get();

	// ⭐⭐ THE TERMINAL'S ARGUMENT IS WORKED OUT ONCE, HERE, BEFORE THE LOOP EXISTS. The cursor is
	// parked at the argument's own span, the ordinary expression reader takes it, and the cursor
	// returns — the same reordering-within-a-scanned-span the join's trampoline does. Inside the
	// loop there is then nothing left of it but a cell.
	ibParamUnit terminalArg;
	int foldBodyAt = -1;                    // where `Aggregate`'s body begins, read inside the loop
	if (takesOneArgument || terminalFold) {
		const int resumeArg = m_numCurrentCompile;
		m_numCurrentCompile = (int)tail.m_numLexArg - 1;   // the next token read is the argument
		terminalArg = GetExpression(loopCtx);
		if (terminalFold) {
			GETDelimeter(',');
			foldBodyAt = m_numCurrentCompile;   // …and the body starts at the next token
		}
		m_numCurrentCompile = resumeArg;
	}

	// The result the chain answers with, allocated in the CALLER's context so it outlives the scope.
	outResult = context->CreateVariable();
	if (terminalCounts || terminalSum) {
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN;                    // an accumulator starts at nothing
		c.m_param1  = outResult;
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else if (terminalContains) {
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;                       // false until a row equals it
		c.m_param1  = outResult;
		c.m_param2  = FindConst(ibValue(false));
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else if (terminalFold) {
		// The seed IS the answer until a row changes it — one assignment, before anything runs.
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1  = outResult;
		c.m_param2  = terminalArg;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	// terminalArray: the Array is built ONCE at the end, out of what was kept (OPER_LINQ_RESULT) —
	// not created here by name and grown a row at a time through a script method call.
	else if (terminalAny) {
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;                       // false until a row says otherwise
		c.m_param1  = outResult;
		c.m_param2  = FindConst(ibValue(false));
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else if (terminalAvg) {
		// An average is a total and a count, and the division happens once, after the loop.
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN;
		c.m_param1  = outResult;
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	// terminalFirst / Last / Min / Max: the slot starts EMPTY, which IS the answer for a source with
	// no rows — and for Min and Max it is also what makes the first row win without a special case.

	// ⭐ ONE SLOT, AND NOTHING BUILT BY NAME. Rows kept, keys to order by and rows already seen all
	// live in LINQ's own collection (procUnitLINQ.cpp, ibValueLinqRows), which the runtime makes in
	// this slot on first use. No `New Array`, no class resolved through a string, and `Distinct`
	// answers in log n instead of walking everything kept so far.
	// ⚠ `Intersect` / `Except` answer with DISTINCT rows — that is what a set operation means — so
	// they need the same "seen it before?" side of this collection that `Distinct` uses.
	ibParamUnit linqKept;
	if (collectsRows || hasDistinct || sortMatters || hasSetTest || terminalGroup)
		linqKept = context->CreateVariable();

	// SkipWhile keeps a flag: true until the first row that fails its predicate, and never true
	// again. That is what makes it SkipWhile and not Where — a later row that would have matched is
	// kept, because the prefix has ended.
	ibParamUnit skippingFlag;
	if (hasSkipWhile) {
		skippingFlag = context->CreateVariable();
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1  = skippingFlag;
		c.m_param2  = FindConst(ibValue(true));
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	ibParamUnit avgCount, avgOne;
	if (terminalAvg) {
		avgCount = context->CreateVariable();
		avgOne   = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_CONSTN; c.m_param1 = avgCount; c.m_param2.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = avgOne; c.m_param2.m_numIndex = 1;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// `Single` counts too — not to find a row but to know whether there was more than one.
	ibParamUnit onlyCount, onlyOne;
	if (terminalOnly) {
		onlyCount = context->CreateVariable();
		onlyOne   = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_CONSTN; c.m_param1 = onlyCount; c.m_param2.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = onlyOne; c.m_param2.m_numIndex = 1;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// ElementAt counts the rows that have gone past — the same two cells an average keeps, for a
	// different question.
	ibParamUnit atCounter, atOne;
	if (terminalAt) {
		atCounter = context->CreateVariable();
		atOne     = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_CONSTN; c.m_param1 = atCounter; c.m_param2.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN; c.m_param1 = atOne; c.m_param2.m_numIndex = 1;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// ⭐ ONE COUNTER PER INDEXED VERB — the position of the row IN WHAT REACHED THAT VERB, which is
	// not the same number for two of them in one chain (a `Where` between them drops rows). Made and
	// zeroed here, before the loop; the body reads it under the name the person gave it.
	std::vector<ibParamUnit> indexCounters(links.size());
	bool hasIndexed = false;
	for (size_t i = 0; i + 1 < links.size(); ++i) {
		if (!IsLinqVerbIndexed(links[i].m_numVerb))
			continue;
		hasIndexed = true;
		indexCounters[i] = context->CreateVariable();
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN;
		c.m_param1  = indexCounters[i];
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	ibParamUnit skipCounter, takeCounter, constOne;
	if (hasSkip || hasTake || hasIndexed) {
		constOne = context->CreateVariable();
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN;
		c.m_param1  = constOne;
		c.m_param2.m_numIndex = 1;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	if (hasSkip) {
		skipCounter = context->CreateVariable();
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN;
		c.m_param1  = skipCounter;
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	if (hasTake) {
		takeCounter = context->CreateVariable();
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_CONSTN;
		c.m_param1  = takeCounter;
		c.m_param2.m_numIndex = 0;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// ⭐⭐ A SECOND SOURCE IS WORKED OUT ONCE, HERE — like the terminal's argument, and for a stronger
	// reason: it IS a source. Read per row it would re-run whatever produced it for every row of the
	// first, which against a database is one query per row.
	//
	// The cursor is parked at the argument's own span, the ordinary expression reader takes it, and
	// the cursor returns — the same reordering-within-a-scanned-span everything else here does. What
	// is left afterwards is a slot, and the verb's tokens are skipped when the loop reaches them.
	std::vector<ibParamUnit> setSource(links.size());
	std::vector<ibParamUnit> setLookup(links.size());
	for (size_t i = 0; i + 1 < links.size(); ++i) {

		const long v = links[i].m_numVerb;
		if (!IsLinqVerbSetTest(v) && !IsLinqVerbSetYield(v))
			continue;

		const int resumeSet = m_numCurrentCompile;
		m_numCurrentCompile = (int)links[i].m_numLexArg - 1;
		const ibParamUnit read = GetExpression(loopCtx);
		m_numCurrentCompile = resumeSet;

		// Into a slot of its own, for the same reason the first source gets one: a temporary must
		// not be the thing a loop is still reading from.
		setSource[i] = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = setSource[i];
			c.m_param2  = read;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		if (!IsLinqVerbSetTest(v))
			continue;

		// ⭐ WALKED ONCE, INTO A SET. Every element goes under ITSELF as a key, which is what makes
		// the per-row question one lookup — the same collection and the same instruction a join's
		// hash is built with, asked a simpler question.
		setLookup[i] = context->CreateVariable();
		const ibParamUnit sideIn  = context->CreateVariable();
		const ibParamUnit sideRow = context->CreateVariable();
		const ibParamUnit sideIt  = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = sideIn;
			c.m_param2  = setSource[i];
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		int sideForeachIp = 0;
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_FOREACH;
			c.m_param1  = sideRow;
			c.m_param2  = sideIn;
			c.m_param3  = sideIt;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			sideForeachIp = (int)m_cByteCode.m_listCode.size() - 1;
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LINQ_BUCKET;
			c.m_param1  = setLookup[i];
			c.m_param2  = sideRow;                 // the key IS the value
			c.m_param3  = sideRow;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NEXT_ITER;
			c.m_param1  = sideIt;
			c.m_param2.m_numIndex = sideForeachIp;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		m_cByteCode.m_listCode[sideForeachIp].m_param4.m_numIndex =
			(long)m_cByteCode.m_listCode.size();
	}

	// The source, held in a slot of its own for the duration of the walk — the same OPER_LET the
	// block road emits, and for the same reason: a temporary receiver must not die mid-loop.
	const ibParamUnit inSlot = context->CreateVariable();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1  = inSlot;
		c.m_param2  = receiver;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// The first verb's parameter IS the loop variable — no binding step, no argument, no frame.
	// The name is taken the way GETIdentifier takes it (m_valData holds the source casing), so the
	// binding is registered under the name the person wrote.
	const wxString firstRowName = m_listLexem[links[0].m_numLexArg + 2].m_valData.GetString();
	// A CELL OF ITS OWN, with the written name bound to it in this loop's scope: the row is an
	// ordinary local of the frame the loop runs in — which is the whole point — and it cannot be the
	// caller's variable that happens to share the name.
	const ibParamUnit rowSlot = BindParamToOwnCell(loopCtx, firstRowName);
	const ibParamUnit itSlot  = context->CreateVariable();

	// ⭐⭐ THE OFFER TO THE SOURCE, emitted BEFORE the loop opens and back-patched once the predicate
	// exists: source, the stretch of instructions that decides a row, and the row's cell. A source
	// that can run it server-side narrows itself; one that cannot ignores it and the loop filters as
	// it would have. This is what makes "a chain is a loop" cost no push-down — see OPER_LINQ_NARROW.
	int narrowIp = -1;
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LINQ_NARROW;
		c.m_param1  = inSlot;
		c.m_param4  = rowSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
		narrowIp = (int)m_cByteCode.m_listCode.size() - 1;
	}

	int foreachIp = 0;
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_FOREACH;
		c.m_param1  = rowSlot;
		c.m_param2  = inSlot;
		c.m_param3  = itSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
		foreachIp = (int)m_cByteCode.m_listCode.size() - 1;
	}

	// Consume the chain for real, with the ordinary parser, so positions and diagnostics stay
	// exactly what they would be — the look-ahead above decided, it did not read.
	//
	// ⭐⭐ EVERY VERB IS A FEW ORDINARY INSTRUCTIONS OVER THE ROW, and the shapes are the ones the
	// BLOCK road already emits for `where` / `skip` / `take` / `distinct` / `orderby` — the same
	// branch, the same counters, the same post-loop sort. Two syntaxes, one emission.
	ibParamUnit      orderKeyValue;    // the key the ordering verb worked out for THIS row
	bool             hasOrderKey = false;
	std::vector<int> breakIps;         // …and one it will never need again: leave the loop
	ibParamUnit current = rowSlot;     // what the row has become so far down the chain

	// ⭐⭐ "THE NEXT ROW" IS NOT ONE PLACE ONCE THE CHAIN OPENS A LOOP OF ITS OWN. `SelectMany` puts a
	// second FOREACH inside the body, and a `Where` written after it rejects an element of THAT loop
	// — so it must continue THAT loop, not abandon the outer row it came from. Kept per level and
	// patched per level when the levels close, innermost first.
	struct ibChainLoop {
		ibParamUnit itSlot;
		int         foreachIp = -1;
	};
	std::vector<ibChainLoop>      innerLoops;       // opened by the chain, inside the body
	std::vector<std::vector<int>> skipIpsAt(1);     // level 0 is the source's own loop
	std::vector<std::vector<int>> skipGotoIpsAt(1); // an unconditional jump (a SkipWhile prefix)

	// ⭐ DEDUP IS ONE SHAPE, ASKED THREE TIMES IN THIS FUNCTION — `Distinct`, the set verbs, and the
	// second walk a `Union` makes. Two instructions: has the collection seen this row, and if it has,
	// leave for the next one. Written out three times it was three chances to write it differently;
	// here it is once, and the caller only says where the jump belongs (a level's skip list, or its
	// own back-patch), because THAT is the part that genuinely differs.
	//
	// Answers the position of the OPER_IF, which is what has to be patched.
	const auto emitSeenGuard = [&](const ibParamUnit& scratch, const ibParamUnit& row) -> int {
		const ibParamUnit fresh = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LINQ_SEEN;
			c.m_param1  = fresh;
			c.m_param2  = scratch;
			c.m_param3  = row;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_IF;                 // already seen -> the next row
		c.m_param1  = fresh;
		c.m_param2.m_numIndex = 0;             // the caller patches it
		m_cByteCode.m_listCode.emplace_back(std::move(c));
		return (int)m_cByteCode.m_listCode.size() - 1;
	};

	for (size_t i = 0; i + 1 < links.size(); ++i) {
		const long verb = links[i].m_numVerb;

		GETDelimeter('.');
		GETIdentifier(true, /*acceptKeyword*/true);
		GETDelimeter('(');

		if (verb == (long)ibValue::ibLinqMethod::Reverse) {
			GETDelimeter(')');
			continue;                            // the collection reverses itself at the end
		}

		// ⭐⭐ THE SECOND SOURCE WAS READ BEFORE THE LOOP — its tokens are skipped here, and what is
		// left is the question about THIS row. `Intersect` keeps a row that is in it; `Except` keeps
		// one that is not; both answer with distinct rows, which is the same three instructions
		// `Distinct` emits, on the same collection. `Concat` / `Union` ask nothing per row — their
		// second source is walked after the loop.
		if (IsLinqVerbSetTest(verb) || IsLinqVerbSetYield(verb)) {

			m_numCurrentCompile = (int)links[i].m_numLexClose;   // the ')' is the last token read

			// ⚠ `Union` DEDUPS BOTH SIDES, so the first source's rows are asked here as well — this
			// verb stands last, so every row that reaches it is every row the first source gave.
			// `Concat` asks nothing: it is the one set verb that keeps repeats, which is the whole
			// difference between the two.
			const bool dedups = IsLinqVerbSetTest(verb)
			                 || verb == (long)ibValue::ibLinqMethod::Union;

			if (IsLinqVerbSetTest(verb)) {

				const ibParamUnit found = context->CreateVariable();
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_LINQ_BUCKET_GET;  // empty when this row is not in the set
					c.m_param1  = found;
					c.m_param2  = setLookup[i];
					c.m_param3  = current;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}

				if (verb == (long)ibValue::ibLinqMethod::Intersect) {
					// Not in it -> the next row. OPER_IF jumps when its condition is FALSE, and an
					// empty bucket IS false — which is why the lookup needs no second test.
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_IF;
					c.m_param1  = found;
					c.m_param2.m_numIndex = 0;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
					skipIpsAt.back().push_back((int)m_cByteCode.m_listCode.size() - 1);
				}
				else {
					// EXCEPT — the other way round, so the jump is over the leaving.
					{
						ibByteUnit c; AddLineInfo(c);
						c.m_numOper = OPER_IF;       // not in it -> past the GOTO, i.e. keep it
						c.m_param1  = found;
						c.m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size() + 2;
						m_cByteCode.m_listCode.emplace_back(std::move(c));
					}
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_GOTO;         // in it -> drop this row
					m_cByteCode.m_listCode.emplace_back(std::move(c));
					skipGotoIpsAt.back().push_back((int)m_cByteCode.m_listCode.size() - 1);
				}
			}

			// …AND DISTINCT, because that is what these set operations answer with.
			if (dedups)
				skipIpsAt.back().push_back(emitSeenGuard(linqKept, current));
			continue;
		}

		if (verb == (long)ibValue::ibLinqMethod::Distinct) {
			GETDelimeter(')');
			// ⭐ SEEN BEFORE? — answered by an ordered set in log n. The shape it replaces walked
			// everything kept so far, comparing values, for every row.
			skipIpsAt.back().push_back(emitSeenGuard(linqKept, current));
			continue;
		}

		if (!IsLinqVerbLambda(verb)) {
			// `Take(n)` / `Skip(n)` — a count, not a lambda.
			const ibParamUnit howMany = GetExpression(loopCtx);
			GETDelimeter(')');

			if (verb == (long)ibValue::ibLinqMethod::Skip) {
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_ADD;
					c.m_param1  = skipCounter;
					c.m_param2  = skipCounter;
					c.m_param3  = constOne;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}
				const ibParamUnit within = context->CreateVariable();
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_LE;
					c.m_param1  = within;
					c.m_param2  = skipCounter;
					c.m_param3  = howMany;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}
				// Still inside the skipped prefix -> take the next row.
				const ibParamUnit past = context->CreateVariable();
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_NOT;
					c.m_param1  = past;
					c.m_param2  = within;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1  = past;
				c.m_param2.m_numIndex = 0;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				skipIpsAt.back().push_back((int)m_cByteCode.m_listCode.size() - 1);
			}
			else {
				// TAKE — enough rows already? then the loop is finished, and it leaves the way
				// `break` does.
				const ibParamUnit enough = context->CreateVariable();
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_GE;
					c.m_param1  = enough;
					c.m_param2  = takeCounter;
					c.m_param3  = howMany;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_IF;          // not enough yet -> carry on past the GOTO
					c.m_param1  = enough;
					c.m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size() + 2;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_GOTO;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
					breakIps.push_back((int)m_cByteCode.m_listCode.size() - 1);
				}
				{
					ibByteUnit c; AddLineInfo(c);
					c.m_numOper = OPER_ADD;
					c.m_param1  = takeCounter;
					c.m_param2  = takeCounter;
					c.m_param3  = constOne;
					m_cByteCode.m_listCode.emplace_back(std::move(c));
				}
			}
			continue;
		}

		// Where / Select / OrderBy — a lambda written here. The indexed pair names a second thing:
		// the position of this row among the ones that reached this verb.
		const bool indexed = IsLinqVerbIndexed(verb);
		GETKeyWord(KEY_FUNCTION);
		GETDelimeter('(');
		const wxString paramName = GETIdentifier(true);
		wxString indexName;
		if (indexed) {
			GETDelimeter(',');
			indexName = GETIdentifier(true);
		}
		GETDelimeter(')');

		// The index is a cell of the loop, bound under the name the body uses — the same binding the
		// row gets, and for the same reason: nothing of the name reaches run time.
		if (indexed) {
			const ibParamUnit indexCell = BindParamToOwnCell(loopCtx, indexName);
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LET;
				c.m_param1  = indexCell;
				c.m_param2  = indexCounters[i];
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			// ⚠ COUNTED BEFORE THE BODY RUNS, not after: a `WhereIndexed` leaves for the next row
			// the moment its predicate says no, so an increment placed after the branch would never
			// run for a rejected row and the index would count what SURVIVED rather than what
			// arrived. The body reads the copy taken above, so bumping it here changes nothing it
			// sees.
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_ADD;
			c.m_param1  = indexCounters[i];
			c.m_param2  = indexCounters[i];
			c.m_param3  = constOne;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		// Each verb names the row itself; the first verb's name IS the loop variable, and every one
		// after it is bound to whatever the row has become. One assignment per verb per row.
		if (i > 0) {
			const ibParamUnit bound = BindParamToOwnCell(loopCtx, paramName);
			if (bound.m_numArray != current.m_numArray || bound.m_numIndex != current.m_numIndex) {
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LET;
				c.m_param1  = bound;
				c.m_param2  = current;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			current = bound;
		}

		const int predicateFrom = (int)m_cByteCode.m_listCode.size();
		bool simpleBody = true;
		const ibParamUnit value = EmitLambdaBody(loopCtx, &simpleBody);

		if (verb == (long)ibValue::ibLinqMethod::Where
			|| verb == (long)ibValue::ibLinqMethod::WhereIndexed) {
			// The FIRST filter is the one the source is offered: it runs before any projection has
			// changed the row, so its columns are still the source's own. A later filter reads a
			// row the loop has already reshaped, and that is not a question a table can answer.
			//
			// ⚠ …AND ONLY A BODY THAT IS ONE EXPRESSION. What the source is handed is a RANGE OF
			// INSTRUCTIONS to read as a condition (NarrowByInstructions); a body with branches and
			// jumps in it is a program, not a condition, and offering one would mean asking the
			// table a question the instructions do not actually ask. Such a filter simply stays in
			// the loop — every row still gets it, which is what the loop was always for.
			//
			// 🛑 …AND NOT ONCE THE CHAIN HAS OPENED A LOOP OF ITS OWN. After a `SelectMany` the
			// predicate reads a FLATTENED element, not a row of the source, so handing it to the
			// source would be asking the table about a value it has never heard of — and a filter
			// that is wrong is worse than a filter that stayed in memory.
			if (simpleBody && innerLoops.empty()
				&& narrowIp >= 0 && m_cByteCode.m_listCode[narrowIp].m_param2.m_numIndex == 0) {
				ibByteUnit& narrow = m_cByteCode.m_listCode[narrowIp];
				narrow.m_param2.m_numIndex = predicateFrom;                          // first
				narrow.m_param2.m_numArray = (long)m_cByteCode.m_listCode.size();    // one past last
				narrow.m_param3 = value;                                             // where it lands
			}
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_IF;                 // false -> next row, the same branch `if` uses
			c.m_param1  = value;
			c.m_param2.m_numIndex = 0;             // back-patched to the NEXT_ITER below
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			skipIpsAt.back().push_back((int)m_cByteCode.m_listCode.size() - 1);
		}
		else if (verb == (long)ibValue::ibLinqMethod::Select
			|| verb == (long)ibValue::ibLinqMethod::SelectIndexed) {
			current = value;                       // Select — the row IS the projection from here
		}
		else if (verb == (long)ibValue::ibLinqMethod::SelectMany) {
			// ⭐⭐ FLATTEN — a second FOREACH, opened HERE and closed with the others before the outer
			// one. What the lambda answered with is a source; the rest of the chain runs once per
			// element of it, so from this point on the row IS that element.
			//
			// The source goes into a slot of its own for the same reason the outer one does: the
			// value is a temporary and the loop must not read a slot that has been reused. And the
			// level is pushed, so every `Where` written after this continues THIS loop.
			const ibParamUnit innerIn  = context->CreateVariable();
			const ibParamUnit innerRow = context->CreateVariable();
			const ibParamUnit innerIt  = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LET;
				c.m_param1  = innerIn;
				c.m_param2  = value;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_FOREACH;
				c.m_param1  = innerRow;
				c.m_param2  = innerIn;
				c.m_param3  = innerIt;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				innerLoops.push_back({ innerIt, (int)m_cByteCode.m_listCode.size() - 1 });
			}
			skipIpsAt.emplace_back();
			skipGotoIpsAt.emplace_back();
			current = innerRow;
		}
		else if (verb == (long)ibValue::ibLinqMethod::SkipWhile) {
			// ⭐ SKIP WHILE — a flag, and once it falls it stays down. The predicate is not asked
			// again after the first row that fails it, which is what makes it SkipWhile and not
			// Where: a later row that would have matched is kept.
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;             // already past the prefix -> keep this row
				c.m_param1  = skippingFlag;
				c.m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size() + 3;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;             // the prefix ended here
				c.m_param1  = value;
				c.m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size() + 2;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GOTO;           // still in the prefix -> next row
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				skipGotoIpsAt.back().push_back((int)m_cByteCode.m_listCode.size() - 1);
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LET;            // the flag falls, once
				c.m_param1  = skippingFlag;
				c.m_param2  = FindConst(ibValue(false));
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
		}
		else if (verb == (long)ibValue::ibLinqMethod::TakeWhile) {
			// ⭐ TAKE WHILE — the first row that fails ends the loop, the way `break` does.
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;             // still true -> carry on past the GOTO
				c.m_param1  = value;
				c.m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size() + 2;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GOTO;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				breakIps.push_back((int)m_cByteCode.m_listCode.size() - 1);
			}
		}
		else {
			// ORDER BY — the key of THIS row. It is not stored separately: the row and its key go
			// into the same collection together (OPER_LINQ_KEEP below), so however many rows are
			// dropped afterwards the two can never come apart.
			orderKeyValue = value;
			hasOrderKey   = true;
		}

		GETDelimeter(')');       // the body's own braces were consumed with it
	}

	// The terminal, consumed and then emitted as ordinary work inside the loop. Its argument, where
	// there is one, was read before the loop opened and is only skipped over here.
	GETDelimeter('.');
	GETIdentifier(true, /*acceptKeyword*/true);
	if (takesOneArgument || terminalFold || terminalGroup)
		m_numCurrentCompile = (int)tail.m_numLexClose;
	else {
		GETDelimeter('(');
		GETDelimeter(')');
	}

	std::vector<int> exitGotoIps;   // early terminals leave the way `break` does

	if (terminalGroup) {
		// ⭐⭐ THE KEY, WORKED OUT FOR THIS ROW, AND THE ROW PUT UNDER IT. The body is read from
		// where it was written — the cursor goes back into its span and returns, the same
		// reordering everything else on this road does — and the name it gives the row is bound to
		// the cell the row is already in.
		const int resumeGroup = m_numCurrentCompile;
		m_numCurrentCompile = (int)tail.m_numLexArg - 1;
		GETKeyWord(KEY_FUNCTION);
		GETDelimeter('(');
		const wxString keyRowName = GETIdentifier(true);
		GETDelimeter(')');
		BindParamToCell(loopCtx, keyRowName, current);
		const ibParamUnit key = EmitLambdaBody(loopCtx);
		m_numCurrentCompile = resumeGroup;

		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LINQ_BUCKET;      // one instruction; the collection keeps the order of keys
		c.m_param1  = linqKept;
		c.m_param2  = key;
		c.m_param3  = current;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else if (terminalCounts || terminalSum) {
		// `Count` adds one, `Sum` adds the row — the same instruction, a different second operand.
		ibParamUnit addend;
		if (terminalCounts) {
			addend = context->CreateVariable();
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_CONSTN;
			c.m_param1  = addend;
			c.m_param2.m_numIndex = 1;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_ADD;
		c.m_param1  = outResult;
		c.m_param2  = outResult;
		c.m_param3  = terminalCounts ? addend : current;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else if (terminalLast) {
		// LAST — every row overwrites the answer, so the one left standing is the last that got here.
		// No collection, no index, and nothing kept but the row itself.
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1  = outResult;
		c.m_param2  = current;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else if (terminalMin || terminalMax) {
		// MIN / MAX — keep this row when it beats what is kept, and when nothing is kept yet. The
		// emptiness test is what makes the first row win without the loop needing to know it is
		// first.
		const ibParamUnit beats = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = terminalMin ? OPER_LS : OPER_GT;
			c.m_param1  = beats;
			c.m_param2  = current;
			c.m_param3  = outResult;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		const ibParamUnit nothingYet = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NOT;               // nothing kept yet reads as empty, i.e. false
			c.m_param1  = nothingYet;
			c.m_param2  = outResult;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		const ibParamUnit keepIt = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_OR;
			c.m_param1  = keepIt;
			c.m_param2  = beats;
			c.m_param3  = nothingYet;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		int keepIfIp = 0;
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_IF;
			c.m_param1  = keepIt;
			c.m_param2.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			keepIfIp = (int)m_cByteCode.m_listCode.size() - 1;
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = outResult;
			c.m_param2  = current;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		m_cByteCode.m_listCode[keepIfIp].m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size();
	}
	else if (terminalOnly) {
		// ⭐ THE FIRST ROW IS KEPT AND EVERY ROW IS COUNTED — the count is what makes the verb a
		// statement rather than a `First`. Nothing is decided here; the refusal, if there is one, is
		// after the loop, where the count is final.
		// `OPER_IF` jumps when its condition is FALSE, so the test is "nothing kept yet": true falls
		// through into the assignment, false jumps past it. The same shape Min and Max use.
		const ibParamUnit nothingYet = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NOT;
			c.m_param1  = nothingYet;
			c.m_param2  = onlyCount;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_IF;
			c.m_param1  = nothingYet;
			c.m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size() + 2;   // past the assignment
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = outResult;
			c.m_param2  = current;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_ADD;
			c.m_param1  = onlyCount;
			c.m_param2  = onlyCount;
			c.m_param3  = onlyOne;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
	}
	else if (terminalFold) {
		// ⭐⭐ THE FOLD ITSELF. The body is read from where it was written — the position was noted
		// when the seed was taken — and it runs with two names bound: the accumulator, which is the
		// ANSWER'S own cell so it survives the row, and the row itself. What the body works out
		// becomes the answer, and the next row sees it.
		const int resumeFold = m_numCurrentCompile;
		m_numCurrentCompile = foldBodyAt;
		GETKeyWord(KEY_FUNCTION);
		GETDelimeter('(');
		const wxString accName  = GETIdentifier(true);
		GETDelimeter(',');
		const wxString rowName  = GETIdentifier(true);
		GETDelimeter(')');

		BindParamToCell(loopCtx, accName, outResult);
		BindParamToCell(loopCtx, rowName, current);

		const ibParamUnit folded = EmitLambdaBody(loopCtx);
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = outResult;
			c.m_param2  = folded;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		m_numCurrentCompile = resumeFold;
	}
	else if (terminalContains) {
		// ⭐ CONTAINS — one comparison, and the loop is over the moment it holds. The value compared
		// against was worked out before the loop opened, so this is an equality and a branch.
		const ibParamUnit same = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_EQ;
			c.m_param1  = same;
			c.m_param2  = current;
			c.m_param3  = terminalArg;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		int missIfIp = 0;
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_IF;                // not this row → carry on
			c.m_param1  = same;
			c.m_param2.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			missIfIp = (int)m_cByteCode.m_listCode.size() - 1;
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = outResult;
			c.m_param2  = FindConst(ibValue(true));
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_GOTO;              // found — leave the way `break` does
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			exitGotoIps.push_back((int)m_cByteCode.m_listCode.size() - 1);
		}
		m_cByteCode.m_listCode[missIfIp].m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size();
	}
	else if (terminalAt) {
		// ⭐ ELEMENT AT — a counter, an equality and the same early exit. The counter answers "how
		// many rows have gone past", so the row wanted is the one where it equals the argument.
		const ibParamUnit atIt = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_EQ;
			c.m_param1  = atIt;
			c.m_param2  = atCounter;
			c.m_param3  = terminalArg;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		int notYetIp = 0;
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_IF;
			c.m_param1  = atIt;
			c.m_param2.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			notYetIp = (int)m_cByteCode.m_listCode.size() - 1;
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = outResult;
			c.m_param2  = current;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_GOTO;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			exitGotoIps.push_back((int)m_cByteCode.m_listCode.size() - 1);
		}
		m_cByteCode.m_listCode[notYetIp].m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_ADD;               // one more row has gone past
			c.m_param1  = atCounter;
			c.m_param2  = atCounter;
			c.m_param3  = atOne;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
	}
	else if (terminalAvg) {
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_ADD;
			c.m_param1  = outResult;
			c.m_param2  = outResult;
			c.m_param3  = current;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_ADD;
		c.m_param1  = avgCount;
		c.m_param2  = avgCount;
		c.m_param3  = avgOne;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else if (collectsRows) {
		// Kept, in row order, together with the key it will be ordered by — one instruction, into
		// LINQ's own collection. `Distinct` did not keep it: that only asked whether it had been
		// seen, which is a different question from whether it is being returned.
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LINQ_KEEP;
		c.m_param1  = linqKept;
		c.m_param2  = current;
		if (hasOrderKey) c.m_param3 = orderKeyValue;
		else             c.m_param3.m_numArray = DEF_VAR_SKIP;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else {
		// `Any` and `First` are answered by the FIRST row that gets here, so they leave — and they
		// leave through the language's own exit: `break` is a plain OPER_GOTO patched to the loop's
		// end (CompileBlock, KEY_BREAK), which is what makes an early terminal cost one row rather
		// than all of them.
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = outResult;
			c.m_param2  = terminalAny ? FindConst(ibValue(true)) : current;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		if (earlyExitOk) {
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_GOTO;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			exitGotoIps.push_back((int)m_cByteCode.m_listCode.size() - 1);
		}
	}

	// ⭐⭐ THE LEVELS THE CHAIN OPENED CLOSE FIRST, INNERMOST OUTWARD — the same order and the same
	// shape the block road closes a join's bucket loops in. Each level's NEXT_ITER is emitted here,
	// so a level that runs out falls straight into the NEXT_ITER of the level around it, and finally
	// into the source's own. Nothing is searched for: the header's position was written down when
	// it was emitted, and this writes the answer back into it.
	for (size_t level = innerLoops.size(); level > 0; --level) {

		const ibChainLoop& loop = innerLoops[level - 1];
		const int innerNextIp = (int)m_cByteCode.m_listCode.size();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NEXT_ITER;
			c.m_param1  = loop.itSlot;
			c.m_param2.m_numIndex = loop.foreachIp;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		// Exhausted -> the instruction after this NEXT_ITER, which is the next level out.
		m_cByteCode.m_listCode[loop.foreachIp].m_param4.m_numIndex =
			(long)m_cByteCode.m_listCode.size();

		for (const int ip : skipIpsAt[level])
			m_cByteCode.m_listCode[ip].m_param2.m_numIndex = innerNextIp;
		for (const int ip : skipGotoIpsAt[level])
			m_cByteCode.m_listCode[ip].m_param1.m_numIndex = innerNextIp;
	}

	// Close the loop exactly as the block road does: NEXT_ITER back to the header, the header's
	// fourth operand pointing past it, and every failed `Where` landing on the NEXT_ITER.
	const int nextIterIp = (int)m_cByteCode.m_listCode.size();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_NEXT_ITER;
		c.m_param1  = itSlot;
		c.m_param2.m_numIndex = foreachIp;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	const long exitIp = (long)m_cByteCode.m_listCode.size();
	m_cByteCode.m_listCode[foreachIp].m_param4.m_numIndex = exitIp;
	for (const int ip : skipIpsAt[0])
		m_cByteCode.m_listCode[ip].m_param2.m_numIndex = nextIterIp;
	// A GOTO carries its target in the FIRST operand and an IF in the second — the same address, two
	// different fields.
	for (const int ip : skipGotoIpsAt[0])
		m_cByteCode.m_listCode[ip].m_param1.m_numIndex = nextIterIp;
	// An early terminal jumps HERE — the same address the loop falls through to, which is what
	// `break` is patched to as well.
	for (const int ip : exitGotoIps)
		m_cByteCode.m_listCode[ip].m_param1.m_numIndex = exitIp;
	// `Take` leaves the same way, and for the same reason.
	for (const int ip : breakIps)
		m_cByteCode.m_listCode[ip].m_param1.m_numIndex = exitIp;

	// ⭐⭐ AND THEN THE SECOND SOURCE — `Concat` / `Union`. The gate above put the verb LAST, so
	// nothing of the chain is owed to these rows: they go into the same collection the first
	// source's rows went into, and the one answer below is built out of both. That is the whole
	// difference between compiling this and refusing it — with a verb in the middle, every
	// instruction after it would have to be emitted a second time.
	//
	// `Union` asks the collection whether it has seen the row; `Concat` keeps repeats, which is what
	// makes them two verbs.
	for (size_t i = 0; i + 1 < links.size(); ++i) {

		if (!IsLinqVerbSetYield(links[i].m_numVerb))
			continue;

		const bool dedup = links[i].m_numVerb == (long)ibValue::ibLinqMethod::Union;

		const ibParamUnit moreIn  = context->CreateVariable();
		const ibParamUnit moreRow = context->CreateVariable();
		const ibParamUnit moreIt  = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1  = moreIn;
			c.m_param2  = setSource[i];
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		int moreForeachIp = 0;
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_FOREACH;
			c.m_param1  = moreRow;
			c.m_param2  = moreIn;
			c.m_param3  = moreIt;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			moreForeachIp = (int)m_cByteCode.m_listCode.size() - 1;
		}

		// The same two instructions the first source's rows go through — one shape, one place.
		const int seenIfIp = dedup ? emitSeenGuard(linqKept, moreRow) : -1;
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LINQ_KEEP;
			c.m_param1  = linqKept;
			c.m_param2  = moreRow;
			c.m_param3.m_numArray = DEF_VAR_SKIP;   // no ordering key: a concatenation is not sorted
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		const int moreNextIp = (int)m_cByteCode.m_listCode.size();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NEXT_ITER;
			c.m_param1  = moreIt;
			c.m_param2.m_numIndex = moreForeachIp;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		m_cByteCode.m_listCode[moreForeachIp].m_param4.m_numIndex =
			(long)m_cByteCode.m_listCode.size();
		if (seenIfIp >= 0)
			m_cByteCode.m_listCode[seenIfIp].m_param2.m_numIndex = moreNextIp;
	}

	// ---- 4. AFTER THE LOOP — one instruction, and the only work a row cannot do by itself -----
	// Ordering is that work: the first row of an ordered result is not knowable until the last row
	// has been seen. LINQ's own collection has both the rows and their keys, so it orders them and
	// answers in one step — and this is the single place where what it holds becomes a value the
	// language can hold.
	// GROUPING IS THE SAME LAST INSTRUCTION, asked for the buckets instead of the rows — one light
	// group per key, in the order the keys first appeared, exactly as `group … by` answers.
	if (terminalGroup) {
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LINQ_RESULT;
		c.m_param1  = outResult;
		c.m_param2  = linqKept;
		c.m_param3.m_numIndex = 2;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	else if (collectsRows) {
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LINQ_RESULT;
		c.m_param1  = outResult;
		c.m_param2  = linqKept;
		// WHAT THE COLLECTION BECOMES: 0 the rows as they are · 1 the first of them. (2 is the
		// groups, and only the two roads that group emit it.)
		c.m_param3.m_numIndex = terminalFirst ? 1 : 0;
		c.m_param3.m_numArray = !sortMatters ? 0
			: (hasReverse && !hasOrderBy) ? 3 : (orderDesc ? 1 : 2);
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// AVERAGE — the division, once, and only where there was something to divide. An empty source
	// averages to nothing, which is what the slot already holds.
	if (terminalAvg) {
		int emptyIfIp = 0;
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_IF;
			c.m_param1  = avgCount;
			c.m_param2.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
			emptyIfIp = (int)m_cByteCode.m_listCode.size() - 1;
		}
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_DIV;
			c.m_param1  = outResult;
			c.m_param2  = outResult;
			c.m_param3  = avgCount;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		m_cByteCode.m_listCode[emptyIfIp].m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size();
	}

	// ⭐⭐ THE ONE — and the refusal, which is the verb's whole meaning. Asking for `Single` is saying
	// there is exactly one; more than one is not a row to pick from, it is a wrong statement, and a
	// verb that quietly returned the first would hide it. The count is final only here.
	if (terminalOnly) {

		const auto refuse = [&](const ibParamUnit& when, const wxString& why) {
			int okIp = 0;
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;            // jumps when FALSE — i.e. when there is nothing wrong
				c.m_param1  = when;
				c.m_param2.m_numIndex = 0;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				okIp = (int)m_cByteCode.m_listCode.size() - 1;
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_RAISE_T;
				c.m_param1  = FindConst(ibValue(why));
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			m_cByteCode.m_listCode[okIp].m_param2.m_numIndex = (long)m_cByteCode.m_listCode.size();
		};

		// More than one — wrong for both spellings of the verb.
		const ibParamUnit tooMany = context->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_GT;
			c.m_param1  = tooMany;
			c.m_param2  = onlyCount;
			c.m_param3  = onlyOne;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		refuse(tooMany, _("Single: the source has more than one element"));

		// …and NOTHING is wrong only for the strict spelling. `SingleOrDefault` says in its own name
		// that an empty source is an answer.
		if (terminalOnlyStrict) {
			const ibParamUnit none = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_NOT;
				c.m_param1  = none;
				c.m_param2  = onlyCount;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			refuse(none, _("Single: the source is empty"));
		}
	}

	// ⭐ SAID, BECAUSE THE TWO ROADS ANSWER THE SAME. A chain that becomes a loop and a chain that
	// builds state objects give identical results, so "which one did it take" is invisible from the
	// outside — and it is the whole difference between 0.96x and 1.31x a hand-written loop. One line
	// per inlined chain, at compile time, the same way every lambda already says whether it pushes
	// down. ASCII only: a non-ASCII dash in a wxT() literal reaches the journal as mojibake.
	ibJournalInfo(wxT("linq"), wxT("chain of %d verb(s) compiled as a loop, %s in the caller's frame"),
		(int)links.size(),
		terminalCounts ? wxT("counting") : wxT("collecting"));

	return true;
}

// Recursive worker — one `from <id> in <expr>` clause plus either
// (a) recursive call to itself for the next `from` (nested foreach),
// or (b) tail clauses (where/skip/take/select) + Add at the deepest
// level. Each level emits its own OPER_FOREACH header on entry and
// matching OPER_NEXT_ITER + back-patches on unwind. Works for any
// nesting depth — single `from`, multi-from chain, or a tree once
// let/join/group/into bind new variables at branch points.
//
// Caller (CompileLinqExpression for outermost, or self-recursive
// call) has already consumed KEY_FROM and we start with the binding
// name.
void ibCompileCode::CompileLinqBlock(ibCompileContext* linqCtx)
{
	// linqCtx — the fake LINQ context (RETURN_BLOCK kind with non-null
	// m_linqQuery naming the query, entered once by
	// CompileLinqExpression). All binding registration +
	// expression compilation goes through it; lookups walk parent
	// chain so outer-scope locals stay visible (closure capture).
	// State (m_bindings, m_resultArray, counters, ...) lives on
	// linqCtx->m_linqQuery — the entry in the BYTECODE that
	// CompileLinqExpression added.
	ibCompileContext* const context = linqCtx;

	const wxString bindRealName = GETIdentifier(true);
	const wxString bindUpper    = stringUtils::MakeUpper(bindRealName);
	GETKeyWord(KEY_IN);

	ibLinqBinding b;
	b.name       = bindRealName;
	b.origin     = ibLinqBinding::FromSource;
	b.valueSlot  = context->GetVariable(bindRealName);
	b.iterInSlot = context->GetVariable(bindUpper + wxT("@in_"), true, false, false, true);
	b.iterItSlot = context->GetVariable(bindUpper + wxT("@it_"), true, false, false, true);

	// OPER_LET @in_ := source-expression
	{
		const ibParamUnit srcSlot = GetExpression(context);
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1 = b.iterInSlot;
		c.m_param2 = srcSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// OPER_FOREACH header
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_FOREACH;
		c.m_param1 = b.valueSlot;
		c.m_param2 = b.iterInSlot;
		c.m_param3 = b.iterItSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
		b.foreachStartIp = (int)m_cByteCode.m_listCode.size() - 1;
	}

	// Delegate to the pre-bound entry (which assumes OPER_LET +
	// OPER_FOREACH already emitted and `b` fully populated).
	CompileLinqBlock(linqCtx, b);
}

// Pre-bound overload — caller has emitted OPER_LET / OPER_FOREACH and
// filled `preBound` (incl. foreachStartIp). Used by `group ... into g`
// continuation to re-enter the leaf-clause / NEXT_ITER machinery with
// `g` as a synthetic binding over m_groupsContainer's pair rows.
void ibCompileCode::CompileLinqBlock(ibCompileContext* linqCtx, const ibLinqBinding& preBound)
{
	ibLinqQuery& data = *linqCtx->m_linqQuery;
	ibCompileContext* const context = linqCtx;

	// Per-from-level state — local to THIS CompileLinqBlock call so
	// recursion (nested from) doesn't pollute outer levels. Each
	// level owns its own pending join trampolines (emitted at THIS
	// level's NEXT_ITER, absolute-ip GOTO from body requires same
	// scope). GROUP's data.m_hasGroup + data.m_groupsContainer are linq-scope
	// (data.m_*) — expansion fires once after the outermost
	// CompileLinqBlock returns, in CompileLinqExpression.
	std::vector<ibLinqPendingJoin> pendingJoins;

	ibLinqBinding b = preBound;
	data.m_bindings.push_back(b);
	context->StartLoopList();


	// Multiple WHERE clauses are allowed (each emits its own OPER_IF);
	// all skip-targets back-patch to the same post-Add ip below — which,
	// once joins opened bucket loops, is the INNERMOST bucket's
	// NEXT_ITER: "skip this joined row, take the next match". JOIN-miss
	// OPER_IFs do NOT share this target (a miss never opened its bucket
	// loop) — they live on the pending join and are patched per join in
	// the close section.
	std::vector<int> whereSkipIps;

	// SKIP's "drop this row" GOTOs when joins opened bucket loops at
	// this level: the level continue would abandon the outer row's
	// remaining matches, making skip count OUTER rows instead of result
	// rows. Patched in the close section to the innermost bucket
	// continue — the same target whereSkipIps gets. Without joins SKIP
	// keeps registering into the loop's continue list as before.
	std::vector<int> skipContinueGotoIps;

	// JOIN clauses appear after `from`, before tail clauses (C# grammar).
	// Each emits per-iter lookup + its bucket FOREACH inline in outer
	// body + records an ibLinqPendingJoin for the trampoline (hash
	// build) emitted after outer NEXT_ITER. Multiple joins at the same
	// level are allowed; their bucket loops nest in clause order and are
	// closed innermost-first in the close section.
	while (IsNextKeyWord(KEY_JOIN)) {
		GETKeyWord(KEY_JOIN);
		CompileLinqJoin(context, pendingJoins);
	}

	if (IsNextKeyWord(KEY_FROM)) {
		// === Recurse — nested `from` adds another foreach depth ===
		GETKeyWord(KEY_FROM);
		CompileLinqBlock(context);
	}
	else {
		// === Innermost level — process tail clauses + Add ===
		// WHERE and let-clauses (`var alias = ...` or implicit
		// `alias = ...`) can interleave freely between `from` and
		// `select` / `skip` / `take`. Unified while-loop handles any
		// order, matching C# LINQ grammar.
		while (true) {
			// WHERE clause
			if (IsNextKeyWord(KEY_WHERE)) {
				GETKeyWord(KEY_WHERE);
				const ibParamUnit whereExpr = GetExpression(context);
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = whereExpr;
				c.m_param2.m_numIndex = 0;  // back-patched below
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				whereSkipIps.push_back((int)m_cByteCode.m_listCode.size() - 1);
				continue;
			}

			// ORDERBY clause — compile key expression per-row, optional
			// ASCENDING/DESCENDING. The expression's natural result slot
			// may carry DEF_VAR_TEMP marker (for compound expressions like
			// `o * -1`) which can interact badly with subsequent
			// OPER_SET arg loads. Copy into a stable regular slot via
			// OPER_LET so __keys.Add reads it as a normal variable.
			if (IsNextKeyWord(KEY_ORDERBY)) {
				GETKeyWord(KEY_ORDERBY);

				// ⭐⭐ AS MANY KEYS AS THE CLAUSE NAMES, separated by commas — `orderby Warehouse,
				// Item` is how an ordering is actually written, and until 2026-09-08 the comma was
				// a refusal. Each key gets its own stable cell (an OPER_LET copies the
				// per-iteration expression into it) and they are kept in the order written; the
				// comparison walks them until one differs (procUnitLINQ.cpp, SortByKeys).
				do {
					const ibParamUnit rawKey = GetExpression(context);

					const ibParamUnit keySlot = context->CreateVariable();
					{
						ibByteUnit c; AddLineInfo(c);
						c.m_numOper = OPER_LET;
						c.m_param1 = keySlot;
						c.m_param2 = rawKey;
						m_cByteCode.m_listCode.emplace_back(std::move(c));
					}
					data.m_orderByKeySlots.push_back(keySlot);

					if (!IsNextDelimeter(','))
						break;
					GETDelimeter(',');
				} while (true);

				data.m_hasOrderBy = true;

				// ⚠ THE DIRECTION IS THE CLAUSE'S, NOT EACH KEY'S. `ascending` / `descending` after
				// the last key applies to all of them — which is what the runtime can express: the
				// keys of a row are compared in order and the whole comparison is then read
				// forwards or backwards (SortByKeys). Per-key direction would need a direction
				// vector down the same road, and nothing has asked for it yet.
				if (IsNextKeyWord(KEY_DESCENDING)) {
					GETKeyWord(KEY_DESCENDING);
					data.m_orderByDescending = true;
				} else if (IsNextKeyWord(KEY_ASCENDING)) {
					GETKeyWord(KEY_ASCENDING);
					data.m_orderByDescending = false;
				}
				continue;
			}

			// Let-clause — `var alias = <expr>` or implicit `alias = <expr>`.
			// Both emit OPER_LET + push FromLet binding so subsequent
			// clauses see `alias` via standard context lookup.
			if (IsNextKeyWord(KEY_VAR)) {
				GETKeyWord(KEY_VAR);
			}
			else {
				// Implicit — 2-token peek <IDENTIFIER> '='. Identifier
				// must not be a clause-terminator keyword (those are
				// caught by SKIP/TAKE/SELECT branches below and never
				// reach this peek).
				// ⚠ AND THE CURSOR HAS TO BE INSIDE THE TEXT before it is read as an index: a
				// negative one casts to a huge size_t, and `pos + 1 >= size()` then passes it
				// through to an out-of-bounds read instead of stopping it.
				if (m_numCurrentCompile < 0) break;
				const size_t pos = (size_t)m_numCurrentCompile;
				if (pos + 1 >= m_listLexem.size()) break;
				const ibLexem& l0 = m_listLexem[pos];
				const ibLexem& l1 = m_listLexem[pos + 1];
				if (l0.m_lexType != IDENTIFIER) break;
				if (l1.m_lexType != DELIMITER || l1.m_numData != '=') break;
			}

			const wxString aliasReal = GETIdentifier(true);
			GETDelimeter('=');
			const ibParamUnit aliasSlot = context->GetVariable(aliasReal);
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LET;
				c.m_param1 = aliasSlot;
				c.m_param2 = GetExpression(context);
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			ibLinqBinding lb;
			lb.name      = aliasReal;
			lb.origin    = ibLinqBinding::FromLet;
			lb.valueSlot = aliasSlot;
			data.m_bindings.push_back(lb);
		}

		// SKIP — counter++, if (counter <= N) goto next-iter.
		if (IsNextKeyWord(KEY_SKIP)) {
			GETKeyWord(KEY_SKIP);
			const ibParamUnit skipExpr = GetExpression(context);
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_ADD;
				c.m_param1 = data.m_skipCounter;
				c.m_param2 = data.m_skipCounter;
				c.m_param3 = data.m_constOne;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const ibParamUnit tmpLE = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LE;
				c.m_param1 = tmpLE;
				c.m_param2 = data.m_skipCounter;
				c.m_param3 = skipExpr;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = tmpLE;
				c.m_param2.m_numIndex = 0;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const int skipIfIp = (int)m_cByteCode.m_listCode.size() - 1;
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GOTO;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				const int gotoIp = (int)m_cByteCode.m_listCode.size() - 1;
				if (!pendingJoins.empty()) {
					// Inside bucket loops "drop this row" continues the
					// INNERMOST bucket, so skip counts JOINED rows.
					skipContinueGotoIps.push_back(gotoIp);
				}
				else {
					auto* pList = context->m_listContinue[context->m_numDoNumber];
					if (pList != nullptr) pList->emplace_back(gotoIp);
				}
			}
			m_cByteCode.m_listCode[skipIfIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
		}

		// TAKE — if (counter >= N) goto break-out; else counter++.
		if (IsNextKeyWord(KEY_TAKE)) {
			GETKeyWord(KEY_TAKE);
			const ibParamUnit takeExpr = GetExpression(context);
			const ibParamUnit tmpGE = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GE;
				c.m_param1 = tmpGE;
				c.m_param2 = data.m_takeCounter;
				c.m_param3 = takeExpr;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;
				c.m_param1 = tmpGE;
				c.m_param2.m_numIndex = 0;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			const int takeIfIp = (int)m_cByteCode.m_listCode.size() - 1;
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_GOTO;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				const int gotoIp = (int)m_cByteCode.m_listCode.size() - 1;
				auto* pList = context->m_listBreak[context->m_numDoNumber];
				if (pList != nullptr) pList->emplace_back(gotoIp);
			}
			m_cByteCode.m_listCode[takeIfIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_ADD;
				c.m_param1 = data.m_takeCounter;
				c.m_param2 = data.m_takeCounter;
				c.m_param3 = data.m_constOne;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
		}

		// GROUP — terminal projection: `group X by K`. Mutually exclusive with SELECT at this level.
		// A row goes under its key in ONE instruction (OPER_LINQ_BUCKET), and after the loop the
		// same collection turns its buckets into the answer — one light group per key, `Key` and
		// `Values` by ordinal, in the order the keys first appeared. WHERE / ORDERBY before group
		// operate on raw rows (filter / sort before grouping). `into <g>` continues the query with
		// the groups as its rows.
		if (IsNextKeyWord(KEY_GROUP)) {
			GETKeyWord(KEY_GROUP);
			data.m_hasGroup = true;
			data.m_grouped  = true;          // the FACT; the flag above is parse state and gets cleared

			// Allocate persistent slots (in caller's context via
			// CreateVariable's RETURN_BLOCK chain-delegation).
			data.m_groupsContainer = context->CreateVariable();

			// ⭐ NOTHING IS CREATED HERE. The buckets live in LINQ's own collection, which the first
			// instruction that touches it makes in its slot — so there is no `New` guarded by an
			// is-it-there test, and no class resolved through a NAME at run time. What stood here
			// was that test: an OPER_NOT into a temp, read by an OPER_IF that no longer exists. An
			// instruction nobody reads still runs, once per row.

			// Parse X (value to group) — eval inline per iter.
			const ibParamUnit groupValueSlot = GetExpression(context);

			GETKeyWord(KEY_BY);

			// Parse K (group key) — eval inline per iter.
			const ibParamUnit groupKeySlot = GetExpression(context);

			// ⭐⭐ THE ROW GOES UNDER ITS KEY — ONE INSTRUCTION, WHERE THERE WERE SIX.
			//
			// What this replaces ran PER ROW: `Property(K, bucket)` — a method resolved by NAME, two
			// arguments — then a NOT, a branch, `New Array` (the class resolved by NAME again),
			// `Insert(K, bucket)` by name, and finally `Add(X)` by name. Four name lookups and an
			// object construction, for every row of the source, to express "put this under that key".
			//
			// The light collection holds the buckets itself: the key is found in log n, the bucket is
			// made when it is first needed, and the order the keys first appeared in is remembered —
			// because that is the order the answer comes out in.
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LINQ_BUCKET;
				c.m_param1 = data.m_groupsContainer;
				c.m_param2 = groupKeySlot;
				c.m_param3 = groupValueSlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			// `... into <g>` — non-terminal group. CompileLinqExpression's
			// post-block expansion will detect m_hasGroupInto, open a new
			// foreach over m_groupsContainer, bind <g> to Structure{Key,
			// Values} per pair, and re-enter CompileLinqBlock for the
			// continuation clauses. We do NOT parse the continuation here
			// — outer level's leaf parser falls through (hasGroup=true
			// suppresses SELECT/Add); cursor stays at the continuation's
			// first token, where post-block picks it up.
			if (IsNextKeyWord(KEY_INTO)) {
				GETKeyWord(KEY_INTO);
				data.m_groupIntoName = GETIdentifier(true);
				data.m_hasGroupInto  = true;
			}
		}

		// ⭐⭐ SELECT NAMES THE COLUMNS — WHICHEVER WAY IT IS WRITTEN, AND THERE IS ONLY ONE EXIT.
		//
		// Max, 2026-09-08: *"a query must always meet a value table — it always returns a table.
		// There has to be ONE exit, the same for everyone, or it is not workable. So all the selects
		// define column names… and we cannot name those columns any old way, and cannot have two the
		// same."*
		//
		// So every form of projection produces a ROW WITH COLUMNS, and the query answers with a
		// table of them (procUnitLINQ.cpp, TableOfRows):
		//
		//   select { Total = …, Name = … }   the author's names
		//   select o.Ref.Code                a WALK, glued — `RefCode`
		//   select <anything else>           `Field1`, `Field2`, … — it has no natural name
		//   <omitted>                        the row itself, one column, same rule
		//
		// The rule is not invented here: it is the query engine's own (queryRewrite.h,
		// ibQueryProposedName / ibQueryEnsureUniqueName), down to the leading source name never
		// being part of a column name and a repeat being numbered. Two roads answering the same
		// question must answer it the same way, or a person learns two vocabularies.
		ibParamUnit addValue = b.valueSlot;
		if (!data.m_hasGroup) {

			// The row and the shape it points at. The row is made BEFORE its fields are computed,
			// but the names are known only when the projection has been read — so the instruction
			// goes down now and its operand is written in afterwards. A forward reference is
			// patched, not waited for; every jump in this compiler is emitted the same way.
			//
			// What this replaces emitted, PER ROW: `New Structure` — the class resolved through the
			// object factory BY NAME at run time — and then, per field, `Insert(name, value)`: a
			// method resolved by name, its two arguments loaded through a call frame, and the field
			// name stored inside the object so that every later `row.Field` had to look it up again.
			// Three instructions and two name lookups per field, per row, for a shape the compiler
			// was holding in full while it compiled.
			const ibParamUnit rowSlot   = context->CreateVariable();
			const ibParamUnit shapeSlot = context->CreateVariable();
			const int rowIp = (int)m_cByteCode.m_listCode.size();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LINQ_ROW;
				c.m_param1 = rowSlot;
				c.m_param2 = shapeSlot;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}

			// One place stores a column, whichever form named it — BY POSITION, so nothing about the
			// name reaches run time.
			std::vector<wxString> columnNames;
			const auto keepColumn = [&](const wxString& proposed, const ibParamUnit& value) {
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LINQ_FIELD;
				c.m_param1 = rowSlot;
				c.m_param2 = value;
				c.m_param3.m_numIndex = (long)columnNames.size();
				m_cByteCode.m_listCode.emplace_back(std::move(c));
				columnNames.push_back(UniqueColumnName(columnNames, proposed));
			};

			if (IsNextKeyWord(KEY_SELECT)) {
				GETKeyWord(KEY_SELECT);

				if (IsNextDelimeter('{')) {
					GETDelimeter('{');
					// `name = expr` pairs — the author names every column.
					while (!IsNextDelimeter('}')) {

						// 🛑 A CLAUSE LOOP HAS TO BE ABLE TO STOP WITHOUT A RAISE, and this one could
						// not. In the TOLERANT mode the editor compiles in, nothing throws and nothing
						// ends the read — and a caret inside a projection IS a text that stops there,
						// because ibCaretCompile truncates at the caret. So `select { C = o.` never met
						// its `}`: the loop turned forever, appending a column each time, and the
						// designer stopped answering. Found from a live break, 2026-09-08: the time was
						// burning inside UniqueColumnName, scanning a column list that grew without
						// bound — the symptom was two frames away from the cause.
						//
						// Two ways out, and both belong here. The tokens ran out is the case that
						// happens; a turn that CONSUMED NOTHING is the general one — a malformed pair
						// the tolerant parser declines to step over is not a shape worth enumerating,
						// it is simply no progress, and no progress twice is a loop.
						if (IsEndOfProgram())
							break;
						const int beforeTurn = m_numCurrentCompile;

						const wxString fieldName = GETIdentifier(true);
						GETDelimeter('=');
						keepColumn(fieldName, GetExpression(context));
						if (IsNextDelimeter(',')) GETDelimeter(',');

						if (m_numCurrentCompile <= beforeTurn)
							break;
					}
					GETDelimeter('}');
				}
				else {
					// One column, and its name is whatever the INSTRUCTIONS say the value is: a member
					// walk names itself, anything else is `Field1`. The range is the stretch the
					// expression emitted — noted before it runs, read after.
					const int emitFrom = (int)m_cByteCode.m_listCode.size();
					const ibParamUnit value = GetExpression(context);
					keepColumn(ColumnNameOfProjection(m_cByteCode, emitFrom,
						(int)m_cByteCode.m_listCode.size(), b.valueSlot, value), value);
				}
			}
			else {
				// No `select` written: the row itself is the one column, and it has no natural name.
				keepColumn(wxEmptyString, b.valueSlot);
			}

			// The shape, as ONE constant: the column names in the order they were written, which is
			// the order that made them ordinals.
			wxString shapeNames;
			for (size_t i = 0; i < columnNames.size(); ++i) {
				if (i != 0) shapeNames << wxT('\n');
				shapeNames << columnNames[i];
			}
			m_cByteCode.m_listCode[rowIp].m_param3.m_numIndex = GetConstString(shapeNames);
			m_cByteCode.m_listCode[rowIp].m_param3.m_numArray = (long)columnNames.size();

			// …and the same names go into the QUERY, which is the compiler's own record of what it
			// understood. The instructions carry them for the runtime; this carries them for the
			// readers on this side (byteCodeLINQ.h).
			data.m_columns = columnNames;

			addValue = rowSlot;
		}

		// DISTINCT modifier — `distinct` after select, which drops a row the query has already
		// produced.
		//
		// ⭐⭐ SEEN BEFORE? — ONE INSTRUCTION, ANSWERED IN log n.
		//
		// What this replaces asked the RESULT collection `Contains(row)` — a method resolved by NAME
		// that then walked everything kept so far, comparing values. Per row. So `distinct` over ten
		// thousand rows performed fifty million comparisons, and the shape of the emission (a call, a
		// NOT, a branch) hid that behind three ordinary-looking instructions.
		//
		// The light collection keeps an ordered set beside its rows — ordered because `ibValue` has a
		// total order and no hash at all — and answers the question directly.
		int distinctSkipIp = -1;
		if (IsNextKeyWord(KEY_DISTINCT)) {
			GETKeyWord(KEY_DISTINCT);

			const ibParamUnit fresh = context->CreateVariable();
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_LINQ_SEEN;
				c.m_param1 = fresh;
				c.m_param2 = data.m_resultArray;
				c.m_param3 = addValue;
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			{
				ibByteUnit c; AddLineInfo(c);
				c.m_numOper = OPER_IF;              // already seen -> past the keep
				c.m_param1 = fresh;
				c.m_param2.m_numIndex = 0;          // back-patch below
				m_cByteCode.m_listCode.emplace_back(std::move(c));
			}
			distinctSkipIp = (int)m_cByteCode.m_listCode.size() - 1;
		}

		// ⭐⭐ THE ROW AND ITS ORDERING KEY, KEPT TOGETHER, IN ONE INSTRUCTION.
		//
		// This was two calls resolved BY NAME (`__r.Add(row)` then `__keys.Add(key)`) into two script
		// Arrays — per row. Two names looked up, two user-visible objects grown one element at a
		// time, and the pairing of a row with its key held together only by both calls happening.
		// They go into the same collection now, so they cannot come apart.
		//
		// Suppressed when GROUP BY took over: grouping puts its rows in buckets as they arrive, and
		// the answer is built from those after the loop.
		if (!data.m_hasGroup) {
			// ⭐⭐ THE ROW GOES IN ONCE, THE KEYS ONE PER INSTRUCTION. `orderby a, b` needs two keys
			// against one row, and the instruction has room for one — so the FIRST KEEP carries the
			// row together with key 0, and each further key rides its own KEEP with the row operand
			// skipped. No second opcode, no widened operand: the fourth field, unused here until
			// now, says WHICH key this is (procUnitLINQ.cpp, KeepKey).
			const size_t keys = data.m_hasOrderBy ? data.m_orderByKeySlots.size() : 0;

			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LINQ_KEEP;
			c.m_param1 = data.m_resultArray;
			c.m_param2 = addValue;
			if (keys > 0) c.m_param3 = data.m_orderByKeySlots[0];
			else          c.m_param3.m_numArray = DEF_VAR_SKIP;
			c.m_param4.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));

			for (size_t k = 1; k < keys; k++) {
				ibByteUnit more; AddLineInfo(more);
				more.m_numOper = OPER_LINQ_KEEP;
				more.m_param1 = data.m_resultArray;
				more.m_param2.m_numArray = DEF_VAR_SKIP;   // the row is already kept
				more.m_param3 = data.m_orderByKeySlots[k];
				more.m_param4.m_numIndex = (long)k;
				m_cByteCode.m_listCode.emplace_back(std::move(more));
			}
		}

		// Back-patch DISTINCT skip target → past the Add (= NEXT_ITER).
		if (distinctSkipIp >= 0) {
			m_cByteCode.m_listCode[distinctSkipIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
		}

		// Back-patch all WHERE skip targets → past the Add (= NEXT_ITER).
		for (const int ifIp : whereSkipIps) {
			m_cByteCode.m_listCode[ifIp].m_param2.m_numIndex =
				(long)m_cByteCode.m_listCode.size();
		}
	}

	// === Close the bucket loops — innermost join first ===
	// A fall-through from the body's Add continues the INNERMOST bucket;
	// each exhausted bucket FOREACH jumps past its own NEXT_ITER, into
	// the next-outer bucket's close, and finally into the level's own
	// NEXT_ITER below. The leaf branch's whereSkipIps / DISTINCT targets
	// already point at the first close emitted here (= the innermost
	// continue), which is exactly "skip this joined row, take the next
	// match" — the fan-out analogue of the old "skip to next iter".
	std::vector<long> bucketCloseIp(pendingJoins.size(), 0);
	for (size_t k = pendingJoins.size(); k-- > 0; ) {
		const ibLinqPendingJoin& pj = pendingJoins[k];
		bucketCloseIp[k] = (long)m_cByteCode.m_listCode.size();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NEXT_ITER;
			c.m_param1 = pj.bucketItSlot;
			c.m_param2.m_numIndex = pj.bucketForeachIp;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		m_cByteCode.m_listCode[pj.bucketForeachIp].m_param4.m_numIndex =
			(long)m_cByteCode.m_listCode.size();
	}
	// A join-miss must NOT land on the innermost continue — that would
	// resume a bucket iterator this row never opened. It lands on the
	// PREVIOUS join's continue (whose loop IS open at that point), and
	// the first join's miss lands past every close, on the level's own
	// NEXT_ITER. The equality re-check, by contrast, fires INSIDE its
	// own open loop, so it continues its OWN bucket.
	for (size_t k = 0; k < pendingJoins.size(); ++k) {
		m_cByteCode.m_listCode[pendingJoins[k].missIfIp].m_param2.m_numIndex =
			(k > 0) ? bucketCloseIp[k - 1]
			        : (long)m_cByteCode.m_listCode.size();
		if (pendingJoins[k].recheckIfIp >= 0)
			m_cByteCode.m_listCode[pendingJoins[k].recheckIfIp].m_param2.m_numIndex =
				bucketCloseIp[k];
	}
	// SKIP's "drop this row" continues the innermost bucket, so skip
	// counts joined rows rather than outer ones.
	if (!skipContinueGotoIps.empty()) {
		const long innermostContinue = bucketCloseIp[pendingJoins.size() - 1];
		for (const int gotoIp : skipContinueGotoIps)
			m_cByteCode.m_listCode[gotoIp].m_param1.m_numIndex = innermostContinue;
	}

	// === Close this level's loop ===
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_NEXT_ITER;
		c.m_param1 = b.iterItSlot;
		c.m_param2.m_numIndex = b.foreachStartIp;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// === JOIN trampolines (Phase 1.5) ===
	// Live AFTER outer NEXT_ITER and BEFORE the foreach m_param4 patch
	// (= loop-exit target). NEXT_ITER always jumps back (never falls
	// through), so trampolines are unreachable in normal flow. On loop
	// exhaustion, OPER_FOREACH's m_param4 = post-trampolines ip, so
	// they're skipped on exit too. Reached only via the per-iter
	// conditional OPER_GOTO placeholder emitted inside the body by
	// CompileLinqJoin — fires once on first iter, the trampoline
	// builds the hash + jumps back to that join's skip_label.
	for (const ibLinqPendingJoin& pj : pendingJoins) {
		const int trampolineLabel = (int)m_cByteCode.m_listCode.size();

		// Patch the body-side placeholder GOTO to land here.
		m_cByteCode.m_listCode[pj.placeholderGotoIp].m_param1.m_numIndex =
			trampolineLabel;

		// ⭐ THE JOIN HASH IS LINQ'S OWN COLLECTION, made in its slot by the first instruction that
		// puts a row in it — so nothing is constructed here and no class is resolved by NAME.

		// Replay T's lex range — eval inner source ONCE into a temp.
		const int savedCursorT = m_numCurrentCompile;
		m_numCurrentCompile = pj.tLexStart;
		const ibParamUnit innerSrcSlot = GetExpression(linqCtx);
		m_numCurrentCompile = savedCursorT;

		// Inner foreach: foreach pj.bindSlot in innerSrcSlot.
		// We reuse pj.bindSlot as the inner iter-var; after hash build
		// it gets overwritten per outer-iter via Property's out-param.
		const ibParamUnit innerInSlot =
			linqCtx->GetVariable(wxString::Format(wxT("@join_in_%d"),
				(int)pendingJoins.size()), true, false, false, true);
		const ibParamUnit innerItSlot =
			linqCtx->GetVariable(wxString::Format(wxT("@join_it_%d"),
				(int)pendingJoins.size()), true, false, false, true);
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LET;
			c.m_param1 = innerInSlot;
			c.m_param2 = innerSrcSlot;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		const int innerForeachIp = (int)m_cByteCode.m_listCode.size();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_FOREACH;
			c.m_param1 = pj.bindSlot;
			c.m_param2 = innerInSlot;
			c.m_param3 = innerItSlot;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		// ⭐⭐ AND THE JOIN ALIAS NOW SAYS WHERE ITS ROW COMES FROM. `foreachStartIp` is the one
		// number a reader standing after `b.` needs — the header that PRODUCES the row cell, so the
		// walk starts inside this query instead of scanning the tape from the end (see
		// ibLinqBinding::foreachStartIp). The header exists; it was simply never written down, and
		// a join alias offered nothing while a `from` alias offered its whole row (measured
		// 2026-09-08). The binding is found by the cell, which is the only thing that ties the two.
		//
		// ⚠ THE SPAN IT IMPLIES IS NOT THE ALIAS'S SCOPE. This header belongs to the pass that
		// BUILDS the hash, which ends long before the clauses that read `b` — so the name list keeps
		// using the query's own text span for a join (scriptComplete.cpp) and takes only the row
		// from here.
		for (ibLinqBinding& bound : data.m_bindings) {
			if (bound.origin == ibLinqBinding::FromJoin
				&& bound.foreachStartIp < 0
				&& bound.valueSlot.m_numIndex == pj.bindSlot.m_numIndex) {
				bound.foreachStartIp = innerForeachIp;
				break;
			}
		}

		// Replay K2's lex range — emit K2 reading from pj.bindSlot
		// (which is the inner iter var here).
		const int savedCursorK2 = m_numCurrentCompile;
		m_numCurrentCompile = pj.k2LexStart;
		const ibParamUnit k2Slot = GetExpression(linqCtx);
		m_numCurrentCompile = savedCursorK2;

		const ibParamUnit buildFoundSlot    = linqCtx->CreateVariable();
		const ibParamUnit buildNotFoundSlot = linqCtx->CreateVariable();

		// Lookup-or-create the key's bucket, then Add the row into it —
		// the same emitted shape GROUP BY uses. A repeated key lands in
		// the existing bucket instead of hitting Container::Insert's
		// duplicate refusal (which raised out of the whole query), and
		// the lookup site fans out over the bucket.
		// ⭐⭐ THE INNER ROW GOES UNDER ITS KEY — ONE INSTRUCTION, WHERE THERE WERE SIX.
		//
		// The shape it replaces ran PER INNER ROW: `Property(k, bucket)` — a method resolved by NAME,
		// two arguments — then a NOT, a branch, `New Array` (the class resolved by NAME again),
		// `Insert(k, bucket)` by name, and `Add(row)` by name. Four name lookups and an object
		// construction, per row, to say "put this under that key".
		//
		// The light collection holds the buckets itself: the key is found in log n and the bucket is
		// made when it is first needed.
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_LINQ_BUCKET;
			c.m_param1 = pj.hashSlot;
			c.m_param2 = k2Slot;
			c.m_param3 = pj.bindSlot;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}

		// Close inner foreach.
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_NEXT_ITER;
			c.m_param1 = innerItSlot;
			c.m_param2.m_numIndex = innerForeachIp;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		m_cByteCode.m_listCode[innerForeachIp].m_param4.m_numIndex =
			(long)m_cByteCode.m_listCode.size();

		// GOTO back to the per-iter lookup's skip_label (return into
		// outer body after first-iter build).
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_GOTO;
			c.m_param1.m_numIndex = pj.skipLabelIp;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
	}

	m_cByteCode.m_listCode[b.foreachStartIp].m_param4.m_numIndex =
		(long)m_cByteCode.m_listCode.size();

	// GROUP BY expansion intentionally NOT emitted here — moved to
	// CompileLinqExpression's post-block emit so it fires ONCE after
	// the outermost foreach exhausts (not per inner-foreach exit).
	// Per-row aggregation (Insert into data.m_groupsContainer) is
	// emitted in the KEY_GROUP branch at whichever level the group
	// keyword appeared.

	context->FinishLoopList(m_cByteCode,
		(long)m_cByteCode.m_listCode.size() - 1,
		(long)m_cByteCode.m_listCode.size());
}

void ibCompileCode::CompileLinqJoin(ibCompileContext* linqCtx,
	std::vector<ibLinqPendingJoin>& pendingJoins)
{
	// KEY_JOIN already consumed by caller. Grammar:
	//   join <bindName> in <T> on <K1> equals <K2>
	//
	// Emit per-iter lookup INLINE in outer body (current ip):
	//   1. tmp_isEmpty = !hashSlot          (OPER_NOT)
	//   2. OPER_IF tmp_isEmpty, skipLabel   (jump on hashSlot non-empty)
	//   3. OPER_GOTO trampolineLabel        (placeholder, back-patched)
	//   4. skipLabel:
	//   5. found = hashSlot.Property(K1, bucketSlot)
	//   6. OPER_IF found, past-this-bucket   (pj.missIfIp, patched at close)
	//   7. bucket FOREACH over bucketSlot    (the fan-out; body runs per match)
	//
	// T and K2 lex ranges saved by parse-and-discard so the trampoline
	// (emitted after outer NEXT_ITER) can replay them in the build
	// context (T pre-loop, K2 inside inner foreach where bindSlot is
	// the iter var).
	ibLinqQuery& data = *linqCtx->m_linqQuery;

	const wxString joinName = GETIdentifier(true);
	GETKeyWord(KEY_IN);

	ibLinqPendingJoin pj;

	// Save T's lex range. Parse-and-discard: GetExpression advances
	// the cursor + emits bytecode; we resize m_listCode back to drop
	// the emit (replayed later at trampoline emit time). Side effects
	// like constant-pool inserts and var auto-decl persist but are
	// idempotent across the second parse.
	pj.tLexStart = m_numCurrentCompile;
	{
		// ⭐⭐ THE POSITION IS FIXED, THE BLOCK RUNS, AND THE ANSWER IS TAKEN OUT OF THE ARRAY AT THAT
		// POSITION — before the emission is dropped. What is asked of it is below.
		const size_t preSize = m_cByteCode.m_listCode.size();
		GetExpression(linqCtx);
		pj.m_needsReset = ReadsAnOuterBinding(m_cByteCode, (int)preSize,
			(int)m_cByteCode.m_listCode.size(), data.m_bindings);
		m_cByteCode.m_listCode.resize(preSize);
	}
	pj.tLexEnd = m_numCurrentCompile;

	GETKeyWord(KEY_ON);

	// Bind b in the linq scope. Slot persists across iterations; the
	// bucket FOREACH writes the current matching inner row into it (and
	// the trampoline reuses it as the inner build-loop iter var).
	pj.bindSlot = linqCtx->GetVariable(joinName);

	// Parse K1 inline — emits at current ip (outer body, per-iter).
	// K1 reads `o` (already bound by the enclosing FROM).
	const ibParamUnit k1Slot = GetExpression(linqCtx);

	GETKeyWord(KEY_EQUALS);

	// Save K2's lex range — same parse-and-discard pattern. K2
	// references bindSlot which is bound NOW (above), so the parse
	// resolves the identifier; emit goes to the discarded buffer.
	pj.k2LexStart = m_numCurrentCompile;
	{
		const size_t preSize = m_cByteCode.m_listCode.size();
		GetExpression(linqCtx);
		m_cByteCode.m_listCode.resize(preSize);
	}
	pj.k2LexEnd = m_numCurrentCompile;

	// Persistent hash slot — survives across outer iters; first-iter
	// trampoline allocates the Container into it.
	pj.hashSlot = linqCtx->CreateVariable();

	// Per-row reset for outer-referenced T — `hashSlot = empty` so
	// the IsEmpty guard below falls through to trampoline rebuild on
	// EVERY row. This sacrifices the hash amortisation (O(M²) instead
	// of O(M+N)) for correctness when T depends on outer iter vars.
	// Hoisting reset to outer-foreach body entry (one rebuild per
	// outer iter) is deferred — requires lookahead through level-N's
	// CompileLinqBlock before emitting OPER_FOREACH header.
	if (pj.m_needsReset) {
		// OPER_LET hashSlot = (default empty ibValue from const pool).
		// We can't easily emit "set TYPE_EMPTY" inline; the cleanest
		// trigger is OPER_NEW for an empty array / undefined slot —
		// but simpler is to leverage the IsEmpty path: assign a
		// known-empty constant. Use a const-pool empty ibValue.
		const ibParamUnit emptyConst = FindConst(ibValue());
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1 = pj.hashSlot;
		c.m_param2 = emptyConst;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (1) tmp_isEmpty = !hashSlot. Untyped OPER_NOT writes BOOLEAN
	// via SetTypeBoolean(IsEmptyValue) — TYPE_EMPTY → true, otherwise
	// false. Compile-side m_clsid stays 0 so no +TYPE_DELTA
	// inference fires (kept on the untyped IsEmpty path).
	const ibParamUnit tmpIsEmpty = linqCtx->CreateVariable();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_NOT;
		c.m_param1 = tmpIsEmpty;
		c.m_param2 = pj.hashSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (2) OPER_IF tmp_isEmpty, skipLabel
	// OPER_IF jumps when condition is FALSE (per runtime: if !cond,
	// goto). tmpIsEmpty TRUE on first iter (empty hash) → fall through
	// to GOTO trampoline. FALSE on subsequent (built) → jump to
	// skipLabel past the trampoline GOTO.
	const int condIfIp = (int)m_cByteCode.m_listCode.size();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_IF;
		c.m_param1 = tmpIsEmpty;
		c.m_param2.m_numIndex = 0;  // back-patched after the GOTO emit
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (3) OPER_GOTO trampolineLabel (placeholder — trampoline emitted
	// after outer NEXT_ITER patches param1 with its ip).
	pj.placeholderGotoIp = (int)m_cByteCode.m_listCode.size();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_GOTO;
		c.m_param1.m_numIndex = 0;   // back-patched in trampoline emit loop
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (4) skipLabel — back-patch the conditional IF to land here on
	// subsequent iters (hashSlot non-empty path).
	pj.skipLabelIp = (int)m_cByteCode.m_listCode.size();
	m_cByteCode.m_listCode[condIfIp].m_param2.m_numIndex = pj.skipLabelIp;

	// (5) found = hashSlot.Property(K1, bucketSlot)
	// A key maps to a BUCKET (Array of every matching inner row), so
	// Property hands back the bucket, never a row; returns false on
	// miss (bucketSlot left as previous content, never iterated).
	pj.bucketSlot = linqCtx->CreateVariable();
	const ibParamUnit foundSlot = linqCtx->CreateVariable();
	// ⭐⭐ THE MATCHING ROWS, AS A VIEW — per OUTER row, so this is the hottest instruction in a
	// join. It hands back the bucket itself, not a copy of it: building an Array here would
	// construct one object and copy every match, for every outer row.
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LINQ_BUCKET_GET;
		c.m_param1 = pj.bucketSlot;
		c.m_param2 = pj.hashSlot;
		c.m_param3 = k1Slot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	{
		// Found is "the bucket is not empty" — one test, no second lookup.
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1 = foundSlot;
		c.m_param2 = pj.bucketSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_SET;
		c.m_param1 = pj.bucketSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (6) OPER_IF found — on miss (foundSlot=false) jump PAST this
	// join's bucket loop: into the previous join's continue, or the
	// level NEXT_ITER for the first join. The target exists only once
	// the close section has emitted the bucket NEXT_ITERs, so it is
	// recorded on pj and patched there — NOT in whereSkipIps, whose
	// single shared target (the innermost continue) would resume a
	// bucket iterator this row never opened.
	pj.missIfIp = (int)m_cByteCode.m_listCode.size();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_IF;
		c.m_param1 = foundSlot;
		c.m_param2.m_numIndex = 0;   // back-patched in the close section
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (7)+(8) the bucket loop — the fan-out itself. The rest of the
	// body (further joins, wheres, the Add) sits INSIDE it, so one
	// outer row emits one result row per matching inner row, exactly
	// like the `.Join()` executor. Slot names carry the emit ip so
	// joins at different levels of a nested `from` can never share a
	// loop cell.
	{
		const int unique = (int)m_cByteCode.m_listCode.size();
		pj.bucketInSlot = linqCtx->GetVariable(
			wxString::Format(wxT("@jbkt_in_%d"), unique), true, false, false, true);
		pj.bucketItSlot = linqCtx->GetVariable(
			wxString::Format(wxT("@jbkt_it_%d"), unique), true, false, false, true);
	}
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_LET;
		c.m_param1 = pj.bucketInSlot;
		c.m_param2 = pj.bucketSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}
	pj.bucketForeachIp = (int)m_cByteCode.m_listCode.size();
	{
		ibByteUnit c; AddLineInfo(c);
		c.m_numOper = OPER_FOREACH;
		c.m_param1 = pj.bindSlot;
		c.m_param2 = pj.bucketInSlot;
		c.m_param3 = pj.bucketItSlot;
		m_cByteCode.m_listCode.emplace_back(std::move(c));
	}

	// (9) re-check the join equality per match, with the LANGUAGE's own
	// comparison. The Container's key comparator is looser than the
	// script `equals` — string keys compare case-insensitively — so keys
	// the language tells apart can land in one bucket. Rather than grow
	// a second index type, the loop re-evaluates K2 on the matched row
	// and drops a row the comparison refuses, continuing THIS bucket
	// exactly like a failed `where`. This is also what keeps the block
	// spelling semantically identical to the `.Join()` executor, whose
	// map compares with ibValue's own ordering.
	{
		const int savedCursorRecheck = m_numCurrentCompile;
		m_numCurrentCompile = pj.k2LexStart;
		const ibParamUnit k2vSlot = GetExpression(linqCtx);
		m_numCurrentCompile = savedCursorRecheck;

		const ibParamUnit eqSlot = linqCtx->CreateVariable();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_EQ;
			c.m_param1 = eqSlot;
			c.m_param2 = k1Slot;
			c.m_param3 = k2vSlot;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
		pj.recheckIfIp = (int)m_cByteCode.m_listCode.size();
		{
			ibByteUnit c; AddLineInfo(c);
			c.m_numOper = OPER_IF;   // jumps on false → this bucket's continue (patched at close)
			c.m_param1 = eqSlot;
			c.m_param2.m_numIndex = 0;
			m_cByteCode.m_listCode.emplace_back(std::move(c));
		}
	}

	// Push binding so subsequent clauses see `b` via standard linq
	// context lookup. Origin FromJoin in case future clauses (e.g.
	// `into g` group-join) need to distinguish.
	ibLinqBinding jb;
	jb.name      = joinName;
	jb.origin    = ibLinqBinding::FromJoin;
	jb.valueSlot = pj.bindSlot;
	data.m_bindings.push_back(jb);

	pendingJoins.push_back(pj);
}

#pragma warning(pop)
