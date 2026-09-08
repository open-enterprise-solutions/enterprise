#ifndef __LAMBDA_QUERY_AST_H__
#define __LAMBDA_QUERY_AST_H__

// L4-2 — the query tree of a pipeline predicate, READ OFF THE INSTRUCTIONS.
//
// The compiler is single-pass and keeps no syntax tree, and for LINQ it does not need one: the
// instructions ARE the structure. A `Where` is a branch, a column walk is a chain of member reads,
// an operator is an opcode whose typed delta even carries the operand type. So the tree the
// push-down lowers is DERIVED from the compiled body, by the same def-use walk — "who wrote this
// slot" — that answers the caret.
//
// ⭐⭐ THERE USED TO BE A SECOND READER HERE, over the LEXEMES, with a recursive-descent grammar of
// its own. Two readers of one language: every grammar change had to be made twice, and the second
// one late or not at all. It was retired by measurement (identical trees on 32 of 32 shapes, and
// the one divergence was IT accepting `x.Code in (…)`, which the compiler refuses) — a reader whose
// grammar is wider than the language can record predicates the program does not contain.
//
// ANYTHING outside the translatable subset answers null — the step then runs in RAM, which is
// always correct — and says WHAT it could not translate, with a position. The refusal matters:
// a null tree is the difference between a server-side query and a full scan, and it used to be
// invisible.
//
// See docs/query-language-arc.md §23.5 (L4-2) and docs/linq.md §0.
#include "translateCode.h"   // ibLexem

#include <functional>
#include <memory>
#include <vector>

struct ibQueryAstExpr;   // backend/query/queryAST.h — fwd only (the .cpp includes it)

// Record the body span [from, to) of a single-parameter lambda into the L4
// query AST. rowParamName = the lambda's first parameter (the row variable);
// its member chains become Column paths. Returns null when the body is
// outside the translatable subset (never throws on shape — shape is data).
// The LINQ lambda body `[ '{' ] Return expr [ ';' ] [ '}' | EndFunction ]` -> the L4 pushdown AST
// (single row parameter).
// ⭐⭐ AND WHEN IT REFUSES, IT SAYS WHAT IT COULD NOT TRANSLATE, with a position. A null tree decides
// that the pipeline step runs in RAM instead of in the database, and that decision used to be
// invisible: twenty-six ways to answer null, none of them named, so adding one condition to a
// predicate could silently turn a server-side query into a full table scan. The FIRST refusal wins —
// it is the innermost, i.e. the actual cause, not the rule that merely passed it up.
// ⭐⭐ …EXCEPT ONE FACT THE INSTRUCTIONS DO NOT CARRY YET: the NAME of a captured outer local.
// A body that reads `Limit` compiles to an operand saying `one frame out, cell 0` — the coordinates,
// which is all the runtime needs — and the enclosing function's symbol table is not in the bytecode
// at this moment, because that function is still being compiled (measured 2026-09-08: m_listFunc
// held the lambda ALONE and m_listVar was empty). Reading the cell number out of the lambda's own
// locals gave the ROW parameter's name, so `x.Article > Limit` recorded as `Article > &x` — a tree
// that exists, translates, and asks the wrong question.
//
// So the one thing only the compiler knows is asked OF the compiler: `nameOfOuter(framesOut, slot)`
// answers with the identifier that lives there, framesOut counting from 1 = the context the lambda
// was written in. Null callback = no captured names (the recorder then refuses such a body rather
// than guessing), which is what a caller outside the compiler gets.
//
// ⚠ It is a stopgap in the right direction, not the destination: the fold currently re-finds that
// value AT RUNTIME by walking the captured frames and comparing names case-insensitively
// (valueQueryable.cpp, ResolveCapturedByName) — a search for something the compiler had in its hand.
// Carrying the coordinates INTO the tree removes both the name and the search. (docs/linq.md §0.2e)
BACKEND_API std::shared_ptr<ibQueryAstExpr> ibBuildLambdaQueryAstFromCode(
	const struct ibByteCode& byteCode, long funcIndex, wxString* outRefusal = nullptr,
	const std::function<wxString(long framesOut, long slot)>& nameOfOuter = nullptr);

// ⭐⭐ THE SAME READ, OVER A PLAIN RANGE OF INSTRUCTIONS. A chain compiled as a loop has no lambda
// and no function record: its predicate is a stretch of ordinary instructions in the enclosing
// frame, and its answer is a SLOT rather than a `Return`. That is the only difference, so it is the
// only thing this takes differently — the row is named by its cell (`rowSlot`) and the result by
// its own (`resultSlot`).
//
// This is what lets a SOURCE decide server-side execution by reading the loop's own instructions:
// the tree is derived where it is needed and does not have to be stored, carried or versioned
// anywhere. `nameOfSlot(framesOut, slot)` names a cell of the frame — framesOut 0 is the frame the
// loop runs in — and may be null, in which case a body that reads a captured value is refused
// rather than guessed at.
BACKEND_API std::shared_ptr<ibQueryAstExpr> ibBuildQueryAstFromRange(
	const struct ibByteCode& byteCode, long from, long to,
	const struct ibParamRunUnit& rowSlot, const struct ibParamRunUnit& resultSlot,
	wxString* outRefusal = nullptr,
	const std::function<wxString(long, long)>& nameOfSlot = nullptr);



// ⭐⭐ WHAT THE PREDICATE ACTUALLY BECAME, in one line, canonically.
//
// It exists because "both recorders answered" is not the question. Presence was all that could be
// compared while there was no way to say a tree out loud, and two trees can both EXIST and mean
// different things — so a comparison of presence would have licensed deleting the older reader on
// evidence that never looked at the answer. Written parenthesised and in a fixed order so two
// renderings are equal exactly when the trees are.
//
// The second reader is diagnostics: a lambda that runs in RAM already says WHY in the journal, and
// one that pushes down can now say WHAT — which is the difference between reading a plan and
// guessing at one. An empty tree renders as `-`.
// `withAddresses` false leaves a capture's coordinate out — for comparing two readers, only one of
// which can know it. Comparing WITH it would report a disagreement on every captured lambda and
// bury the disagreements that mean something.
BACKEND_API wxString ibDescribeQueryAst(const std::shared_ptr<ibQueryAstExpr>& expr,
	bool withAddresses = true);

#endif
