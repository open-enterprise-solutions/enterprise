////////////////////////////////////////////////////////////////////////////
//	L4-2 — lambda expression recorder: script lexemes -> ibQueryAstExpr
//	(lambdaQueryAst.h). Conservative: any doubt = null = RAM path.
////////////////////////////////////////////////////////////////////////////

#include "lambdaQueryAst.h"

#include "backend/query/queryAst.h"   // ibQueryAstExpr — the L4-1 AST the pushdown lowers
#include "codeDef.h"                  // KEY_* script keyword ids

namespace {

// Cursor over the body span. All Parse* return null on a subset violation;
// null propagates up and the recorder yields null (bail-out, not an error).
struct Cur
{
	const std::vector<ibLexem>& lex;
	size_t pos;
	size_t end;
	const wxString& rowParam;

	bool AtEnd() const { return pos >= end; }
	const ibLexem& Peek(size_t ahead = 0) const {
		static const ibLexem null{};
		return (pos + ahead < end) ? lex[pos + ahead] : null;
	}

	bool IsDelim(wxChar c, size_t ahead = 0) const {
		const ibLexem& l = Peek(ahead);
		return l.m_lexType == DELIMITER && l.m_numData == (short)c;
	}
	bool IsKey(int key, size_t ahead = 0) const {
		const ibLexem& l = Peek(ahead);
		return l.m_lexType == KEYWORD && l.m_numData == (short)key;
	}

	// Two single-character delimiter lexemes form ONE operator only when they
	// are source-adjacent (same line, next column) — "< =" must not pair.
	bool DelimPair(wxChar a, wxChar b) const {
		if (!IsDelim(a, 0) || !IsDelim(b, 1)) return false;
		const ibLexem& l0 = Peek(0);
		const ibLexem& l1 = Peek(1);
		return l0.m_numLine == l1.m_numLine && l1.m_numString == l0.m_numString + 1;
	}
};

ibQueryAstExprPtr ParseOr(Cur& c);   // fwd — full precedence chain below

ibQueryAstExprPtr MakeLiteral(const ibValue& v)
{
	auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
	e->m_literal = v;
	return e;
}

// The REAL-cased identifier name: the lexer normalises m_strData (the upper-case
// lookup key) and keeps the as-written spelling in m_valData. Column paths and
// captured-parameter names must carry the real casing (resolution is
// case-insensitive, but metadata names and watch strings need the true spelling).
wxString RealName(const ibLexem& l)
{
	const wxString real = l.m_valData.GetString();
	return real.IsEmpty() ? l.m_strData : real;
}

// primary := '(' or ')' | literal | TRUE/FALSE/NULL | rowParam '.' chain |
//            captured identifier | '!' primary | '-' numberLiteral
ibQueryAstExprPtr ParsePrimary(Cur& c)
{
	if (c.AtEnd()) return nullptr;
	const ibLexem& t = c.Peek();

	if (c.IsDelim(wxT('('))) {
		++c.pos;
		ibQueryAstExprPtr inner = ParseOr(c);
		if (!inner || !c.IsDelim(wxT(')'))) return nullptr;
		++c.pos;
		return inner;
	}

	// unary NOT — CES '!' (the keyword form is handled at the NOT level)
	if (c.IsDelim(wxT('!')) && !c.DelimPair(wxT('!'), wxT('='))) {
		++c.pos;
		ibQueryAstExprPtr inner = ParsePrimary(c);
		if (!inner) return nullptr;
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Not);
		n->m_lhs = inner;
		return n;
	}

	// unary minus — folded into a negated NUMBER literal only (else bail)
	if (c.IsDelim(wxT('-'))) {
		const ibLexem& next = c.Peek(1);
		if (next.m_lexType == CONSTANT && next.m_valData.GetType() == ibValueTypes::TYPE_NUMBER) {
			c.pos += 2;
			return MakeLiteral(ibValue(ibNumber(0) - next.m_valData.GetNumber()));
		}
		return nullptr;
	}

	if (t.m_lexType == CONSTANT) {
		++c.pos;
		return MakeLiteral(t.m_valData);
	}
	if (c.IsKey(KEY_TRUE) || c.IsKey(KEY_FALSE)) {
		const bool val = c.IsKey(KEY_TRUE);
		++c.pos;
		return MakeLiteral(ibValue(val));
	}
	if (c.IsKey(KEY_NULL)) {
		++c.pos;
		ibValue v; v.SetType(ibValueTypes::TYPE_NULL);
		return MakeLiteral(v);
	}

	if (t.m_lexType == IDENTIFIER) {
		const bool isRowParam = RealName(t).CmpNoCase(c.rowParam) == 0;
		++c.pos;
		if (isRowParam) {
			// the row parameter MUST be dereferenced — a bare row value has no
			// translatable meaning; its member chain becomes the Column path
			// (WITHOUT the parameter segment, matching the source's columns)
			if (!c.IsDelim(wxT('.'))) return nullptr;
			auto e = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
			e->m_line = t.m_numLine; e->m_col = t.m_numString;
			while (c.IsDelim(wxT('.'))) {
				++c.pos;
				const ibLexem& seg = c.Peek();
				if (seg.m_lexType != IDENTIFIER) return nullptr;
				e->m_path.push_back(RealName(seg));
				++c.pos;
			}
			// a method call on the chain (x.Name.Trim()) is not translatable
			if (c.IsDelim(wxT('('))) return nullptr;
			return e;
		}
		// captured outer identifier -> Param node (value resolved by name from
		// the captured frames at execute time). A member access or a call on a
		// captured object is NOT translatable (the value must be a plain scalar).
		if (c.IsDelim(wxT('.')) || c.IsDelim(wxT('('))) return nullptr;
		auto p = ibQueryAstExpr::Make(ibQueryAstExprKind::Param);
		p->m_paramName = RealName(t);
		p->m_line = t.m_numLine; p->m_col = t.m_numString;
		return p;
	}

	return nullptr;
}

ibQueryAstExprPtr ParseMulDiv(Cur& c)
{
	ibQueryAstExprPtr lhs = ParsePrimary(c);
	while (lhs) {
		ibQueryArithOp op;
		if      (c.IsDelim(wxT('*'))) op = ibQueryArithOp::Mul;
		else if (c.IsDelim(wxT('/'))) op = ibQueryArithOp::Div;
		else if (c.IsDelim(wxT('%'))) op = ibQueryArithOp::Mod;
		else break;
		++c.pos;
		ibQueryAstExprPtr rhs = ParsePrimary(c);
		if (!rhs) return nullptr;
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Arith);
		n->m_arith = op; n->m_lhs = lhs; n->m_rhs = rhs;
		lhs = n;
	}
	return lhs;
}

ibQueryAstExprPtr ParseAddSub(Cur& c)
{
	ibQueryAstExprPtr lhs = ParseMulDiv(c);
	while (lhs) {
		ibQueryArithOp op;
		if      (c.IsDelim(wxT('+'))) op = ibQueryArithOp::Add;
		else if (c.IsDelim(wxT('-'))) op = ibQueryArithOp::Sub;
		else break;
		++c.pos;
		ibQueryAstExprPtr rhs = ParseMulDiv(c);
		if (!rhs) return nullptr;
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Arith);
		n->m_arith = op; n->m_lhs = lhs; n->m_rhs = rhs;
		lhs = n;
	}
	return lhs;
}

// comparison := addsub [ cmpOp addsub ]   (cmpOp optional — a bare boolean
// column / captured flag is a valid predicate, the lowering handles truthiness)
ibQueryAstExprPtr ParseComparison(Cur& c)
{
	ibQueryAstExprPtr lhs = ParseAddSub(c);
	if (!lhs) return nullptr;

	ibQueryCompareOp op;
	size_t opLen = 0;
	if      (c.DelimPair(wxT('<'), wxT('='))) { op = ibQueryCompareOp::Le; opLen = 2; }
	else if (c.DelimPair(wxT('>'), wxT('='))) { op = ibQueryCompareOp::Ge; opLen = 2; }
	else if (c.DelimPair(wxT('<'), wxT('>'))) { op = ibQueryCompareOp::Ne; opLen = 2; }
	else if (c.DelimPair(wxT('!'), wxT('='))) { op = ibQueryCompareOp::Ne; opLen = 2; }
	else if (c.DelimPair(wxT('='), wxT('='))) { op = ibQueryCompareOp::Eq; opLen = 2; }
	else if (c.IsDelim(wxT('<')))             { op = ibQueryCompareOp::Lt; opLen = 1; }
	else if (c.IsDelim(wxT('>')))             { op = ibQueryCompareOp::Gt; opLen = 1; }
	else if (c.IsDelim(wxT('=')))             { op = ibQueryCompareOp::Eq; opLen = 1; }
	else return lhs;   // no comparison — bare truthy operand

	c.pos += opLen;
	ibQueryAstExprPtr rhs = ParseAddSub(c);
	if (!rhs) return nullptr;
	auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Compare);
	n->m_cmp = op; n->m_lhs = lhs; n->m_rhs = rhs;
	return n;
}

ibQueryAstExprPtr ParseNot(Cur& c)
{
	if (c.IsKey(KEY_NOT)) {
		++c.pos;
		ibQueryAstExprPtr inner = ParseNot(c);
		if (!inner) return nullptr;
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Not);
		n->m_lhs = inner;
		return n;
	}
	return ParseComparison(c);
}

// Logical operators are the And/Or keywords ONLY (both code styles): the script
// lexer reserves bare '&' / '|' for other syntax ('|' = string continuation),
// so '&&' / '||' never reach the lexeme stream.
ibQueryAstExprPtr ParseAnd(Cur& c)
{
	ibQueryAstExprPtr lhs = ParseNot(c);
	while (lhs) {
		if (!c.IsKey(KEY_AND)) break;
		++c.pos;
		ibQueryAstExprPtr rhs = ParseNot(c);
		if (!rhs) return nullptr;
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		n->m_isOr = false; n->m_lhs = lhs; n->m_rhs = rhs;
		lhs = n;
	}
	return lhs;
}

ibQueryAstExprPtr ParseOr(Cur& c)
{
	ibQueryAstExprPtr lhs = ParseAnd(c);
	while (lhs) {
		if (!c.IsKey(KEY_OR)) break;
		++c.pos;
		ibQueryAstExprPtr rhs = ParseAnd(c);
		if (!rhs) return nullptr;
		auto n = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		n->m_isOr = true; n->m_lhs = lhs; n->m_rhs = rhs;
		lhs = n;
	}
	return lhs;
}

} // namespace

std::shared_ptr<ibQueryAstExpr> ibBuildLambdaQueryAst(
	const std::vector<ibLexem>& lexems, size_t from, size_t to,
	const wxString& rowParamName)
{
	if (rowParamName.IsEmpty() || from >= to || to > lexems.size())
		return nullptr;

	Cur c{ lexems, from, to, rowParamName };

	// body := [ '{' ] Return expr [ ';' ] [ '}' | EndFunction ]  — nothing else
	if (c.IsDelim(wxT('{'))) ++c.pos;
	if (!c.IsKey(KEY_RETURN)) return nullptr;
	++c.pos;

	ibQueryAstExprPtr expr = ParseOr(c);
	if (!expr) return nullptr;

	if (c.IsDelim(wxT(';')))       ++c.pos;
	if (c.IsDelim(wxT('}')))       ++c.pos;
	else if (c.IsKey(KEY_ENDFUNCTION)) ++c.pos;

	// any trailing lexeme = a second statement / unconsumed operator — bail
	if (!c.AtEnd()) return nullptr;

	return expr;
}
