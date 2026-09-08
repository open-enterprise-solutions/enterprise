////////////////////////////////////////////////////////////////////////////
//	L4-2 — lambda expression recorder: script lexemes -> ibQueryAstExpr
//	(lambdaQueryAST.h). Conservative: any doubt = null = RAM path.
////////////////////////////////////////////////////////////////////////////

#include "lambdaQueryAST.h"

#include "backend/query/queryAST.h"   // ibQueryAstExpr — the L4-1 AST the pushdown lowers
#include "backend/compiler/byteCode.h"       // the instructions the second recorder reads
#include "backend/compiler/compileContext.h" // DEF_VAR_CONST / DEF_VAR_NORET — how an operand names a pool
#include "codeDef.h"                  // KEY_* script keyword ids / OPER_* opcodes

// ⭐⭐ THE SECOND GRAMMAR IS GONE. Until 2026-09-08 this file also held a recursive-descent parser
// over the LEXEMES — a whole precedence chain, its own bracket matching, its own re-assembly of
// two-character operators from adjacent delimiters — that built the SAME tree by reading the text a
// second time. Two readers of one language: every grammar change had to be made twice, and the
// second one late or not at all, which is exactly why LINQ lagged.
//
// It was retired by measurement, not by argument: both readers ran side by side, each journalled
// its verdict, and they produced the same TREE for every shape the language can express (32 of 32).
// The single divergence was the lexeme reader ACCEPTING `x.Code in (…)`, which the compiler
// answers with "Symbol expected ')'" — there is no `in` operator. A reader whose grammar is WIDER
// than the language records predicates the program does not contain, and that settled which of the
// two could be the authority.
//
// What is left below reads the INSTRUCTIONS. It is not a parser at all: it is the def-use walk the
// caret answer already runs on, asked a different question.

////////////////////////////////////////////////////////////////////////////
//	THE SAME TREE, FROM THE INSTRUCTIONS — see the header for why.
////////////////////////////////////////////////////////////////////////////

namespace {

// The def-use walk over a lambda's own body: an instruction names the slot it read, that slot was
// written by an earlier instruction, and the chain unwinds itself. Bounded to the body, so a slot
// number cannot be answered by a different frame's cell — the same guard the caret walk needs.
struct CodeCur {
	const ibByteCode& bc;
	// ⭐ NULL WHEN THE RANGE IS NOT A LAMBDA BODY. A chain compiled as a loop has its predicate as a
	// plain stretch of instructions in the enclosing frame — no function record, no parameter list —
	// so the row is named by its SLOT (rowSlot below) and the frame's own cells are named by whoever
	// is compiling. One reader, both shapes.
	const ibByteCode::ibByteFunction* fn;
	long from;          // the OPER_LFUNC itself, or the first instruction of the range
	long to;            // the OPER_ENDLFUNC, or one past the range
	wxString* refusal;
	// The one question the instructions cannot answer on their own — see NameOfSlot / the header.
	const std::function<wxString(long, long)>& nameOfOuter;
	// Set only for a range: the cell the row arrives in. Null = ask the function record, as before.
	const ibParamRunUnit* rowSlot = nullptr;

	ibQueryAstExprPtr Refuse(const wxChar* what, unsigned int at) const {
		if (refusal != nullptr && refusal->IsEmpty())
			*refusal = wxString::Format(wxT("%s (at %u)"), what, at);
		return nullptr;
	}

	// A slot at or below zero is THIS frame (procUnit.cpp, ResolveRead) — a temp carries
	// DEF_VAR_TEMP rather than a depth, and both land on the same cell.
	static wxLongLong_t FrameOf(const ibParamRunUnit& p) {
		return p.m_numArray <= 0 ? 0 : p.m_numArray;
	}
	static bool SameSlot(const ibParamRunUnit& a, const ibParamRunUnit& b) {
		return FrameOf(a) == FrameOf(b) && a.m_numIndex == b.m_numIndex;
	}

	// 🛑 A CELL NUMBER IS ONLY A NAME WITHIN ITS OWN FRAME. This used to scan the lambda's locals by
	// index alone, so a captured `Limit` — which arrives as `frame 1, cell 0` — was answered with
	// cell 0 of the LAMBDA, i.e. the row parameter, and `x.Article > Limit` recorded as
	// `Article > &x`. The tree existed, translated, and asked a different question; comparing the
	// two recorders by whether they ANSWERED could never have caught it (2026-09-08).
	//
	// The outer frames are not in the bytecode yet (the enclosing function is still being compiled),
	// so their names come from whoever passed nameOfOuter — see the header.
	wxString NameOfSlot(const ibParamRunUnit& p) const {
		const wxLongLong_t frame = FrameOf(p);
		if (frame != 0 || fn == nullptr)
			return nameOfOuter ? nameOfOuter((long)frame, (long)p.m_numIndex) : wxString();

		for (const auto& var : fn->m_listLocals)
			if ((wxLongLong_t)var.m_slotIndex == p.m_numIndex)
				return var.m_strRealName;
		return wxEmptyString;
	}

	// 🛑 THE ROW IS RECOGNISED BY ITS NAME, NOT BY ITS CELL NUMBER. Parameter 0 does live at cell 0,
	// so "cell 0 is the row" reads like a fact — and it is one only while nothing ELSE lands there.
	// Measured 2026-09-08: a captured name the body never writes also came back as cell 0, so
	// `x.Article > Limit` was read as a comparison against THE ROW and refused. Names are what the
	// language gave these two things to tell them apart; the cell number is an implementation
	// detail they happen to share. (Same family as the caret walk's `Data` at cell zero.)
	bool IsRowSlot(const ibParamRunUnit& p) const {
		// A range says its row outright — there is no parameter list to compare a name against.
		if (rowSlot != nullptr)
			return SameSlot(p, *rowSlot);
		if (fn == nullptr || fn->m_listParam.empty() || FrameOf(p) != 0)
			return false;
		const wxString name = NameOfSlot(p);
		// ⭐ EITHER PARAMETER IS A ROW when there are two — a join ON is `(s, a) => s.k <op> a.k`, and
		// both sides name a row. Which side is which is read off the comparison by whoever consumes
		// the tree, not out of the names.
		if (name.IsEmpty())
			return false;
		for (const ibByteCode::ibByteParam& param : fn->m_listParam)
			if (name.CmpNoCase(param.m_strName) == 0)
				return true;
		return false;
	}
};

// A Column with an EMPTY path is the row itself, before any member has been read. A sentinel rather
// than a node kind of its own: GET_A appends to it, and a bare one left at the end is the same
// refusal the lexeme recorder gives for a row used as a value.
ibQueryAstExprPtr MakeRowMarker()
{
	return ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
}

bool IsRowMarker(const ibQueryAstExprPtr& e)
{
	return e && e->m_kind == ibQueryAstExprKind::Column && e->m_path.empty();
}

ibQueryAstExprPtr ResolveSlot(const CodeCur& c, const ibParamRunUnit& slot, int depth);

// 🛑 AN INSTRUCTION THAT PRODUCES NOTHING IS NOT A PRODUCER, and matching on the slot alone is not
// enough to tell: a block marker and an argument marker carry ALL-ZERO operands, which read as slot
// zero — a perfectly ordinary slot. The caret walk learned this the hard way (scriptComplete.cpp,
// where it took real modules from 19% to 66%); this reader needs the same rule, or the scan stops at
// the first structural instruction that happens to sit on the number it is looking for.
//
// So the opcode has to CLAIM the slot. Everything claimed is either translated below or refused by
// name; everything unclaimed is stepped over and the search goes on.
bool ClaimsItsSlot(short oper)
{
	switch (oper) {
	case OPER_EQ: case OPER_NE: case OPER_LS:
	case OPER_GT: case OPER_LE: case OPER_GE:
	case OPER_AND: case OPER_OR: case OPER_NOT:
	case OPER_ADD: case OPER_SUB: case OPER_MULT: case OPER_DIV: case OPER_MOD:
	case OPER_GET_A: case OPER_LET: case OPER_CONST:
	case OPER_CALL: case OPER_CALL_METHOD: case OPER_CALL_LINQ: case OPER_CALL_LAMBDA:
	case OPER_GET_ARRAY:
		return true;
	default:
		return false;
	}
}

// One instruction, one node. Everything the tree needs is an operand of the instruction that
// produced the slot — which is why this reads the OPCODE and never the text.
ibQueryAstExprPtr NodeOf(const CodeCur& c, const ibByteUnit& code, int depth)
{
	const short oper = code.m_numOper % TYPE_DELTA1;

	const auto binary = [&](ibQueryAstExprKind kind) -> ibQueryAstExprPtr {
		ibQueryAstExprPtr lhs = ResolveSlot(c, code.m_param2, depth + 1);
		if (!lhs) return nullptr;
		ibQueryAstExprPtr rhs = ResolveSlot(c, code.m_param3, depth + 1);
		if (!rhs) return nullptr;
		// 🛑 THE ROW MARKER MUST NOT TRAVEL AS AN OPERAND. It stands for "the row, before any member
		// was read", and comparing it — `x = x` — is exactly the shape the lexeme reader refuses.
		// Checking only the FINISHED tree let it through inside a Compare, which is the same class of
		// mistake as checking a value and not the verb that produced it.
		if (IsRowMarker(lhs) || IsRowMarker(rhs))
			return c.Refuse(wxT("the row itself is used as a value; a column has to be named"),
				code.m_numString);
		auto e = ibQueryAstExpr::Make(kind);
		e->m_lhs = lhs;
		e->m_rhs = rhs;
		e->m_col = code.m_numString;
		return e;
	};

	switch (oper) {

	case OPER_EQ: case OPER_NE: case OPER_LS:
	case OPER_GT: case OPER_LE: case OPER_GE: {
		ibQueryAstExprPtr e = binary(ibQueryAstExprKind::Compare);
		if (!e) return nullptr;
		e->m_cmp = oper == OPER_EQ ? ibQueryCompareOp::Eq
			: oper == OPER_NE ? ibQueryCompareOp::Ne
			: oper == OPER_LS ? ibQueryCompareOp::Lt
			: oper == OPER_LE ? ibQueryCompareOp::Le
			: oper == OPER_GT ? ibQueryCompareOp::Gt
			: ibQueryCompareOp::Ge;
		return e;
	}

	case OPER_AND: case OPER_OR: {
		ibQueryAstExprPtr e = binary(ibQueryAstExprKind::Logical);
		if (!e) return nullptr;
		e->m_isOr = (oper == OPER_OR);
		return e;
	}

	case OPER_ADD: case OPER_SUB: case OPER_MULT: case OPER_DIV: case OPER_MOD: {
		ibQueryAstExprPtr e = binary(ibQueryAstExprKind::Arith);
		if (!e) return nullptr;
		e->m_arith = oper == OPER_ADD ? ibQueryArithOp::Add
			: oper == OPER_SUB ? ibQueryArithOp::Sub
			: oper == OPER_MULT ? ibQueryArithOp::Mul
			: oper == OPER_DIV ? ibQueryArithOp::Div
			: ibQueryArithOp::Mod;
		return e;
	}

	case OPER_NOT: {
		ibQueryAstExprPtr inner = ResolveSlot(c, code.m_param2, depth + 1);
		if (!inner) return nullptr;
		if (IsRowMarker(inner))
			return c.Refuse(wxT("the row itself is used as a value; a column has to be named"),
				code.m_numString);
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Not);
		e->m_lhs = inner;
		e->m_col = code.m_numString;
		return e;
	}

	// A member read: the receiver is the chain so far, the name is the next segment. A member of
	// anything but the row is not a column — it is a value the query cannot see.
	case OPER_GET_A: {
		const long nameIdx = (long)code.m_param3.m_numIndex;
		if (nameIdx < 0 || (size_t)nameIdx >= c.bc.m_listConst.size())
			return c.Refuse(wxT("a member read with no name"), code.m_numString);

		ibQueryAstExprPtr owner = ResolveSlot(c, code.m_param2, depth + 1);
		if (!owner) return nullptr;
		if (owner->m_kind != ibQueryAstExprKind::Column)
			return c.Refuse(wxT("a field of a captured value; only a plain captured value travels"),
				code.m_numString);

		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		e->m_path = owner->m_path;
		e->m_path.push_back(c.bc.m_listConst[(size_t)nameIdx].GetString());
		e->m_col = code.m_numString;
		return e;
	}

	// Forwarding steps: the value IS whatever the slot they read turned out to be.
	case OPER_LET: case OPER_CONST:
		return ResolveSlot(c, code.m_param2, depth + 1);

	case OPER_CALL: case OPER_CALL_METHOD: case OPER_CALL_LINQ: case OPER_CALL_LAMBDA:
		return c.Refuse(wxT("a call inside the predicate"), code.m_numString);

	case OPER_GET_ARRAY:
		return c.Refuse(wxT("an indexed element"), code.m_numString);

	default:
		break;
	}

	return c.Refuse(wxT("a construct the query subset does not cover"), code.m_numString);
}

// What wrote this slot, searched backwards inside the lambda's own body.
ibQueryAstExprPtr ResolveSlot(const CodeCur& c, const ibParamRunUnit& slot, int depth)
{
	if (depth > 64)
		return c.Refuse(wxT("an expression nested past any reasonable depth"), 0);

	// A CONSTANT IS ITS OWN ANSWER — no producer to look for.
	if (slot.m_numArray == DEF_VAR_CONST) {
		if (slot.m_numIndex < 0 || (size_t)slot.m_numIndex >= c.bc.m_listConst.size())
			return c.Refuse(wxT("a constant that is not in the pool"), 0);
		auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		e->m_literal = c.bc.m_listConst[(size_t)slot.m_numIndex];
		return e;
	}

	for (long ip = c.to - 1; ip > c.from; --ip) {
		const ibByteUnit& code = c.bc.m_listCode[(size_t)ip];
		if (!ClaimsItsSlot(code.m_numOper % TYPE_DELTA1))
			continue;
		if (!CodeCur::SameSlot(code.m_param1, slot))
			continue;
		return NodeOf(c, code, depth);
	}

	// Nothing in the body wrote it, so it came from outside: the row itself, or a captured value.
	if (c.IsRowSlot(slot))
		return MakeRowMarker();

	const wxString name = c.NameOfSlot(slot);

	// ⭐⭐ IN RANGE MODE A NAME IS NOT NEEDED AT ALL. The predicate of a loop reads locals of the very
	// frame it runs in, and those are written OUTSIDE the range — so there is no producer to find and
	// no symbol table to consult. There does not have to be: the operand already says WHERE the value
	// is, and that is what the fold reads. The name that goes on the node is for diagnostics and for
	// the lowering's map key, nothing more.
	// ⭐⭐ A NAME IS NOT NEEDED AT ALL, and that is what lets the tree go unstored. The operand already
	// says WHERE the value is — a frame and a cell — and that is what the fold reads. A name is only
	// wanted so a human, and the lowering's own map, have something to call it; where there is none
	// (a range's own frame, or a lambda whose enclosing symbol table is not compiled yet) the
	// coordinates stand on their own.
	auto p = ibQueryAstExpr::Make(ibQueryAstExprKind::Param);
	p->m_paramName = name.IsEmpty()
		? wxString::Format(wxT("@cell%d_%d"), (int)CodeCur::FrameOf(slot), (int)slot.m_numIndex)
		: name;
	// …AND WHERE IT LIVES, which the instruction just said. The fold reads the value straight out of
	// that frame instead of searching the frames for the name (queryAST.h, m_capturedFrame).
	p->m_capturedFrame = (long)CodeCur::FrameOf(slot);
	p->m_capturedSlot  = (long)slot.m_numIndex;
	return p;
}

} // namespace

std::shared_ptr<ibQueryAstExpr> ibBuildLambdaQueryAstFromCode(
	const ibByteCode& byteCode, long funcIndex, wxString* outRefusal,
	const std::function<wxString(long, long)>& nameOfOuter)
{
	if (outRefusal != nullptr)
		outRefusal->clear();

	if (funcIndex < 0 || (size_t)funcIndex >= byteCode.m_listFunc.size())
		return nullptr;

	const ibByteCode::ibByteFunction& fn = byteCode.m_listFunc[(size_t)funcIndex];
	// One parameter is a predicate over a row; TWO are a join ON — `(s, a) => s.k <op> a.k` — where
	// both are rows and the side tells them apart. Nothing else is a query.
	if (fn.m_listParam.empty() || fn.m_listParam.size() > 2)
		return nullptr;

	// ⚠ THE BODY'S BOUNDS COME OFF THE OPENING INSTRUCTION, not off the function entry. The bytecode
	// entry carries only where the body BEGINS (m_lCodeLine, the OPER_LFUNC); where it ends is
	// stamped into that instruction's own operand at compile time (procUnit.cpp reads it the same
	// way to skip the body at module-init).
	const long from = fn.m_lCodeLine;
	if (from < 0 || (size_t)from >= byteCode.m_listCode.size())
		return nullptr;

	const long to = (long)byteCode.m_listCode[(size_t)from].m_param2.m_numIndex;
	if (to <= from || (size_t)to >= byteCode.m_listCode.size())
		return nullptr;

	const CodeCur c{ byteCode, &fn, from, to, outRefusal, nameOfOuter };

	// The body's answer is what it RETURNS, and the return names its slot — so the tree is that slot
	// resolved. A body with no return has nothing to translate.
	for (long ip = to - 1; ip > from; --ip) {

		const ibByteUnit& code = byteCode.m_listCode[(size_t)ip];
		if ((code.m_numOper % TYPE_DELTA1) != OPER_RET)
			continue;
		if (code.m_param1.m_numArray == DEF_VAR_NORET)
			return c.Refuse(wxT("the body returns nothing"), code.m_numString);

		ibQueryAstExprPtr expr = ResolveSlot(c, code.m_param1, 0);
		if (!expr)
			return nullptr;
		if (IsRowMarker(expr))
			return c.Refuse(wxT("the row itself is used as a value; a column has to be named"),
				code.m_numString);
		return expr;
	}

	return c.Refuse(wxT("the body does more than return one expression"), 0);
}

std::shared_ptr<ibQueryAstExpr> ibBuildQueryAstFromRange(
	const ibByteCode& byteCode, long from, long to,
	const ibParamRunUnit& rowSlot, const ibParamRunUnit& resultSlot,
	wxString* outRefusal, const std::function<wxString(long, long)>& nameOfSlot)
{
	if (outRefusal != nullptr)
		outRefusal->clear();

	if (from < 0 || to <= from || (size_t)to > byteCode.m_listCode.size())
		return nullptr;

	// No function record and no `Return` to look for: a range ANSWERS with a slot, and the tree is
	// that slot resolved. Everything else — the column walk, the operators, the literals, the
	// captures — is the same reader, because it was never about lambdas; it was about instructions.
	const CodeCur c{ byteCode, nullptr, from, to, outRefusal, nameOfSlot, &rowSlot };

	ibQueryAstExprPtr expr = ResolveSlot(c, resultSlot, 0);
	if (!expr)
		return nullptr;
	if (IsRowMarker(expr))
		return c.Refuse(wxT("the row itself is used as a value; a column has to be named"), 0);
	return expr;
}

////////////////////////////////////////////////////////////////////////////
//	Saying a tree out loud — see ibDescribeQueryAst in the header for why.
////////////////////////////////////////////////////////////////////////////

namespace {

wxString DescribeNode(const ibQueryAstExprPtr& e);

wxString JoinPath(const std::vector<wxString>& path)
{
	wxString out;
	for (const wxString& seg : path) {
		if (!out.IsEmpty()) out << wxT('.');
		out << seg;
	}
	return out;
}

wxString DescribeList(const std::vector<ibQueryAstExprPtr>& items)
{
	wxString out;
	for (const ibQueryAstExprPtr& item : items) {
		if (!out.IsEmpty()) out << wxT(", ");
		out << DescribeNode(item);
	}
	return out;
}

const wxChar* CompareWord(ibQueryCompareOp op)
{
	switch (op) {
	case ibQueryCompareOp::Eq: return wxT("=");
	case ibQueryCompareOp::Ne: return wxT("<>");
	case ibQueryCompareOp::Lt: return wxT("<");
	case ibQueryCompareOp::Le: return wxT("<=");
	case ibQueryCompareOp::Gt: return wxT(">");
	case ibQueryCompareOp::Ge: return wxT(">=");
	}
	return wxT("?");
}

const wxChar* ArithWord(ibQueryArithOp op)
{
	switch (op) {
	case ibQueryArithOp::Add: return wxT("+");
	case ibQueryArithOp::Sub: return wxT("-");
	case ibQueryArithOp::Mul: return wxT("*");
	case ibQueryArithOp::Div: return wxT("/");
	case ibQueryArithOp::Mod: return wxT("%");
	}
	return wxT("?");
}

wxString DescribeNode(const ibQueryAstExprPtr& e)
{
	if (!e)
		return wxT("-");

	switch (e->m_kind) {

	case ibQueryAstExprKind::Column: {
		// The dotted path as written. A path that starts behind a CAST says so, because two columns
		// with the same name off different casts are not the same column.
		const wxString path = JoinPath(e->m_path);
		return e->m_arg ? DescribeNode(e->m_arg) + wxT(".") + path : path;
	}

	// A literal is quoted when it IS a string, so `x.Code = "100"` and `x.Code = 100` do not render
	// the same — the whole point of a canonical form is that two different trees look different.
	case ibQueryAstExprKind::Literal:
		return e->m_literal.GetType() == ibValueTypes::TYPE_STRING
			? wxT("\"") + e->m_literal.GetString() + wxT("\"")
			: e->m_literal.GetString();

	// A capture shows its ADDRESS, because that is what the fold now reads — and a rendering that
	// hid it would agree with a tree that still searches for the name.
	case ibQueryAstExprKind::Param:
		return e->m_capturedFrame > 0
			? wxString::Format(wxT("&%s@%d:%d"), e->m_paramName,
				(int)e->m_capturedFrame, (int)e->m_capturedSlot)
			: wxT("&") + e->m_paramName;

	case ibQueryAstExprKind::Value:
		return wxT("value(") + JoinPath(e->m_path) + wxT(")");

	case ibQueryAstExprKind::Compare:
		return wxT("(") + DescribeNode(e->m_lhs) + wxT(" ") + CompareWord(e->m_cmp) + wxT(" ")
		     + DescribeNode(e->m_rhs) + wxT(")");

	case ibQueryAstExprKind::Arith:
		return wxT("(") + DescribeNode(e->m_lhs) + wxT(" ") + ArithWord(e->m_arith) + wxT(" ")
		     + DescribeNode(e->m_rhs) + wxT(")");

	case ibQueryAstExprKind::Logical:
		return wxT("(") + DescribeNode(e->m_lhs) + (e->m_isOr ? wxT(" OR ") : wxT(" AND "))
		     + DescribeNode(e->m_rhs) + wxT(")");

	case ibQueryAstExprKind::Not:
		return wxT("NOT ") + DescribeNode(e->m_lhs);

	case ibQueryAstExprKind::In:
		return wxT("(") + DescribeNode(e->m_lhs) + (e->m_negated ? wxT(" NOT IN (") : wxT(" IN ("))
		     + (e->m_subquery ? wxString(wxT("<subquery>")) : DescribeList(e->m_list)) + wxT("))");

	case ibQueryAstExprKind::Like:
		return wxT("(") + DescribeNode(e->m_lhs) + (e->m_negated ? wxT(" NOT LIKE ") : wxT(" LIKE "))
		     + DescribeNode(e->m_rhs) + wxT(")");

	case ibQueryAstExprKind::IsNull:
		return wxT("(") + DescribeNode(e->m_lhs) + (e->m_negated ? wxT(" IS NOT NULL)") : wxT(" IS NULL)"));

	case ibQueryAstExprKind::Between:
		return wxT("(") + DescribeNode(e->m_lhs) + (e->m_negated ? wxT(" NOT BETWEEN ") : wxT(" BETWEEN "))
		     + DescribeNode(e->m_low) + wxT(" AND ") + DescribeNode(e->m_high) + wxT(")");

	case ibQueryAstExprKind::Refs:
		return wxT("(") + DescribeNode(e->m_lhs) + (e->m_negated ? wxT(" NOT REFS ") : wxT(" REFS "))
		     + JoinPath(e->m_path) + wxT(")");

	case ibQueryAstExprKind::Cast:
		return wxT("CAST(") + DescribeNode(e->m_arg) + wxT(" AS ") + JoinPath(e->m_path) + wxT(")");

	case ibQueryAstExprKind::Func:
		return wxString::Format(wxT("fold#%d(%s%s)"), (int)e->m_func,
			e->m_distinctArg ? wxT("DISTINCT ") : wxT(""),
			e->m_star ? wxString(wxT("*")) : DescribeNode(e->m_arg));

	case ibQueryAstExprKind::ScalarCall:
		return wxString::Format(wxT("call#%d(%s)"), (int)e->m_scalar, DescribeList(e->m_args));

	case ibQueryAstExprKind::Case: {
		wxString out = wxT("CASE");
		for (const std::pair<ibQueryAstExprPtr, ibQueryAstExprPtr>& branch : e->m_cases)
			out << wxT(" WHEN ") << DescribeNode(branch.first) << wxT(" THEN ") << DescribeNode(branch.second);
		if (e->m_else) out << wxT(" ELSE ") << DescribeNode(e->m_else);
		return out + wxT(" END");
	}
	}

	// A kind added later renders as its number rather than silently as something else — the rule the
	// AOT serialiser learned the hard way, where an unknown kind read as "not translatable".
	return wxString::Format(wxT("<kind %d>"), (int)e->m_kind);
}

} // namespace

wxString ibDescribeQueryAst(const std::shared_ptr<ibQueryAstExpr>& expr, bool withAddresses)
{
	const wxString said = DescribeNode(expr);
	if (withAddresses)
		return said;

	// Drop `@frame:slot` after a capture. Taken off the finished line rather than threaded through
	// twenty-three recursive calls, and safe to do here because the format is this file's own: the
	// suffix follows `&<identifier>`, and an identifier cannot contain `@`.
	wxString out;
	out.reserve(said.length());
	for (size_t i = 0; i < said.length(); ++i) {
		if (said[i] != wxT('@')) { out << said[i]; continue; }
		size_t j = i + 1;
		while (j < said.length() && (wxIsdigit(said[j]) || said[j] == wxT(':'))) ++j;
		i = j - 1;   // skip the coordinate; the loop's ++i lands on the first char after it
	}
	return out;
}
