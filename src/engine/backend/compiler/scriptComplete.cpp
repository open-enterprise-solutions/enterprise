#include "backend/compiler/scriptComplete.h"

#include "backend/backend_exception.h"
#include "backend/compiler/compileModule.h"      // ibCompileModule — the contextual compile
#include "backend/compiler/procUnitLambda.h"     // ibValueFunction, and through it procUnit.h —
                                                 // ibLinqRow / ibLinqNamedColumns, the query's own rules
#include "backend/system/value/valueTable.h"     // ibValueModelTable — and with a table when they are named
#include "backend/metaData.h"                    // ibCompileValueCache::FindCompileModule
#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject::GetMetaData
#include "backend/moduleInfo.h"                  // ibRuntimeModuleDataObject::GetCompileModule
#include "backend/moduleManager/moduleManager.h" // ibValueModuleManager — the context a snippet parents to
#include "backend/session/session.h"             // ibSession::EditModuleManagerFor
#include "backend/diagnostics/journal.h"         // where the refusals are read out when nothing resolved
#include "backend/typeDescription.h"             // ibTypeDescription - a type, possibly several
#include "backend/objCtor.h"                     // ibCtorMetaValueType - one door: the metaobject AND the maker
#include "backend/metaCollection/metaObjectComposite.h"   // where fields live: catalog, document, register, tabular section

#include <limits>   // the "no closing position" sentinel is named, not spelled as a cast
#include <memory>
#include <set>       // one name, one entry — the ladder is walked nearest first
#include <algorithm> // the refusal dump is bounded

namespace {

// THE LADDER, NEAREST FIRST — the compile modules a name can be resolved against, which is the
// same chain the compiler walked when it bound the name and the same one the runtime lays out as
// m_ppArrayContext. [0] is the caret's own compile; then the module the text lives in, then its
// parent, up to the configuration root.
using ibCompileChain = std::vector<const ibCompileCode*>;

// THE FRAME AN OPERAND NAMES, AS THE RUNTIME READS IT (procUnit.cpp, ResolveRead/ResolveWrite —
// the authority, because it is what actually runs). Everything at or below zero is THIS frame: a
// temp carries DEF_VAR_TEMP rather than a depth, and a variable reached through a block scope
// carries it too (compileContext.cpp: `blockReturn ? DEF_VAR_TEMP : depth`), yet all three land on
// the same cell. Reading the marker literally would make one cell look like three.
wxLongLong_t FrameOf(const ibParamRunUnit& p)
{
	return p.m_numArray <= 0 ? 0 : p.m_numArray;
}

bool SameSlot(const ibParamRunUnit& a, const ibParamRunUnit& b)
{
	return FrameOf(a) == FrameOf(b) && a.m_numIndex == b.m_numIndex;
}

// ⭐⭐ THE TYPES A FIELD IS DECLARED WITH — the one lookup behind both the walk's widening and the
// type printed beside each name in a listing (see ibFieldTypesOf in the header for why it is asked
// here rather than of the value).
bool FieldTypesOf(const ibMetaData* metaData, const ibValue& owner, const wxString& field,
	std::vector<ibFieldType>& outTypes)
{
	if (metaData == nullptr)
		return false;

	// ⭐⭐ ONE DOOR, ASKED BY CLASS. `GetTypeCtor(clsid)` is the metadata's own answer to "what is
	// this type" and it carries BOTH halves of the composite question: the METAOBJECT that declares
	// the fields, and the maker that produces a value of the type. So neither half needs the value
	// narrowed to a class until one fits — and it works the same for a reference, an object and a
	// selection row, which is what a composite path actually walks through (most often a reference:
	// `…EmptyRef().Owner.` has no object anywhere in it).
	const ibCtorMetaValueType* ownerType = metaData->GetTypeCtor(owner.GetClassType());

	// ⭐ AT THE LEVEL WHERE FIELDS EXIST, and that level is COMPOSITE data — everything with
	// attributes of its own is one: a catalog, a document, a REGISTER, a TABULAR SECTION. Naming the
	// record object instead answers for one of them and silently skips the rest, and asking the bare
	// metaobject for a child by name answers with too much — that search descends, so a field of a
	// nested table can come back in place of this object's own.
	const ibValueMetaObjectCompositeData* meta = ownerType != nullptr
		? dynamic_cast<const ibValueMetaObjectCompositeData*>(ownerType->GetMetaObject())
		: nullptr;
	if (meta == nullptr)
		return false;

	for (const ibValueMetaObjectAttributeBase* attribute : meta->GetGenericAttributeArrayObject()) {
		if (attribute == nullptr || !stringUtils::CompareString(attribute->GetName(), field))
			continue;

		for (const ibClassID& clsid : attribute->GetTypeValueDesc().m_listTypeClass) {

			// TWO REGISTRIES, ASKED IN THE ORDER THAT MATTERS. A configuration type
			// (CatalogRef.X, DocumentObject.Y) is the METADATA's and the platform has never heard
			// of it; a String or a Date is the platform's. Asking the platform first is what
			// `ibValue::GetNameObjectFromID` does, and it does not answer "no" — it throws.
			const ibCtorAbstractType* named = metaData->GetTypeCtor(clsid);
			if (named == nullptr)
				named = ibValue::GetAvailableCtor(clsid);

			ibFieldType type;
			type.m_class = clsid;
			if (named != nullptr)
				type.m_name = named->GetClassName();
			outTypes.push_back(type);
		}

		return !outTypes.empty();
	}

	return false;
}

// What an instruction turned out to be, to whoever is looking for the producer of a slot. Three
// answers rather than two, because "I do not claim this opcode" and "this step had nothing to give"
// mean opposite things to the search: keep looking, versus stop.
enum class ibStep {
	NotAStep,   // structure, not a resolution step — a block marker, a jump, an argument marker
	Failed,     // a step, and it did not resolve
	Value,      // a step, and `out` now holds what it produced
};

// The walk itself — see the header. Recursive over the OPERANDS, not over the text: an instruction
// names the slot it read from, and that slot was written by an earlier instruction, so the chain
// unwinds itself. Depth is the length of the dotted path, not of the module.
class ibCaretWalk {
public:
	ibCaretWalk(const ibCompileChain& chain, const ibByteCode::ibByteFunction* frame,
		long lastCode, const ibMetaData* metaData = nullptr, long firstCode = 0)
		// In DECLARATION order — GCC and Clang warn on any other, and their warning log is an order
		// of magnitude longer than MSVC's, so a reorder here is invisible locally and noisy in CI.
		: m_chain(chain), m_byteCode(chain.front()->m_cByteCode),
		  m_frame(frame), m_lastCode(lastCode), m_metaData(metaData), m_firstCode(firstCode) {}

	// ⭐⭐ THE CARET AFTER A DOT IS AN INSTRUCTION THE COMPILER WROTE FOR IT — not "the last one", and
	// not "the last one minus a few". A tolerant compile emits the attribute step with an EMPTY name
	// when the text ends at the dot (compileCode.cpp, GetCurrentIdentifier), so the caret has a
	// bytecode of its own, and its RECEIVER is the whole answer. Nothing else needs looking at.
	//
	// ⚠ THIS REPLACES A WINDOW, and the window was the shape of four different defects. It stepped
	// back a fixed number of instructions because the caret's own instruction is often something
	// else — the operator that was waiting for a right-hand side (`… And row.`), the assignment
	// wrapping a bare expression statement, the jump closing a loop. Stepping back reached the right
	// answer for those and DRIFTED for others, because a step back is also a step away: measured
	// 2026-09-07, `i.GetTemplate().` answered with the members of `i` — the receiver of a call that
	// had refused, which is a different expression wearing the right answer's clothes.
	//
	// Asking for the caret's own step removes the choice: the operator above it, the marker beside
	// it and the jump after it are not consulted at all, so none of them can be mistaken for it.
	//
	// ⭐⭐ AND IT ANSWERS WITH A LIST — see the header. The receiver may be DECLARED as several
	// things, and picking one of them here would be picking for the reader.
	bool ValueAt(long ip, std::vector<ibCaretValue>& out) const
	{
		for (long at = ip; at >= m_firstCode; --at) {

			const ibByteUnit& code = m_byteCode.m_listCode[(size_t)at];

			if ((code.m_numOper % TYPE_DELTA1) != OPER_GET_A
				&& (code.m_numOper % TYPE_DELTA1) != OPER_GET_SCOPE)
				continue;

			// A NAMED member step belongs to the expression, not to the caret — `Catalogs.Goods.`
			// emits one for `Goods` and then the empty one for the dot being typed.
			if (!ConstName(code.m_param3.m_numIndex).IsEmpty())
				continue;

			// From HERE, not from wherever the caret's position was matched — see ValuesOfSlot.
			return ValuesOfSlot(code.m_param2, out, 0, at);
		}

		return false;
	}

	// What the walk did, step by step — see StepOf.
	const wxString& Trace() const { return m_trace; }

	// The ONE value a step wants when it wants one — the first of what the slot resolved to. Only a
	// member access can branch, so every other step is asking a question it has no way to answer
	// differently.
	//
	// 🛑 MOVED, NEVER COPIED. A copy of an ibValue that HOLDS an object points AT THE SOURCE and
	// takes a reference on it (value.cpp, Copy: TYPE_VALUE becomes TYPE_REFFER at &cOld) — and the
	// source here is a vector about to be destroyed.
	// ⭐ AND THE ORIGIN COMES WITH IT WHEN THE CALLER IS PASSING THE VALUE ON. Most callers here want
	// an INTERMEDIATE — the parent of a member read, the receiver of a call, an argument — and how
	// that intermediate was reached says nothing about the answer. Two of them are different: the
	// step that simply forwards a slot (a read of an extern, a let, a constant) and the one that
	// takes a callee's return. For those the value IS the answer, so dropping the origin here left
	// the whole road reading `platform` no matter what the step below had found (measured
	// 2026-09-08: the context map said `ThisObject` and the wire said `platform`).
	// ⭐⭐ `from` IS WHERE THE PRODUCER IS LOOKED FOR, AND A STEP SHOULD PASS ITS OWN INSTRUCTION.
	// An operand is written before the instruction that reads it — always — so scanning back from
	// the instruction finds it. Scanning back from the CARET finds it only while the caret happens
	// to sit below, which is true of ordinary code and false of a query: its clauses are emitted in
	// the order the compiler needs, not the order they are written, so a join's source `OPER_LET`
	// lands PAST the caret that reads its alias. Measured 2026-09-08, twice in one trace: first the
	// loop header could not resolve its source, then the LET under it could not resolve its own.
	bool ValueOfSlot(const ibParamRunUnit& slot, ibValue& out, int depth = 0,
		ibNameOrigin* outOrigin = nullptr, long from = -1) const
	{
		std::vector<ibCaretValue> values;
		const bool answered = ValuesOfSlot(slot, values, depth, from);

		// ⚠ TRUE WITH NOTHING IN IT IS A THIRD OUTCOME, and it reads exactly like the second. A step
		// may report success and leave the vector empty — a branch that widened into no branches, a
		// member that resolved to nothing — and the caller then says "did not resolve" about a slot
		// that DID. Named apart because a trace that cannot tell them apart sends the next hour to
		// the wrong place.
		if (answered && values.empty())
			m_trace << wxString::Format(wxT("  [(%d,%d) answered with nothing]"),
				(int)slot.m_numArray, (int)slot.m_numIndex);

		if (!answered || values.empty())
			return false;
		if (outOrigin != nullptr)
			*outOrigin = values.front().m_origin;
		out = std::move(values.front().m_value);
		return true;
	}

	// ⭐⭐ EVERYTHING `slot` RESOLVED TO. One entry is the ordinary case; several mean a composite
	// declaration was crossed on the way here, and the set travels onward rather than being
	// collapsed at the step that made it.
	// `from` is where the search for a producer BEGINS, and it matters as soon as a lambda is in the
	// text. The caret's own step is found by ValueAt, and that step can sit INSIDE a lambda body
	// while the instruction its POSITION matched sits after the body closed. Starting the scan at
	// the latter walks out of the caret's frame through the lambda's ENDLFUNC/LFUNC pair and answers
	// with whatever holds the same slot NUMBER in the enclosing procedure — measured 2026-09-08:
	// `a.Where(Function(row) { return row.` answered about `a`, because `row` is the lambda's slot 0
	// and `a` is the procedure's. Resolving from where the STEP is keeps the guard inside the right
	// body. -1 means "from the end", which is what every caller but ValueAt wants.
	bool ValuesOfSlot(const ibParamRunUnit& slot, std::vector<ibCaretValue>& out, int depth = 0,
		long from = -1) const
	{
		if (depth > kMaxChain)
			return false;   // a chain this long is not a dotted path; refuse rather than recurse

		// A CONSTANT IS ITS OWN ANSWER — no producer to look for.
		if (slot.m_numArray == DEF_VAR_CONST) {
			if (slot.m_numIndex < 0 || (size_t)slot.m_numIndex >= m_byteCode.m_listConst.size())
				return false;
			out.emplace_back();
			out.back().m_value = m_byteCode.m_listConst[(size_t)slot.m_numIndex];
			out.back().m_origin = ibNameOrigin::Declared;   // written in this text, right here
			return true;
		}

		// 🛑 A PRODUCER IN ANOTHER FRAME IS NOT A PRODUCER, and without saying so this search reads
		// the whole tape as if it were one. Slot numbers are per FRAME and `FrameOf` deliberately
		// folds a temp onto "this frame" (a temp carries DEF_VAR_TEMP, not a depth) — so temp #5
		// inside a procedure and cell #5 of the module are the same key here, and the scan happily
		// took one for the other.
		//
		// It is why the fault GREW WITH THE TEXT: every statement in a body above the caret mints
		// another temp, and the collision arrives as soon as their numbering reaches the module cell
		// being asked about. Measured 2026-09-07 on a text of Max's — three `Message` lines above a
		// module-level `AccountType.` were enough, while one or two were not, and the trace named it
		// in one run: the container slot resolved against an instruction inside the procedure.
		//
		// Scanning BACKWARDS, a closer is where a foreign body begins and its opener is where it
		// ends; meeting an opener at depth zero is the start of the caret's OWN frame, and there is
		// nothing above it that belongs to this one.
		long foreign = 0;
		bool producerRefused = false;   // see the LINQ row below — the one case with a second road

		for (long ip = (from >= 0 ? from : m_lastCode); ip >= m_firstCode; --ip) {

			const ibByteUnit& code = m_byteCode.m_listCode[(size_t)ip];
			const short oper = code.m_numOper % TYPE_DELTA1;

			if (oper == OPER_ENDFUNC || oper == OPER_ENDLFUNC) { foreign++; continue; }
			if (oper == OPER_FUNC || oper == OPER_LFUNC) {

				// ⭐⭐ A LAMBDA'S OPENER IS TWO THINGS AT ONCE, and reading it as one of them was
				// why the compiler could not type `a.Where(x => …)`. OPER_LFUNC opens a body — the
				// guard above and below counts it as such — AND it PRODUCES a value: the runtime
				// materialises an ibValueFunction into m_param1 there (procUnit.cpp). Counted only
				// as a marker, the lambda's slot had no producer at all, so the argument arrived
				// empty and the pipeline verb refused: "Argument must be a Function or Procedure".
				//
				// Depth 1 is exactly "a lambda written inside the body the caret is in": scanning
				// backwards its ENDLFUNC was met first (foreign 0 → 1) and its own body skipped, so
				// its opener arrives with one level still open. Deeper is somebody else's lambda.
				if (oper == OPER_LFUNC && foreign == 1 && SameSlot(code.m_param1, slot)) {
					const long funcIdx = (long)code.m_param3.m_numIndex;
					if (funcIdx >= 0 && funcIdx < (long)m_byteCode.m_listFunc.size()) {
						// No captured frames: nothing here is ever CALLED. The verb stores the
						// function in an iterator state and answers its shape from upstream, and
						// what a lowering would want off it — the recorded query AST — travels on
						// the bytecode entry this value names.
						out.emplace_back(new ibValueFunction(&m_byteCode, funcIdx),
							ibNameOrigin::Declared);
						m_trace << wxString::Format(wxT("\n  %*sop %3d at %-5u -> lambda #%ld"),
							depth * 2, wxT(""), (int)oper, code.m_numString, funcIdx);
						return true;
					}
				}

				if (foreign == 0)
					break;              // the top of the body the caret is in
				foreign--; continue;
			}
			if (foreign > 0)
				continue;               // inside a declaration that is not the caret's

			if (!SameSlot(code.m_param1, slot))
				continue;

			// 🛑 AN INSTRUCTION THAT PRODUCES NOTHING IS NOT A PRODUCER — and several of them carry
			// all-zero operands, which read as slot 0, a perfectly ordinary slot. A block's opening
			// marker thereby claimed the variable declared just before it, and an argument marker
			// claims the argument it merely names. Matching by SLOT alone is not enough; the opcode
			// has to say it is a step, and the scan goes on past anything that is not one.
			//
			// Measured 2026-09-07 on this configuration's own modules: with the slot-only rule, 19%
			// of carets in real code answered. `a = New Array(); if (true) { a.` was enough to see
			// it — `OPER_CTX_BEGIN` sat between the assignment and the caret with p1 = (0,0).
			const ibStep step = StepValues(code, ip, out, depth);
			if (step == ibStep::NotAStep)
				continue;
			if (step == ibStep::Value) {
				// ⭐⭐ AND WHATEVER WAS DONE TO THIS CELL IS DONE NOW. A collection is built empty
				// and filled by calls that produce nothing — `people.Add(...)` — so a walk that only
				// resolves producers sees an empty Array and can say nothing about its rows. The
				// calls stand on THIS slot on the tape; playing them here, where the cell has just
				// been given its value, is what the old precompiler did by evaluating as it read.
				//
				// ⚠ ON THE CELL THAT WAS FILLED, NOT THE ONE THAT IS READ. A loop reads a COPY of
				// its source (`@in_` gets an OPER_LET), and the fills stand on the original — so
				// doing this at the loop header found nothing. Here every cell replays its own.
				for (ibCaretValue& answer : out)
					ReplayCallsOn(slot, answer.m_value, depth);
				return true;
			}

			// ⭐⭐ A PRODUCER THAT COULD NOT ANSWER IS NOT ALWAYS THE ONLY PRODUCER. A join writes
			// its alias TWICE: once from the loop that builds the hash, and again from the loop
			// over the matched bucket — and the bucket's source arrives through a step this walk
			// does not know, so the nearer of the two refuses. Reading that as the final answer
			// left `join b in … where b.` offering nothing while the tree answered with all ten
			// fields (measured 2026-09-08, with the tape in view).
			//
			// So a query's own row gets its second chance below, at the header the compiler
			// recorded for it. Nothing else does: for ordinary code a producer that cannot answer
			// IS the answer, and falling through to the name lookup would let a slot be described
			// by whoever else happens to be called that.
			producerRefused = true;
			break;
		}

		// ⭐ NOTHING WROTE IT, AND THAT IS ITSELF THE ANSWER: code always writes a variable before
		// reading it, so a slot with no producer is not a variable the module computed — it is a
		// handle the module was GIVEN (Manager, ThisObject, a common module under its own name).
		// Being given is exactly what kind=External/Context records, so the walk asks for the name
		// under that rule rather than against a list of names that would need keeping in step.
		//
		// This is the one lookup by NAME in the whole walk — the START of a chain, the same single
		// table the 2026-08 attempt arrived at ("one table, one lookup"). Everything after it is
		// asked of a VALUE.
		// ⭐⭐ …EXCEPT INSIDE A LAMBDA, WHERE ONE SLOT IS FILLED BY THE VERB THAT TAKES IT. A pipeline
		// lambda's first parameter is the ROW, and nothing in its body writes it — so by the rule
		// above it would be looked up as a bound name, find nothing, and the caret inside
		// `t.Where(Function(row) { return row.` would answer about the TABLE instead of the row.
		// That is the whole reason writing a predicate got no help from the editor.
		//
		// It is not a special case bolted on: parameter i lives at frame slot i (compileCode.cpp
		// stamps `m_param1.m_numIndex = slot (= i)`), so slot 0 of a lambda IS its row, and what the
		// row is comes from the source the verb reads — asked with the same CreateIterator /
		// PeekSample pair `foreach` uses. Nothing is executed: a sample is a type, not a row.
		// ⭐⭐ …AND A QUERY'S ROW COMES OUT OF ITS OWN HEADER, wherever the caret happens to stand.
		// The scan above walks BACKWARDS from the caret and that is right for ordinary code, where
		// a value is written before it is read. A query is not written in that order: its clauses
		// are read by moving the cursor, a join's inner source is replayed into instructions that
		// sit far from the text they came from, and the header that produces the row can end up
		// behind the scan's own frame guard. Measured 2026-09-08: `join b in … where b.` refused,
		// while the SAME row asked through the tree answered with its ten fields — the difference
		// being only where the walk started.
		//
		// The compiler wrote the answer down: `foreachStartIp` is the header that produces this
		// cell (byteCodeLINQ.h), and starting there is what the tree already does for its offers.
		// One rule, both readers.
		//
		// ⚠ LAST, not first: an ordinary producer found by the scan is the caret's own frame
		// talking, and it wins. This is the road for the row nobody else could reach.
		for (const ibLinqQuery& query : m_chain.front()->m_cByteCode.m_listLinq) {
			for (const ibLinqBinding& binding : query.m_bindings) {

				if (binding.foreachStartIp < 0
					|| (size_t)binding.foreachStartIp >= m_byteCode.m_listCode.size())
					continue;
				if (!SameSlot(binding.valueSlot, slot))
					continue;

				const ibByteUnit& header = m_byteCode.m_listCode[(size_t)binding.foreachStartIp];
				if (StepValues(header, (long)binding.foreachStartIp, out, depth) == ibStep::Value)
					return true;
			}
		}

		// A producer was found and it refused. That IS the answer for ordinary code — reading on
		// would describe this cell by whatever else carries its name.
		if (producerRefused)
			return false;

		// ⭐⭐ …EXCEPT INSIDE A LAMBDA, WHERE ONE SLOT IS FILLED BY THE VERB THAT TAKES IT — see the
		// note above; the row a pipeline hands its predicate is written by nobody in the body.
		if (m_frame != nullptr && !m_frame->m_listParam.empty()
			&& FrameOf(slot) == 0 && slot.m_numIndex == 0
			&& ElementOfLambdaSource(out, depth))
			return true;

		out.emplace_back();
		if (!BoundValue(NameOfSlot(slot), out.back().m_value, &out.back().m_origin)) {
			out.pop_back();
			return false;
		}
		return true;
	}

private:

	static const int kMaxChain = 64;

	// ONE INSTRUCTION = ONE RESOLUTION STEP. This is the whole reason the walk reads instructions
	// rather than text: the compiler has already said what step this is and on what.
	//
	// Three answers, not two, because "I do not claim this opcode" and "this step did not resolve"
	// are different facts to the scan above: the first means keep looking for the producer, the
	// second means the producer was found and had nothing to give.
	// ⭐⭐ EVERY STEP SAYS WHAT IT DID. The instruction window shows where the caret LANDED; this
	// shows what happened after — which is the half that four blind fixes in one day were missing.
	// It is the arc's third requirement said in code: a mechanism that does not have to be guessed
	// at (Max, 2026-09-07: *"maximally friendly, so there is less guessing"*).
	// ⭐⭐ THE ONLY STEP THAT CAN BRANCH IS THE ONE THAT READS A MEMBER, so it is the only one written
	// against a SET; every other opcode produces exactly one thing and is left as it was.
	//
	// A composite path both WIDENS and NARROWS, and both halves are the language's own rule rather
	// than a policy invented here (Max, 2026-09-08: *"value(composite).field1(both have it)
	// .field2(also).field3(only one, so the other is gone)"*):
	//
	//   • WIDENS at the field whose DECLARATION names several types — one answer per type, because
	//     each is a different thing to walk into and picking one would be picking for the reader;
	//   • NARROWS at every field after it — a branch that has not got the field simply drops out,
	//     which is what makes the last step of a long path unambiguous again.
	//
	// So the set is carried, not collapsed: `Owner.Region.Code` starts as two and can end as one,
	// and at each dot the reader is offered exactly what the surviving branches share plus what only
	// one of them has.
	//
	// 🛑 EVERY ANSWER IS BUILT WHERE IT WILL LIVE, never carried there through a local: pushing a
	// stack value into a vector leaves the element pointing at a stack slot (value.cpp, Copy).
	ibStep StepValues(const ibByteUnit& code, long ip, std::vector<ibCaretValue>& out, int depth) const
	{
		const short oper = code.m_numOper % TYPE_DELTA1;

		if (oper != OPER_GET_A && oper != OPER_GET_SCOPE) {
			out.emplace_back();
			const ibStep step = StepOf(code, ip, out.back().m_value, depth, &out.back().m_origin);
			if (step != ibStep::Value)
				out.pop_back();
			return step;
		}

		// ⚠ FROM THIS INSTRUCTION, like every other operand — see ValueOfSlot. This branch kept the
		// old default (search back from the caret) after the single-value road was corrected, and
		// that split was invisible: an attribute step resolved its receiver by one rule and every
		// other step by another. It showed up as an instruction two places above being "not found"
		// while it sat plainly on the tape — because the scan had started twenty places earlier.
		std::vector<ibCaretValue> parents;
		if (!ValuesOfSlot(code.m_param2, parents, depth + 1, ip)) {
			m_trace << wxString::Format(wxT("  [receiver (%d,%d) did not resolve]"),
				(int)code.m_param2.m_numArray, (int)code.m_param2.m_numIndex);
			return ibStep::Failed;
		}

		// An empty name is the caret's own dot — every branch is the answer, origin and all.
		const wxString name = ConstName(code.m_param3.m_numIndex);
		if (name.IsEmpty()) {
			out = std::move(parents);
			return ibStep::Value;
		}

		for (ibCaretValue& parent : parents) {

			// ⭐⭐ THE STEP IS WHERE THE ORIGIN IS KNOWN, so it is said here and nowhere else. Asking
			// the value afterwards cannot recover it: a reference reached as a document's own
			// attribute and the same reference reached as a bind describe themselves identically.
			// The metadata is asked once and answers both questions at once — whether to widen, and
			// whose the name is.
			std::vector<ibFieldType> types;
			const bool declared = m_metaData != nullptr
				&& FieldTypesOf(m_metaData, parent.m_value, name, types);

			// The widening is only taken when it actually produced branches. A declared type whose
			// maker the metadata cannot build gives none, and silently dropping the name there
			// would be worse than answering it the ordinary way.
			if (declared && types.size() > 1 && Composite(types, out))
				continue;

			out.emplace_back();
			if (!MemberOf(parent.m_value, name, out.back().m_value)) {
				out.pop_back();
				continue;
			}

			// Declared by the metadata — the object's own data. Otherwise it came off the value's
			// own surface, and the surface holds two different kinds of thing: the platform's own
			// members and whatever the module staged there.
			out.back().m_origin = declared
				? ibNameOrigin::Member
				: OriginOfStaged(out.back().m_value);
		}

		m_trace << wxString::Format(wxT("\n  %*sop %3d at %-5u `%s` -> %u of %u branch(es)"),
			depth * 2, wxT(""), (int)oper, code.m_numString, name,
			(unsigned)out.size(), (unsigned)parents.size());

		return out.empty() ? ibStep::Failed : ibStep::Value;
	}

	// ⭐⭐ ONE BRANCH PER DECLARED TYPE, made by the METADATA'S OWN REGISTRY.
	// `ibValue::GetAvailableCtor` does not know reference types at all (Max: *"your references will
	// not be found"*), so widening through it would have been silently dead — no error, just no
	// branches ever. The types themselves were asked of the metadata by the step above, which is
	// also where a single declared type stops being a branch: the ordinary member road answers that
	// one, and answers it with the value the object actually holds rather than a fresh empty one.
	bool Composite(const std::vector<ibFieldType>& types, std::vector<ibCaretValue>& out) const
	{
		bool any = false;
		for (const ibFieldType& type : types) {

			// The same door as the one that named the type, now as the MAKER.
			ibCtorMetaValueType* const ctor = m_metaData->GetTypeCtor(type.m_class);
			if (ctor == nullptr)
				continue;

			// The wrapper IS the owner: ibValue(ibValue*) takes a reference on what it is handed,
			// and the branch is constructed IN the vector so nothing is ever copied out of a local.
			if (ibValue* const created = ctor->CreateObject()) {
				out.emplace_back(created, ibNameOrigin::Member);   // the metadata declared this field
				any = true;
			}
		}

		return any;
	}

	ibStep StepOf(const ibByteUnit& code, long ip, ibValue& out, int depth,
		ibNameOrigin* outOrigin = nullptr) const
	{
		const ibStep step = DoStepOf(code, ip, out, depth, outOrigin);

		m_trace << wxString::Format(wxT("\n  %*sop %3d at %-5u %s"),
			depth * 2, wxT(""), (int)(code.m_numOper % TYPE_DELTA1), code.m_numString,
			step == ibStep::Value ? wxT("-> value") : step == ibStep::Failed ? wxT("-> FAILED") : wxT("(not a step)"));

		return step;
	}

	ibStep DoStepOf(const ibByteUnit& code, long ip, ibValue& out, int depth,
		ibNameOrigin* outOrigin) const
	{
		// ⭐ A TYPED VARIANT IS THE SAME STEP. The compiler stamps a numeric / string / date /
		// boolean instruction as `OPER_X + TYPE_DELTAn` (codeDef.h), so a switch on the raw number
		// misses every typed one — `if (true)` arrives as 313, which is `OPER_IF` plus the boolean
		// delta and reads as an opcode nobody has heard of. The modulo is what codeDef.h's own note
		// about load-bearing parentheses exists for.
		switch (code.m_numOper % TYPE_DELTA1) {

		// The chain's start when it is a member of a scope binding (`Catalogs` off Manager), and an
		// ordinary attribute step — the same shape, and the runtime treats them as one case too.
		case OPER_GET_SCOPE:
		case OPER_GET_A: {
			ibValue parent;
			if (!ValueOfSlot(code.m_param2, parent, depth + 1, nullptr, ip)) {
				// The receiver is the whole of an attribute step, so naming it names the failure.
				m_trace << wxString::Format(wxT("  [receiver (%d,%d) did not resolve]"),
					(int)code.m_param2.m_numArray, (int)code.m_param2.m_numIndex);
				return ibStep::Failed;
			}

			// ⭐ AN EMPTY NAME MEANS THE CARET IS ON THE DOT, and then the PARENT is the answer —
			// there is nothing to take off it yet, which is exactly what is being asked. The
			// tolerant compile emits the step this way when the text ends there
			// (compileCode.cpp, the attribute step), so the walk needs no notion of a caret: it
			// reads what the compiler wrote down.
			const wxString name = ConstName(code.m_param3.m_numIndex);
			if (name.IsEmpty()) {
				out = parent;
				return ibStep::Value;
			}

			return MemberOf(parent, name, out) ? ibStep::Value : ibStep::Failed;
		}

		// `New X(…)` — the class is named by a CONSTANT, so constructing it runs no user code, which
		// is the same line the header draws for calls. Arguments are not passed, for the same reason
		// they are not passed there.
		case OPER_NEW:
			return NewObject(code, out, ip, depth) ? ibStep::Value : ibStep::Failed;

		// ⭐⭐ THE LOOP VARIABLE IS A SAMPLE OF WHAT THE COLLECTION YIELDS, and the language already
		// knows how to be asked for one: `CreateIterator()` → `PeekSample()`. That is the very pair
		// the precompiler used for exactly this (its CompileForeach, since deleted) — the
		// answer was in the tree, and asking the collection is what makes a foreach over a query
		// selection offer the selection's own columns instead of nothing.
		// ⭐⭐ AN ELEMENT TAKEN BY INDEX IS A SAMPLE OF WHAT THE COLLECTION YIELDS — the same
		// question `foreach` asks, so it is asked the same way. `q[0]` was not a step at all here,
		// and a step the walk does not know is not a refusal: the search for the slot's producer
		// simply carried on past it and answered with whatever ELSE had written that cell — the
		// global context, offered confidently. Measured 2026-09-08: `a[0].` and `t[0].` both listed
		// `CommonModules, CommonForms, Constants, Catalogs`, while the same row reached through a
		// `foreach` answered with its own columns. Answering wrong is worse than answering nothing,
		// which is why an unknown opcode must not be able to leave a plausible trail.
		//
		// The INDEX is not read: a sample says what the elements ARE, and that is what a reader
		// standing after the dot is asking. An associative container answers the same way.
		case OPER_GET_ARRAY: {
			// From the instruction, for the same reason the loop below does — see there.
			std::vector<ibCaretValue> holders;
			if (!ValuesOfSlot(code.m_param2, holders, depth + 1, ip) || holders.empty())
				return ibStep::Failed;
			ibValue collection = holders.front().m_value;

			// The fills were already played when the slot was resolved, on the cell they stand on —
			// see ValuesOfSlot. Nothing to do here but ask what an element looks like.
			const std::shared_ptr<ibValueIteratorState> state = collection.CreateIterator();
			if (!state)
				return ibStep::Failed;

			try { return state->PeekSample(out) ? ibStep::Value : ibStep::Failed; }
			catch (...) { return ibStep::Failed; }
		}

		case OPER_FOREACH: {
			// ⭐ A LOOP CAN FAIL IN THREE PLACES AND THE TRACE USED TO SAY ONLY "FAILED". Which of
			// them it was decides where to look next — the source that would not resolve, a value
			// that yields nothing, or a collection whose element type is not knowable — and each
			// sends the reading somewhere else entirely. Named here because this is the step every
			// query row comes through.
			// ⚠ THE SEARCH STARTS AT THE HEADER, NOT AT THE CARET. A loop's source is written just
			// before the loop, so scanning back from the header always finds it — while scanning
			// back from the caret finds it only when the caret happens to be BELOW. A query breaks
			// that assumption on purpose: a join's build loop is emitted after the clauses that
			// read its alias, so its `OPER_LET` sits past the caret and the source "did not
			// resolve" (measured 2026-09-08 — the trace said exactly that, once it was asked to).
			std::vector<ibCaretValue> sources;
			if (!ValuesOfSlot(code.m_param2, sources, depth + 1, ip) || sources.empty()) {
				m_trace << wxT("  [foreach: the source slot did not resolve]");
				return ibStep::Failed;
			}
			ibValue collection = sources.front().m_value;

			// ⭐⭐ A COLLECTION BUILT IN CODE IS EMPTY UNTIL THE CODE RUNS — and by here it has been
			// run: the calls standing on the source cell were played when that cell was resolved
			// (ValuesOfSlot). `New Array` alone answers nothing about its rows; `people.Add(New
			// Structure("Country, Name", …))` is what says what a row IS, and it is an instruction
			// nobody used to execute. Max, on the old precompiler: *"the interesting part is that
			// the old precompiler could do this"* — it could, because it EVALUATED as it read
			// instead of emitting. Measured 2026-09-08 over the project's own LINQ suite: 66 of 197
			// carets refused, every one of them a collection filled in code.
			//
			// ⚠ AND A FILLED COLLECTION STILL NEED NOT HAVE AN ELEMENT TYPE. An Array is
			// heterogeneous by construction — `Add` may put a structure, a number and a reference
			// into the same array — so "no sample" here is frequently the correct answer rather
			// than a refusal to look (Max, 2026-09-09: an array may hold several values with
			// different columns that do not agree, and there is nothing single to offer).
			const std::shared_ptr<ibValueIteratorState> state = collection.CreateIterator();
			if (!state) {
				m_trace << wxT("  [foreach: the source is not iterable]");
				return ibStep::Failed;
			}

			try {
				if (state->PeekSample(out))
					return ibStep::Value;
				m_trace << wxT("  [foreach: the source yields no sample]");
			}
			catch (...) { m_trace << wxT("  [foreach: asking for a sample raised]"); }

			return ibStep::Failed;
		}

		// A bound handle read into a temp, and a plain copy — the value is whatever was on the
		// right. `OPER_SET` / `OPER_SETREF` / `OPER_SETCONST` are deliberately NOT here: they are
		// ARGUMENT markers, whose m_param1 names a slot they pass rather than one they write
		// (procUnit.cpp reads them positionally after a call). Treating them as producers made an
		// argument claim the variable it was carrying.
		// These forward a slot rather than transform it, so whatever the slot turned out to be — and
		// HOW it was reached — is what this step produced.
		case OPER_LET:
		case OPER_GET_EXTERN:
		case OPER_GET_CONTEXT:
		case OPER_CONST:
			if (ValueOfSlot(code.m_param2, out, depth + 1, outOrigin, ip))
				return ibStep::Value;
			// Which slot it was — a forward that fails says nothing about WHAT it was forwarding,
			// and that operand is the next question every time.
			//
			// ⚠ "did not resolve" covers BOTH outcomes and must not claim either: the producer may
			// have been missing, or found and unable to answer. Saying "found nothing" cost a round
			// here — the trace was read as proof the instruction two places up was invisible, when
			// it had been reached and had refused.
			m_trace << wxString::Format(wxT("  [(%d,%d) did not resolve, searched from %ld down to %ld]"),
				(int)code.m_param2.m_numArray, (int)code.m_param2.m_numIndex, ip, m_firstCode);
			return ibStep::Failed;

		// ⭐⭐ A NUMBER THE INSTRUCTION CARRIES ITSELF. `OPER_CONSTN` holds its value in the operand
		// rather than in the constant pool, so there is no slot to forward and nothing answered —
		// a plain `n = 0` was a value the walk could not describe. It can: the opcode says it is a
		// number, which is the whole of what a reader standing after the dot needs.
		case OPER_CONSTN: {
			ibValue made;
			SetTypeNumber(made, ibNumber((wxLongLong_t)code.m_param2.m_numIndex));
			out = made;
			if (outOrigin != nullptr) *outOrigin = ibNameOrigin::Declared;
			return ibStep::Value;
		}

		// ⭐⭐ A PROJECTED ROW ANSWERS WITH ITS OWN FIELDS. `select { N = o.N, V = doubled }` writes
		// the field names down as one constant and the row is shaped by them — so the reader after
		// the dot needs no lambda, no execution and no guess: it makes the same empty row the
		// instruction would make and asks IT what it has. This is the third mode of the one tape
		// working as intended — the compiler wrote the shape, and the reader reads it.
		case OPER_LINQ_ROW: {
			ibValue shape;      // the row holds it; this hand-off is the only reason it is named
			ibValue made;
			ibLinqRow(made, shape, ConstName(code.m_param3.m_numIndex), (long)code.m_param3.m_numArray);
			out = made;
			if (outOrigin != nullptr) *outOrigin = ibNameOrigin::Declared;
			return ibStep::Value;
		}

		// ⭐⭐ AND WHAT THE QUERY ANSWERS WITH — the instruction that turns LINQ's own collection into
		// something the language holds. Without this case the walk stopped at the very last step of
		// every query: `t = from o in Goods select { … };` then `t.` offered nothing, because the
		// answer moved from a verb the reader knew to an instruction it did not. That is the failure
		// mode this file exists to prevent, and it grew with every terminal that learned to compile.
		//
		// 🛑 NOTHING IS GUESSED ABOUT THE SHAPE. The rows went into that collection through
		// `OPER_LINQ_KEEP`, and the KEEP names the cell the row was in — so the row is resolved by
		// the same walk, one step down, and whatever IT turns out to be (a projected row, a group, a
		// catalog element) is what the columns are named from. The collection's own slot is the key
		// that ties the two instructions together; there is nothing else to match on.
		case OPER_LINQ_RESULT: {

			ibValue row;
			bool haveRow = false;
			for (long back = ip - 1; back >= m_firstCode && !haveRow; --back) {
				const ibByteUnit& kept = m_byteCode.m_listCode[(size_t)back];
				if ((kept.m_numOper % TYPE_DELTA1) != OPER_LINQ_KEEP)
					continue;
				if (!SameSlot(kept.m_param1, code.m_param2))
					continue;
				haveRow = ValueOfSlot(kept.m_param2, row, depth + 1, nullptr);
			}

			// 1 = the first row of the answer, which IS a row and not a collection.
			if (code.m_param3.m_numIndex == 1) {
				if (!haveRow)
					return ibStep::Failed;
				out = row;
				if (outOrigin != nullptr) *outOrigin = ibNameOrigin::Declared;
				return ibStep::Value;
			}

			// 2 = the GROUPS. Their shape is settled before any row is seen — one group per key,
			// and a group is Key and Values — so the sample is made rather than searched for, and
			// it is made by the engine's own maker so the reader cannot describe it differently
			// from the run. A `foreach` over it then answers about a group, which is the hop this
			// case exists to keep open.
			if (code.m_param3.m_numIndex == 2) {
				ibLinqGroupedSample(out);
				if (outOrigin != nullptr) *outOrigin = ibNameOrigin::Declared;
				return ibStep::Value;
			}

			// ⭐⭐ AND THEN THE SAME QUESTION THE RUNTIME ASKS, asked of the sample instead of a real
			// row: does this row NAME its columns? A projection does and a group does, and then the
			// answer is a TABLE carrying those names — the one exit every query has (docs/linq.md
			// §0.2h-quater). A plain value does not, and then the answer is an Array, which is what
			// `ToArray` over such rows means.
			//
			// 🛑 IT IS THE EXPORTED RULE (`ibLinqNamedColumns`, procUnit.h) and not a second copy of
			// it here. Two readings of "does it have columns" would eventually disagree, and the one
			// that drifts is this one — it would go on describing an Array as a table.
			std::vector<wxString> named;
			if (haveRow && ibLinqNamedColumns(row, named)) {
				ibValueModelTable* const table = new ibValueModelTable();
				if (auto* const columns = table->GetColumnCollection())
					for (const wxString& name : named)
						columns->AddColumn(name, ibTypeDescription(), name);
				out = table;
			}
			else
				out = new ibValueArray();

			if (outOrigin != nullptr) *outOrigin = ibNameOrigin::Declared;
			return ibStep::Value;
		}

		// ⭐⭐ ARITHMETIC ANSWERS WITH ITS LEFT OPERAND — and it had to learn to, because the
		// pipeline compiled as a loop counts with an ordinary `OPER_ADD` (compileCode.cpp,
		// CompileLinqChain). Before this, `n = src.Where(…).Count()` came back as a value
		// the walk could not name: the answer moved from a verb it knew to instructions it did not.
		// That is the third mode of the same tape, and a mode that stops reading is a mode that
		// stopped working.
		//
		// An operand is the honest answer: the type of `a + b` is the type of what it adds, string
		// concatenation included.
		//
		// 🛑 …BUT NOT ALWAYS THE LEFT ONE. An accumulator is written `n = n + 1`, so the left operand
		// IS the destination, and asking what wrote it finds this same instruction again — the walk
		// goes round rather than back. That is exactly the shape a counted loop emits, which is to
		// say the shape this case exists for. When the left operand is the destination, the OTHER
		// side is the one that says something.
		case OPER_ADD:
		case OPER_SUB:
		case OPER_MULT:
		case OPER_DIV:
		case OPER_MOD: {
			const bool leftIsSelf = code.m_param2.m_numArray == code.m_param1.m_numArray
				&& code.m_param2.m_numIndex == code.m_param1.m_numIndex;
			const ibParamRunUnit& other = leftIsSelf ? code.m_param3 : code.m_param2;
			if (ValueOfSlot(other, out, depth + 1, outOrigin))
				return ibStep::Value;

			// The other side said nothing — `Sum()` over a collection whose element type is not
			// known, for instance. Then the ACCUMULATOR itself is the answer, and what it is was
			// decided where it was INITIALISED. Asking for it plainly finds this same instruction
			// again (it writes the slot it reads), so the search starts BEFORE here — the same
			// `from` the lambda guard needs, for the same reason: resolve from where the question
			// is, not from the end of the tape.
			std::vector<ibCaretValue> init;
			if (!ValuesOfSlot(code.m_param1, init, depth + 1, ip - 1) || init.empty())
				return ibStep::Failed;
			if (outOrigin != nullptr) *outOrigin = init.front().m_origin;
			out = std::move(init.front().m_value);
			return ibStep::Value;
		}

		// A METHOD ON A VALUE ALREADY IN HAND may be called; see the header for why a bare-name
		// call (OPER_CALL) never is.
		case OPER_CALL_METHOD:
			return CallMethod(code, out, depth, ip) ? ibStep::Value : ibStep::Failed;

		// ⭐⭐ A PIPELINE STEP IS A STEP LIKE ANY OTHER, and it has to be — `Select` is spelled the
		// same whether it opens a query cursor or projects an array, and the compiler picks this
		// opcode by NAME (FindLinqMethodByName), so half the chains in the language pass through
		// here. Dispatched exactly as the runtime dispatches it, by enum id straight off the
		// operand: one road, so LINQ cannot drift away from the rest of the language.
		case OPER_CALL_LINQ:
			return LinqStep(code, out, depth, ip) ? ibStep::Value : ibStep::Failed;

		// ⭐⭐ A CALL IS A STEP INTO ANOTHER RANGE OF THE SAME STREAM — and walking it is the whole
		// point of the mechanism (Max, 2026-09-07: *"function A calls function B, I get its value —
		// that way I can literally compute the whole cycle"*). The compiler recorded where B starts;
		// B's body recorded what it returns; the return's slot is resolved by this same walk, one
		// level down and bounded to B's own instructions.
		//
		// ⚠ AND NOTHING IS EXECUTED. This is READING a function, not running it — which is why it
		// is allowed exactly where running one is not (see the header). A body that returns
		// something the walk cannot resolve simply answers nothing, at no risk.
		case OPER_CALL:
		case OPER_CALL_CLOSURE:
			return CallStep(code, ip, out, depth, outOrigin);

		default:
			return ibStep::NotAStep;   // not a step of the language; keep looking for the producer
		}
	}

	wxString ConstName(wxLongLong_t index) const
	{
		if (index < 0 || (size_t)index >= m_byteCode.m_listConst.size())
			return wxEmptyString;
		return m_byteCode.m_listConst[(size_t)index].GetString();
	}

	// THE NAME AN UNWRITTEN SLOT WAS DECLARED UNDER, looked for where the runtime would look for
	// the cell: the open function's own frame first, then the module's table, then each ancestor's.
	// Only bind-required entries are considered — see the note at the call site.
	wxString NameOfSlot(const ibParamRunUnit& slot) const
	{
		const wxLongLong_t index = slot.m_numIndex;

		auto inTable = [index](const std::vector<ibByteCode::ibByteCodeVarInfo>& table) {
			for (const auto& var : table)
				if (var.IsBindRequired() && (wxLongLong_t)var.m_slotIndex == index)
					return var.m_strRealName;
			return wxString();
		};

		// 🛑 THE OPERAND'S DEPTH IS NOT AN INDEX INTO THIS CHAIN, and it is worth writing down
		// because it looks exactly like one. `m_numArray` counts CONTEXTS the way the runtime
		// stacks them — and a block scope is a context too (`if`, `foreach`: RETURN_BLOCK in
		// compileContext.cpp), so the number grows with nesting and has no fixed relation to how
		// many MODULES up the name lives. Measured 2026-09-07 on this configuration's own modules:
		// reading the depth as a chain index took the answer rate from 66% down to 57%.
		//
		// So the name is found by SLOT across the ladder, nearest table first — the frame the caret
		// stands in, then each module above it. It is a search, and it is honest about being one.
		if (m_frame != nullptr) {
			const wxString name = inTable(m_frame->m_listLocals);
			if (!name.IsEmpty())
				return name;

			// 🛑 …BUT THIS FRAME'S CELL IS NOT LOOKED FOR IN ANOTHER FRAME'S TABLE. An operand at or
			// below zero says THIS frame (procUnit.cpp, ResolveRead), and inside a body that frame
			// is the declaration the caret stands in. Reading on into the module's table answers
			// with whoever holds the same cell NUMBER there — a different frame's variable, matched
			// on nothing but its position.
			//
			// Measured 2026-09-07, and it took an instrument to see: cell zero of a procedure —
			// its first parameter, or an operand the compiler never filled because the expression
			// refused at the caret — came back as `Data`, the global name that happens to sit at
			// cell zero of the module. So `NoSuchName.` and `Cancel.` both offered that object's
			// members, which is how a name the language does not know produced a confident list.
			if (FrameOf(slot) == 0)
				return wxEmptyString;
		}

		for (const ibCompileCode* compile : m_chain) {
			const wxString name = inTable(compile->m_cByteCode.m_listVar);
			if (!name.IsEmpty())
				return name;
		}

		return wxEmptyString;
	}

	// `.name` ASKED OF THE PARENT VALUE — a property when it is one, and otherwise the receiver
	// passed through unchanged, because `res.Select()` spells as a call over a member and failing
	// here would end the walk one step before the call that knows what to do with it.
	static bool MemberOf(ibValue& parent, const wxString& name, ibValue& out)
	{
		if (name.IsEmpty())
			return false;

		const long numProp = parent.FindProp(name);
		if (numProp != wxNOT_FOUND) {
			try { return parent.GetPropVal(numProp, out); }
			catch (...) { return false; }
		}

		if (parent.FindMethod(name) != wxNOT_FOUND) {
			out = parent;
			return true;
		}

		return false;
	}

	// ⭐⭐ THE ARGUMENTS THAT ARE CONSTANTS ARE PASSED, and the rest arrive empty. A literal is not
	// "arbitrary code to run" — it is a value the compiler already wrote into the pool, so handing
	// it over executes nothing, and it is exactly what the answer turns on: `GetCommonTemplate("")`
	// is nothing, `GetCommonTemplate("DeliveryNote")` is a template with an area to complete
	// against. The designer's own dispatcher branch takes four such functions by name — Type,
	// TypeOf, GetCommonForm, GetCommonTemplate — and every one of them reads its first argument.
	//
	// The tape is read the way the runtime reads it (procUnit.cpp, LOAD_ARG_CONST): the `count`
	// instructions after the call, a constant recognised by OPER_SETCONST with its pool index in
	// m_param1 and the pool selector in m_param2.
	void LoadArguments(long callIp, long count, std::vector<ibValue>& storage, int depth) const
	{
		for (long i = 0; i < count; i++) {

			const size_t ip = (size_t)(callIp + 1 + i);
			if (ip >= m_byteCode.m_listCode.size())
				return;

			const ibByteUnit& arg = m_byteCode.m_listCode[ip];

			// A LITERAL, straight out of the pool — the runtime's own shape for this
			// (procUnit.cpp, LOAD_ARG_CONST): the index in m_param1, the pool in m_param2.
			if ((arg.m_numOper % TYPE_DELTA1) == OPER_SETCONST) {
				if (arg.m_param2.m_numArray == 0
					&& arg.m_param1.m_numIndex >= 0
					&& (size_t)arg.m_param1.m_numIndex < m_byteCode.m_listConst.size())
					storage[(size_t)i] = m_byteCode.m_listConst[(size_t)arg.m_param1.m_numIndex];
				continue;
			}

			// ⭐ ANYTHING ELSE IS A SLOT, AND A SLOT IS WHAT THIS WALK RESOLVES. The precompiler
			// evaluated each argument before calling (its GetCurrentIdentifier, since deleted:
			// `listParam.emplace_back(GetExpression())`), and the same is
			// available here for free — the argument marker NAMES the slot the value went into, so
			// asking for it is one more step of the walk rather than a new mechanism. It stays
			// empty when it does not resolve, which is what an unanswerable argument should be.
			ibValue value;
			if (ValueOfSlot(arg.m_param1, value, depth + 1))
				storage[(size_t)i] = value;
		}
	}

	// The class name rides in m_param2 — its index in the const pool, with the argument count
	// beside it (procUnit.cpp, OPER_NEW).
	bool NewObject(const ibByteUnit& code, ibValue& out, long callIp, int depth) const
	{
		const wxString className = ConstName(code.m_param2.m_numIndex);
		if (className.IsEmpty())
			return false;

		const long count = (long)code.m_param2.m_numArray;
		std::vector<ibValue>  storage((size_t)(count > 0 ? count : 1));
		std::vector<ibValue*> args(storage.size());
		LoadArguments(callIp, count, storage, depth);
		for (size_t i = 0; i < storage.size(); i++)
			args[i] = &storage[i];

		try {
			out = ibValue::CreateObject(className, count > 0 ? args.data() : nullptr, count);

			// 🛑 ASK WHETHER A VALUE WAS MADE, NOT WHETHER IT HOLDS ANYTHING. IsEmpty() is virtual,
			// and a container answers it about its CONTENTS — so a freshly built `New Array()` calls
			// itself empty and the walk threw away a perfectly good value. Undefined is the only
			// answer that means "nothing was constructed".
			return out.GetType() != ibValueTypes::TYPE_EMPTY;
		}
		catch (...) {
			return false;   // a speculative question must not surface as an error
		}
	}

	// m_param3.m_numIndex is the ibLinqMethod enum value itself, NOT a const-pool index — the one
	// place in the walk where an operand is read straight rather than through the pool.
	// ⭐⭐ AND ITS ARGUMENTS ARE THE ONES THE COMPILER WROTE DOWN, exactly as CallMethod's are. This
	// step used to pass NONE — `DispatchLinqMethod(method, out, nullptr, 0)` — and half of LINQ is
	// spelled `Where(x => …)`: the dispatch answers "Method requires a function argument" and
	// refuses, so the compiler could not say what ANY chain arrives at. Measured 2026-09-08: a
	// thirteen-probe battery answered 6, and every miss was a pipeline verb.
	//
	// `OPER_CALL_LINQ` lays its arguments out the way `OPER_CALL_METHOD` does — the count in
	// m_param3.m_numArray, one OPER_SET per argument after the call (compileCode.cpp) — so this is
	// the same LoadArguments the neighbour already uses, not a second road.
	//
	// ⚠ NOTHING IS EXECUTED BY THIS. A pipeline verb builds an ITERATOR STATE over its upstream and
	// hands it back; the lambda is stored, not called. `Where`'s state answers PeekSample by
	// forwarding upstream (procUnitLINQ.cpp), which is exactly the type hint wanted here — so the
	// element type survives a chain without a single row being read or a line of the lambda run.
	bool LinqStep(const ibByteUnit& code, ibValue& out, int depth, long ip) const
	{
		ibValue receiver;
		if (!ValueOfSlot(code.m_param2, receiver, depth + 1))
			return false;

		const long passed = (long)code.m_param3.m_numArray;
		const long room   = std::max<long>(passed, 1);

		std::vector<ibValue>  storage((size_t)room);
		std::vector<ibValue*> args((size_t)room);
		LoadArguments(ip, passed, storage, depth);
		for (long i = 0; i < room; i++)
			args[(size_t)i] = &storage[(size_t)i];

		try {
			receiver.DispatchLinqMethod(
				static_cast<ibValue::ibLinqMethod>(code.m_param3.m_numIndex), out,
				args.data(), passed);
			return out.GetType() != ibValueTypes::TYPE_EMPTY;
		}
		catch (...) {
			return false;   // a speculative question must not surface as an error
		}
	}

	// Walk INTO the callee: its entry is `m_param2.m_numIndex` (procUnit reads the same operand as
	// `cRunContext.m_lStart`), its body ends at its own closer, and what it hands back is the
	// operand of its `OPER_RET`. A procedure has none, and then there is nothing to step to.
	ibStep CallStep(const ibByteUnit& code, long callIp, ibValue& out, int depth,
		ibNameOrigin* outOrigin = nullptr) const
	{
		const long entry = (long)code.m_param2.m_numIndex;
		if (entry < 0 || (size_t)entry >= m_byteCode.m_listCode.size())
			return ibStep::Failed;

		// ⭐⭐ A FUNCTION THAT ANSWERS WITH ONE OF ITS PARAMETERS ANSWERS WITH WHAT WAS PASSED. The
		// callee's own frame writes nothing into a parameter slot — the CALLER does, in the
		// `OPER_SET` instructions the compiler lays down right after the call, one per declared
		// parameter and in order (compileCode.cpp). So a body whose `return` names a parameter has
		// its answer in the caller's frame, and the walk stays on this side to read it.
		//
		// Measured 2026-09-08: `Function Take(r) { return r; }` then `Take(q.First()).` offered
		// nothing, while the very same `q.First().` offered the row — the only difference being a
		// call that passes it straight through, which is how half of a person's own helpers look.
		const auto argumentOf = [this, callIp, depth, outOrigin](long slotIndex, ibValue& into) {
			const long argIp = callIp + 1 + slotIndex;
			if (argIp <= callIp || (size_t)argIp >= m_byteCode.m_listCode.size())
				return false;
			const ibByteUnit& arg = m_byteCode.m_listCode[(size_t)argIp];
			const short argOper = arg.m_numOper % TYPE_DELTA1;
			if (argOper != OPER_SET && argOper != OPER_SETREF)
				return false;   // not the argument tape after all — say nothing rather than guess
			return ValueOfSlot(arg.m_param1, into, depth + 1, outOrigin);
		};

		for (long ip = entry + 1; (size_t)ip < m_byteCode.m_listCode.size(); ip++) {

			const short oper = m_byteCode.m_listCode[(size_t)ip].m_numOper % TYPE_DELTA1;

			if (oper == OPER_ENDFUNC || oper == OPER_ENDLFUNC || oper == OPER_END)
				return ibStep::Failed;          // reached the end without a return: a procedure

			if (oper != OPER_RET)
				continue;

			const ibParamRunUnit& returned = m_byteCode.m_listCode[(size_t)ip].m_param1;

			// Is the slot it returns one of its own PARAMETERS? Then nothing in the callee wrote
			// it and the answer is on this side — see argumentOf. A parameter is one of the first
			// `paramCount` slots of the frame, which is how the runtime fills them (procUnit.cpp,
			// phase 1 of the call).
			const ibByteCode::ibByteFunction* const callee = m_byteCode.FindFunctionByEntry(entry);
			if (callee != nullptr && returned.m_numArray <= 0
				&& returned.m_numIndex >= 0
				&& returned.m_numIndex < (wxLongLong_t)callee->m_listParam.size()) {

				ibValue passed;
				if (argumentOf((long)returned.m_numIndex, passed)) {
					out = passed;
					return ibStep::Value;
				}
			}

			// The return's own slot, resolved WITHIN the callee — bounded at its entry so a name
			// that means one thing in B is not answered from the caller's frame.
			const ibCaretWalk inside(m_chain, callee, ip, m_metaData, entry);
			return inside.ValueOfSlot(returned, out, depth + 1, outOrigin)
				? ibStep::Value : ibStep::Failed;
		}

		return ibStep::Failed;
	}

	// ⭐⭐ A BARE-NAME PLATFORM CALL NAMES ITS RECEIVER ON THE METHOD, not in the operand.
	// `CurrentDate()` / `String(x)` / `GetCommonTemplate(…)` are methods OF a context binding, and
	// the compiler resolved the bare name through the ladder before emitting a method call on that
	// binding's slot — a slot no instruction ever wrote and whose number alone does not say whose
	// table it belongs to. The binding's NAME, though, is recorded right on the function entry
	// (`m_strContext`), which is also how the runtime dispatches these. So the method is looked up
	// by name, exactly as the compiler looked it up, and its owner is asked for the value.
	//
	// Measured 2026-09-07: this one shape was 55 of the 96 positions still refusing in real modules —
	// `template = GetCommonTemplate("DeliveryNote")` and everything downstream of it.
	bool ReceiverOfContextMethod(const wxString& method, ibValue& out) const
	{
		if (method.IsEmpty())
			return false;

		for (const ibCompileCode* compile : m_chain)
			for (const auto& fn : compile->m_cByteCode.m_listFunc)
				if (fn.IsContextMethod() && !fn.m_strContext.IsEmpty()
					&& stringUtils::CompareString(method, fn.m_strRealName))
					return BoundValue(fn.m_strContext, out);

		return false;
	}

	bool CallMethod(const ibByteUnit& code, ibValue& out, int depth, long ip) const
	{
		const wxString method = ConstName(code.m_param3.m_numIndex);

		ibValue receiver;
		ValueOfSlot(code.m_param2, receiver, depth + 1);

		// 🛑 THE TEST IS "HAS THIS METHOD", NOT "RESOLVED TO SOMETHING". A receiver slot nobody
		// wrote is found by number across the ladder, and a number alone does not say whose table
		// it belongs to — so the search can come back with a DIFFERENT binding that happens to sit
		// at that index. It then answers nothing, silently and plausibly, which is the worst shape
		// a wrong answer can take. Asking for the method sorts both cases at once: no receiver, and
		// the wrong one.
		long lMethodNum = receiver.FindMethod(method);
		if (lMethodNum == wxNOT_FOUND) {
			if (!ReceiverOfContextMethod(method, receiver))
				return false;
			lMethodNum = receiver.FindMethod(method);
		}

		if (lMethodNum == wxNOT_FOUND || !receiver.HasRetVal(lMethodNum))
			return false;

		// THE ARGUMENTS THE COMPILER ALREADY WROTE DOWN — see LoadArguments. The frame is sized by
		// the larger of what the method declares and what the caller passed, because a callee may
		// index by its own arity while the tape only carries what was written.
		const long passed   = (long)code.m_param3.m_numArray;
		const long declared = receiver.GetNParams(lMethodNum);
		const long room     = std::max<long>(std::max(declared, passed), 1);

		std::vector<ibValue>  storage((size_t)room);
		std::vector<ibValue*> args((size_t)room);
		LoadArguments(ip, passed, storage, depth);
		for (long i = 0; i < room; i++)
			args[(size_t)i] = &storage[(size_t)i];

		try {
			return receiver.CallAsFunc(lMethodNum, out, args.data(), passed);
		}
		catch (...) {
			return false;   // a speculative question must not surface as an error
		}
	}

	// The handles the chain was given, by name and nearest first — which is the rule the compiler
	// bound the name by, not a search widened until something matches. Both maps are filled in the
	// designer as well as at runtime (Bind* on the descriptor), which is what lets this answer in a
	// process that executes nothing.
	// ⭐⭐ WHOSE A MEMBER IS, ANSWERED BY IDENTITY RATHER THAN BY NAME. `ThisObject` is reached as a
	// member of the scope object exactly as `Chars` is reached as a member of a string, so the step
	// alone cannot tell a context handle from a platform member. Comparing NAMES would decide it on
	// a coincidence — any object with a member called `Filter` would be called bound.
	//
	// The maps hold the very values that were staged, so the question is not "is it called that" but
	// "is it THAT ONE". A handful of entries, walked once, and it cannot be fooled.
	//
	// 🛑 BOTH SIDES ARE CHASED TO THE END, and the first attempt was not — measured 2026-09-08, it
	// matched nothing at all. `GetRef` walks the reference chain to the object at the bottom
	// (value.cpp: `m_pRef->GetRef()`), while a map holds whatever was staged, which is often a
	// wrapper around that object. Comparing a resolved end against an unresolved one compares two
	// different pointers to the same thing and answers "no" every time — silently, so the whole
	// mechanism read as "everything is the platform's".
	ibNameOrigin OriginOfStaged(const ibValue& value) const
	{
		const ibValue* const held = value.GetRef();
		if (held == nullptr)
			return ibNameOrigin::Platform;

		for (const ibCompileCode* compile : m_chain) {

			for (const auto& bind : compile->m_listExternValue)
				if (bind.second != nullptr && bind.second->GetRef() == held)
					return ibNameOrigin::Bound;

			for (const auto& context : compile->m_listContextValue)
				if (context.second.m_value != nullptr && context.second.m_value->GetRef() == held)
					return ibNameOrigin::Context;
		}

		return ibNameOrigin::Platform;
	}

	// THE ROW A PIPELINE LAMBDA IS WRITTEN AGAINST — found by following the instructions back to the
	// verb that takes it, which is the only place that knows.
	//
	// Three hops, each of them a fact the compiler already wrote down: this frame is entry N of
	// m_listFunc; the OPER_LFUNC that materialises entry N names the slot the lambda value went
	// into; the OPER_CALL_LINQ that lists that slot among its arguments names the SOURCE in its
	// receiver operand. The source's element is then asked for exactly as `foreach` asks.
	//
	// ⚠ The receiver is resolved in the frame that CONTAINS the call, not in the lambda's — a walk
	// bounded at the call site, the same construction CallStep uses to read a callee's return.
	// ⭐⭐ WHAT A COLLECTION HOLDS, READ FROM WHAT WAS PUT IN IT. An Array built in code is EMPTY
	// while the text is being read — `Add` is an instruction, not something a compile executes — so
	// asking it for a sample answers nothing, and every caret downstream of it went dark:
	//
	//     var people = New Array;
	//     people.Add(New Structure("Country, Name", "USA", "Alice"));
	//     var q = from p in people group p.Name by p.|      ← nothing offered
	//
	// while `New Structure(...)` on its own line offers `Country, Name` perfectly well. Measured
	// 2026-09-08 over the project's own LINQ suite: 66 of 197 carets refused, and every one of them
	// was this — a row of a collection filled in code, and then the group built from it.
	//
	// The answer is on the tape already. The compiler wrote each `Add` down as an OPER_CALL_METHOD
	// on that very slot, with its argument in the OPER_SET that follows; so the element is whatever
	// was passed there, resolved by this same walk. Nothing is executed and nothing is guessed —
	// the fill is READ, exactly as a loop header or a return is read elsewhere here.
	//
	// ⚠ NO LIST OF METHOD NAMES. The first attempt matched `Add` and `Insert` as strings, and Max
	// stopped it: an array fills with `Add`, a map with `Insert`, a table with its own — a list
	// here would fall behind the next container silently. Nothing needs to be recognised. The tape
	// says a method was called ON THIS COLLECTION; calling it is what the runtime would do, and
	// whether it may be called is a question the runtime already answers for itself (backend_core.h,
	// ibEvalMode — `eval_complete` is the mode this whole walk runs inside).
	//
	// ⚠ AND PROCEDURES ESPECIALLY. `CallMethod` above refuses anything without a return value,
	// which is correct for it — it is asked for a VALUE — but it means `Add` was never once called
	// while reading a text. Here the return is beside the point: the effect is.
	void ReplayCallsOn(const ibParamRunUnit& collection, ibValue& target, int depth) const
	{
		if (depth > kMaxChain)
			return;

		long foreign = 0;

		for (long ip = m_firstCode; ip <= m_lastCode && (size_t)ip < m_byteCode.m_listCode.size(); ip++) {

			const ibByteUnit& code = m_byteCode.m_listCode[(size_t)ip];
			const short oper = code.m_numOper % TYPE_DELTA1;

			// Another declaration's body — its slot numbers are not ours (the same guard the
			// producer scan carries, for the same reason).
			if (oper == OPER_FUNC || oper == OPER_LFUNC)   { foreign++; continue; }
			if (oper == OPER_ENDFUNC || oper == OPER_ENDLFUNC) { if (foreign > 0) foreign--; continue; }
			if (foreign > 0)
				continue;

			if (oper != OPER_CALL_METHOD || !SameSlot(code.m_param2, collection))
				continue;

			const long method = target.FindMethod(ConstName(code.m_param3.m_numIndex));
			if (method == wxNOT_FOUND)
				continue;

			const long passed   = (long)code.m_param3.m_numArray;
			const long declared = target.GetNParams(method);
			const long room     = std::max<long>(std::max(declared, passed), 1);

			std::vector<ibValue>  storage((size_t)room);
			std::vector<ibValue*> args((size_t)room);
			LoadArguments(ip, passed, storage, depth + 1);
			for (long i = 0; i < room; i++)
				args[(size_t)i] = &storage[(size_t)i];

			try {
				ibValue ignored;
				if (target.HasRetVal(method)) target.CallAsFunc(method, ignored, args.data(), passed);
				else                          target.CallAsProc(method, args.data(), passed);
			}
			catch (...) {
				// A method that will not run while a text is being read is an ordinary outcome —
				// the collection simply stays as empty as it was, and the caret says nothing rather
				// than something wrong.
			}
		}
	}

	bool ElementOfLambdaSource(std::vector<ibCaretValue>& out, int depth) const
	{
		if (depth > kMaxChain)
			return false;

		long frameIdx = wxNOT_FOUND;
		for (size_t i = 0; i < m_byteCode.m_listFunc.size(); i++)
			if (&m_byteCode.m_listFunc[i] == m_frame) { frameIdx = (long)i; break; }
		if (frameIdx == wxNOT_FOUND)
			return false;

		long lfuncIp = wxNOT_FOUND;
		for (size_t ip = 0; ip < m_byteCode.m_listCode.size(); ip++) {
			const ibByteUnit& unit = m_byteCode.m_listCode[ip];
			if ((unit.m_numOper % TYPE_DELTA1) == OPER_LFUNC
				&& (long)unit.m_param3.m_numIndex == frameIdx) { lfuncIp = (long)ip; break; }
		}
		if (lfuncIp == wxNOT_FOUND)
			return false;

		const ibParamRunUnit lambdaSlot = m_byteCode.m_listCode[(size_t)lfuncIp].m_param1;

		for (size_t ip = (size_t)lfuncIp; ip < m_byteCode.m_listCode.size(); ip++) {

			const ibByteUnit& call = m_byteCode.m_listCode[ip];
			if ((call.m_numOper % TYPE_DELTA1) != OPER_CALL_LINQ)
				continue;

			const long passed = (long)call.m_param3.m_numArray;
			bool takesTheLambda = false;
			for (long a = 0; a < passed && !takesTheLambda; a++) {
				const size_t argIp = ip + 1 + (size_t)a;
				if (argIp >= m_byteCode.m_listCode.size())
					break;
				takesTheLambda = SameSlot(m_byteCode.m_listCode[argIp].m_param1, lambdaSlot);
			}
			if (!takesTheLambda)
				continue;

			const ibCaretWalk outer(m_chain, nullptr, (long)ip, m_metaData, 0);
			ibValue source;
			if (!outer.ValueOfSlot(call.m_param2, source, depth + 1))
				return false;

			const std::shared_ptr<ibValueIteratorState> state = source.CreateIterator();
			if (!state)
				return false;

			out.emplace_back();
			try {
				if (state->PeekSample(out.back().m_value)) {
					out.back().m_origin = ibNameOrigin::Declared;
					m_trace << wxString::Format(wxT("\n  %*slambda row <- the source of the verb at %u"),
						depth * 2, wxT(""), call.m_numString);
					return true;
				}
			}
			catch (...) {}
			out.pop_back();
			return false;
		}

		return false;
	}

	// ⭐ WHICH MAP ANSWERED IS THE ORIGIN. The two are different facts about a name and the caller
	// has no other way to tell them apart afterwards: an export or a native bind (`RegisterRecords`,
	// `Filter`, a common module under its own name) against a CONTEXT handle of the module the caret
	// stands in (`ThisObject`, `ThisForm`). Optional, because the steps that only want the value
	// have nothing to do with the answer.
	bool BoundValue(const wxString& name, ibValue& out, ibNameOrigin* outOrigin = nullptr) const
	{
		if (name.IsEmpty())
			return false;

		for (const ibCompileCode* compile : m_chain) {

			const auto extern_ = compile->m_listExternValue.find(name);
			if (extern_ != compile->m_listExternValue.end() && extern_->second != nullptr) {
				out = *extern_->second;
				if (outOrigin != nullptr)
					*outOrigin = ibNameOrigin::Bound;
				ibJournalInfo(wxT("complete"), wxT("  bound `%s` from the extern map"), name);
				return true;
			}

			const auto context = compile->m_listContextValue.find(name);
			if (context != compile->m_listContextValue.end() && context->second.m_value != nullptr) {
				out = *context->second.m_value;
				if (outOrigin != nullptr)
					*outOrigin = ibNameOrigin::Context;
				ibJournalInfo(wxT("complete"), wxT("  bound `%s` from the context map, scope=%d"),
					name, (int)context->second.m_scopeContext);
				return true;
			}
		}

		return false;
	}

	const ibCompileChain&              m_chain;
	const ibByteCode&                  m_byteCode;
	const ibByteCode::ibByteFunction*  m_frame;
	const long                         m_lastCode;

	// The configuration this caret belongs to — where a DECLARATION is asked about, and where a
	// value of a declared type is made. Null for a text with no module.
	const ibMetaData*                  m_metaData = nullptr;

	// See StepOf. Mutable because tracing what a walk did does not change what it IS.
	mutable wxString                   m_trace;

	// The earliest instruction this walk may look at. Zero for a caret; a function's entry when the
	// walk has STEPPED INTO one, because that body's slots are its own frame's and reading past its
	// start would resolve them against the caller's.
	const long                         m_firstCode;
};

// The compile the text is judged against — the module it LIVES in, whose own bindings (ThisObject,
// the object's context) and whose parent chain (the common modules, the globals) are the language
// the caret stands in. Null is ordinary: a text with no home is judged against the bare language.
ibCompileModule* HostModuleOf(const ibValueMetaObject* moduleObject)
{
	if (moduleObject == nullptr)
		return nullptr;

	const ibMetaData* metaData = moduleObject->GetMetaData();
	if (metaData == nullptr)
		return nullptr;

	ibCompileValueCache* cache = metaData->GetCompileCache();
	if (cache == nullptr)
		return nullptr;

	ibRuntimeModuleDataObject* dataRef = nullptr;
	if (!cache->FindCompileModule(moduleObject, dataRef) || dataRef == nullptr)
		return nullptr;

	return dataRef->GetCompileModule();
}

// ⭐⭐ THE ONE COMPILE BOTH DOORS ASK. A path and a list are two readings of a single artefact, so
// they must not be two compiles: the same text, cut at the same caret, parented to the same module,
// or the two answers can disagree about what exists.
class ibCaretCompile {
public:
	// ⭐⭐ WHAT A CARET MEANS, AND THE TWO DOORS DO NOT MEAN THE SAME THING BY IT.
	//
	// Both agree on the first step: a word the caret stands in is being TYPED. `Catalogs.Goo` is
	// somebody three letters into `Goods`, and what they are asking is what MAY follow the dot, so
	// the caret moves to the start of the word. Resolving `Goo` instead answers "no such member",
	// which is true and useless.
	//
	// They part on the second. A PATH is read UP TO the caret and no further — that is what "the
	// value at a caret" means, and it is also what makes the dotted case and the half-typed case one
	// case, since with the word set aside the text ends on a dot and a tolerant compile already
	// keeps that (compileCode.cpp, the attribute step). A LIST is read WHOLE, because a name is in
	// scope whether or not it is written above: module variables declared at the top are assigned
	// further down and are already there by the time the body runs, and a function declared below
	// the caret is perfectly callable from above (Max, 2026-09-07). The precompiler never truncated
	// for exactly this reason — it used the caret as a POSITION, never as an END, and gated only
	// what genuinely depends on order.
	ibCaretCompile(const wxString& text, unsigned int caret, const ibValueMetaObject* moduleObject,
		bool upToCaret)
		: m_compiler(wxT("caret"), wxT("caret"), false)
	{
		const unsigned int asked = caret < text.length() ? caret : (unsigned int)text.length();

		m_at = asked;
		while (m_at > 0 && (wxIsalnum(text[m_at - 1]) || text[m_at - 1] == wxT('_')))
			m_at--;

		m_typed = text.Mid(m_at, asked - m_at);

		m_chain.push_back(&m_compiler);

		// Tolerant, because the text ends mid-expression BY CONSTRUCTION: standing after a dot is
		// what asking either question means, so the refusal at the caret is not a report about the
		// text — it IS the caret.
		m_compiler.SetCompileMode(ibCompileCode::ibCompileMode::Tolerant);

		// 🛑 …AND THE HOST HAS TO HAVE BEEN COMPILED, the same trap the syntax check documents: a
		// module nobody compiled carries an empty bytecode, so there is nothing to stand beside.
		if (ibCompileModule* host = HostModuleOf(moduleObject)) {
			try { host->Compile(); }
			catch (const ibBackendException&) {
				// A configuration whose own modules do not compile is not this walk's complaint to
				// make — it was asked about the CARET. It answers with whatever did resolve.
			}

			// ⭐⭐ THE TEXT IS A NEW VERSION OF THAT MODULE, SO IT STANDS WHERE THE MODULE STANDS —
			// beside it, not under it. Parenting it to the module itself put every declaration in
			// the text up against its own older copy: "a procedure with this name is already
			// defined", which ends the declaration, and the first function of a real module ended
			// the lot. Measured on this configuration's own modules, 2026-09-07: 12% of carets in
			// real code answered, against 100% of a battery of one-liners that declared nothing.
			// The one-liners could not see it, which is exactly what made the battery agreeable.
			//
			// Standing beside it means taking its PLACE: its parent becomes this compile's parent,
			// and the handles the module was GIVEN — ThisObject, the object's own attributes,
			// RegisterRecords — are carried over, because they belong to the module rather than to
			// the text and the new version has them too.
			for (const auto& kv : host->m_listExternValue)
				m_compiler.AddVariable(kv.first, kv.second);
			for (const auto& kv : host->m_listContextValue)
				m_compiler.AddContextVariable(kv.first, kv.second.m_value, kv.second.m_scopeContext);
			for (const auto& kv : host->m_listLocalValue)
				m_compiler.AddLocalVariable(kv.first, kv.second);

			ibCompileModule* stand = host->GetParent();
			if (stand != nullptr)
				m_compiler.SetParent(stand);
			for (const ibCompileModule* up = stand; up != nullptr; up = up->GetParent())
				m_chain.push_back(up);
		}

		// ⭐⭐ AND THE COMPILE IS TOLD WHERE THE CARET IS, because the declaration standing at it is
		// something only the compile can see: at the end of each body it holds both ends of that
		// body's span, and nothing of that survives into the tape (compileCode.h, SetCaret).
		m_compiler.SetCaret((long)m_at);

		// ⚠ NO GUARD. A tolerant compile reports nothing and keeps reading — it does not raise
		// (compileCode.cpp, DoSetError) — so catching here would be catching nothing.
		m_compiler.Compile(upToCaret ? text.Left(m_at) : text);
	}

	// The body the caret is standing in, or null for the module's own level — answered by the
	// compile that just ran, not looked for afterwards.
	const ibByteCode::ibByteFunction* CaretFrame() const
	{
		const long owner = m_compiler.GetCaretOwner();
		if (owner < 0)
			return nullptr;
		return m_compiler.m_cByteCode.FindFunctionByEntry(owner);
	}

	unsigned int Caret() const { return m_at; }

	// The identifier being typed — the text between where the caret's word starts and where the
	// caret actually is. It is what a list FILTERS by, and the one name such a list must not offer:
	// see the note where it is used.
	const wxString& TypedWord() const { return m_typed; }

	const ibCompileChain& Chain() const { return m_chain; }
	const ibByteCode& ByteCode() const { return m_compiler.m_cByteCode; }

	// The tokens this compile read, for the one question that is about the GRAMMAR of a position
	// rather than about a value: which clause the caret is being typed into. Nothing is re-lexed —
	// the stream is the one the compile just used.
	const std::vector<ibLexem>& Lexems() const { return m_compiler.GetLexems(); }

	// ⭐ THE SAME TAPE, NOT SLICED. The question above wants the instructions, which the base type
	// carries; the LINQ tree lives only on the compiler's own full form (byteCode.h), and a reader
	// that needs it has to be handed that form.
	const ibByteExtCode& FullByteCode() const { return m_compiler.m_cByteCode; }

	// What the compile refused, with a position on every line — enough to see roughly where the
	// chain broke. Empty when the text compiled cleanly.
	const wxString& Refusal() const { return m_compiler.GetRefusal(); }

private:
	// ⭐⭐ THE KIND OF QUESTION, HELD FOR AS LONG AS IT IS BEING ANSWERED — and declared FIRST, so it
	// is set before anything is compiled and released only after every value has been worked out.
	//
	// 🛑 A SCOPE AROUND THE COMPILE ALONE WOULD MISS THE POINT, and did: the compile emits
	// instructions, and it is the WALK OVER THEM that calls into the platform — `GetCommonTemplate`,
	// a constructor, a method on a value. Those run after this object is built, so a guard living
	// only in the constructor would be gone by the time it mattered. Every evaluation this object
	// serves happens inside its lifetime; the mode has to as well.
	const ibBackendException::ibEvalModeScope m_answering{ eval_complete };

	ibCompileCode  m_compiler;
	ibCompileChain m_chain;
	unsigned int   m_at = 0;
	wxString       m_typed;
};

// ⭐⭐ THE ORIGIN, READ OFF TWO FACTS THE BYTECODE ALREADY CARRIES: the entry's KIND, and WHICH
// module of the chain held it. Nothing is stamped and nothing is guessed — a name's kind says what
// KIND of thing it is (a local, an export, a bound handle, a context, a property of one) and its
// place in the ladder says WHOSE it is (this text, the module it lives in, an ancestor, the root).
//
// The ladder, as Max put it: root (code, global vars, contexts) → common modules, which see the
// root entire and register themselves back on it → an object module, which adds its own exports and
// contexts → a form on top. So `depth == 0` is the text being typed, the LAST link is the
// configuration's own manager, and everything between is an ancestor module.
ibNameOrigin OriginOfVar(ibVarKind kind, size_t depth, size_t chainSize, bool globalModule)
{
	const bool own  = depth == 0;
	const bool root = depth + 1 >= chainSize;

	switch (kind) {

	// A CONTEXT'S PROPERTY IS THE OBJECT'S OWN SURFACE — its attributes and tabular sections — and
	// on the root the very same shape is the platform's collections (`Catalogs` is a property of
	// `Manager`). One kind, two origins, told apart by whose table it sits in.
	case ibVarKind::ContextProp:
		return root ? ibNameOrigin::Platform : ibNameOrigin::Member;

	case ibVarKind::Context:
		return root ? ibNameOrigin::Platform : ibNameOrigin::Context;

	case ibVarKind::External:
		return root ? ibNameOrigin::Global : ibNameOrigin::Bound;

	default:   // Local / Export / Protected — something somebody WROTE
		if (own)          return ibNameOrigin::Declared;
		if (globalModule) return ibNameOrigin::GlobalModule;
		return root ? ibNameOrigin::Global : ibNameOrigin::Inherited;
	}
}

ibNameOrigin OriginOfFunc(ibFnKind kind, size_t depth, size_t chainSize, bool globalModule)
{
	const bool root = depth + 1 >= chainSize;

	// ⭐ A METHOD OF A CONTEXT BINDING IS THE SAME RELATIONSHIP A PROPERTY OF ONE IS, so it gets the
	// same answer: the object's own surface off-root (`Write`, `Fill`, `Lock` on a document), the
	// platform's on it (`GetForm` on Manager, and every system function registered the same way).
	// Splitting them made one object's attributes read `member` while its methods read `context` —
	// the same surface under two names, which is exactly what an origin is for telling apart.
	if (kind == ibFnKind::ContextMethod)
		return root ? ibNameOrigin::Platform : ibNameOrigin::Member;

	if (depth == 0)   return ibNameOrigin::Declared;
	if (globalModule) return ibNameOrigin::GlobalModule;
	return root ? ibNameOrigin::Global : ibNameOrigin::Inherited;
}

// The value a context binding holds, by name and nearest first — the same lookup the path walk
// makes for the start of a chain, and for the same reason: the binding is what KNOWS.
const ibValue* BoundValueOf(const ibCompileChain& chain, const wxString& name)
{
	if (name.IsEmpty())
		return nullptr;

	for (const ibCompileCode* compile : chain) {

		const auto context = compile->m_listContextValue.find(name);
		if (context != compile->m_listContextValue.end() && context->second.m_value != nullptr)
			return context->second.m_value;

		const auto extern_ = compile->m_listExternValue.find(name);
		if (extern_ != compile->m_listExternValue.end() && extern_->second != nullptr)
			return extern_->second;
	}
	return nullptr;
}

// ⭐ THE CALL FORM, ASKED OF WHOEVER KNOWS IT. For a function somebody WROTE, the declared
// parameters are right here, and building from them beats storing a second copy to keep in step.
// A METHOD OF A CONTEXT BINDING has no declared parameters at all — it is native, and its shape
// lives on the VALUE — so the value is asked, exactly as the members answer asks it. Building from
// the empty list instead produced `Write(, )`, which is worse than saying nothing.
wxString SignatureOf(const ibByteCode::ibByteFunction& fn, const ibCompileChain& chain)
{
	if (fn.IsContextMethod()) {
		const ibValue* owner = BoundValueOf(chain, fn.m_strContext);
		if (owner == nullptr)
			return wxEmptyString;

		ibValue& value = *const_cast<ibValue*>(owner);
		const long numMethod = value.FindMethod(fn.m_strRealName);
		// ⚠ `wxString(wxEmptyString)` and not the bare constant: outside MSVC `wxEmptyString` is a
		// `const wxChar*`, both arms of the conditional convert to each other, and the expression is
		// AMBIGUOUS — green here, a build failure on macOS and Linux (portability.md §1.10). The tree
		// writes it this way in every other place; this one was the exception, and CI said so.
		return numMethod != wxNOT_FOUND
			? value.GetMethodHelper(numMethod) : wxString(wxEmptyString);
	}

	wxString signature = fn.m_strRealName + wxT("(");
	for (size_t i = 0; i < fn.m_listParam.size(); i++) {
		if (i > 0) signature += wxT(", ");
		signature += fn.m_listParam[i].m_strName;
	}
	return signature + wxT(")");
}

} // namespace

bool ibValueAtCaret(const wxString& text, unsigned int caret,
	const ibValueMetaObject* moduleObject, std::vector<ibCaretValue>& outValues, wxString* outRefusal)
{
	if (text.IsEmpty())
		return false;

	const ibCaretCompile compiled(text, caret, moduleObject, /*upToCaret*/ true);

	ibByteCode::ibCaretPoint point;
	point.m_position = compiled.Caret();

	wxString walkTrace;
	bool resolved = false;
	if (compiled.ByteCode().FindCaret(point) && point.m_instruction >= 0) {
		const ibCaretWalk walk(compiled.Chain(), compiled.CaretFrame(), point.m_instruction,
			moduleObject != nullptr ? moduleObject->GetMetaData() : nullptr);
		resolved = walk.ValueAt(point.m_instruction, outValues);
		walkTrace = walk.Trace();
	}

	// ⭐⭐ THE WALK SAYS WHAT IT STOOD ON. Not a debugging leftover — the third requirement of this
	// arc, stated plainly: a mechanism nobody has to guess about. Four fixes-by-reasoning missed
	// here in one day and a single dump would have answered each of them, because the question is
	// always the same one and it is not answerable from the text: WHICH instructions the caret
	// landed among, and what their operands name. It costs one journal line per completion, in a
	// journal that already records every SQL statement.
	{
		const ibByteCode& bc = compiled.ByteCode();

		// ⭐ A REFUSAL IS READ WITH THE WHOLE TAPE IN VIEW. Four instructions before the caret are
		// enough to see what it stood on, and useless for seeing why a step further down could not
		// resolve — a query emits its clauses out of text order, so the producer that failed can
		// sit twenty instructions back. When the walk RESOLVED there is nothing to hunt for and the
		// window stays small; when it refused, the tape is what the next question is asked of.
		const long windowFrom = resolved ? point.m_instruction - 4 : 0;
		const long windowTo   = resolved ? point.m_instruction + 1
			: (long)std::min<size_t>(bc.m_listCode.size(), 80u) - 1;

		wxString window;
		for (long ip = windowFrom; ip <= windowTo; ip++) {
			if (ip < 0 || (size_t)ip >= bc.m_listCode.size())
				continue;
			const ibByteUnit& code = bc.m_listCode[(size_t)ip];
			// p3 spells the MEMBER NAME for an attribute step, and an empty one is what marks the
			// caret's own dangling dot — so the walk's whole decision is unreadable without it.
			wxString member;
			if (code.m_param3.m_numIndex >= 0
				&& (size_t)code.m_param3.m_numIndex < compiled.ByteCode().m_listConst.size())
				member = compiled.ByteCode().m_listConst[(size_t)code.m_param3.m_numIndex].GetString();

			window << wxString::Format(wxT("\n  %s%4ld  op %3d (%3d)  p1(%d,%d) p2(%d,%d) p3(%d,%d)=`%s`  at %u"),
				ip == point.m_instruction ? wxT("->") : wxT("  "), ip,
				(int)code.m_numOper, (int)(code.m_numOper % TYPE_DELTA1),
				(int)code.m_param1.m_numArray, (int)code.m_param1.m_numIndex,
				(int)code.m_param2.m_numArray, (int)code.m_param2.m_numIndex,
				(int)code.m_param3.m_numArray, (int)code.m_param3.m_numIndex, member,
				code.m_numString);
		}

		const ibByteCode::ibByteFunction* const caretFrame = compiled.CaretFrame();
		ibJournalInfo(wxT("complete"), wxT("caret %u -> instruction %ld, %s [frame=%s params=%d]%s%s"),
			compiled.Caret(), point.m_instruction,
			resolved ? wxT("resolved") : wxT("REFUSED"),
			caretFrame != nullptr ? caretFrame->m_strRealName : wxString(wxT("(module)")),
			caretFrame != nullptr ? (int)caretFrame->m_listParam.size() : -1,
			window, walkTrace);
	}

	// ⭐⭐ AND WHEN IT DID NOT RESOLVE, WHAT THE COMPILE REFUSED GOES BACK TO WHOEVER ASKED. Writing
	// the refusals down and never reading them would be the same mistake in a nicer coat: the
	// sentences exist so that "why did that dot not resolve" has an answer, and this is where they
	// are collected and handed over.
	//
	// ⚠ AN EMPTY REFUSAL IS ITSELF AN ANSWER: the text compiled and the WALK stopped, which is a
	// different fault from a text that would not compile.
	if (!resolved) {
		if (outRefusal != nullptr)
			*outRefusal = compiled.Refusal();

		ibJournalInfo(wxT("complete"), wxT("nothing resolves at %u%s%s"),
			compiled.Caret(),
			compiled.Refusal().IsEmpty() ? wxT(" (the text compiled)") : wxT("; the compile refused:\n"),
			compiled.Refusal());
	}

	return resolved;
}

bool ibFieldTypesOf(const ibValueMetaObject* moduleObject, const ibValue& owner,
	const wxString& field, std::vector<ibFieldType>& outTypes)
{
	return moduleObject != nullptr
		&& FieldTypesOf(moduleObject->GetMetaData(), owner, field, outTypes);
}

bool ibNamesAtCaret(const wxString& text, unsigned int caret,
	const ibValueMetaObject* moduleObject, std::vector<ibCaretName>& outNames)
{
	// ⚠ AN EMPTY TEXT IS AN ORDINARY QUESTION HERE, and the opposite of the one above: standing at
	// the start of a blank module, EVERYTHING the configuration offers is in scope. The path door
	// refuses an empty text because there is no expression; this one answers with the ladder.
	const ibCaretCompile compiled(text, caret, moduleObject, /*upToCaret*/ false);

	// The body the caret is standing in — its locals belong to nobody else, and that is a FRAME
	// position rather than a source, so it rides beside the origin instead of inside it. The
	// compile that just ran is what knows it (ibCaretCompile::CaretFrame).
	const ibByteCode::ibByteFunction* frame = compiled.CaretFrame();

	// ⭐⭐ WHERE EACH NAME WAS DECLARED — read off the tape, not counted again. The compiler emits a
	// declarator for every named local (`OPER_FUNC_LOCAL`, compileCode.cpp) and stamps it with the
	// source position like any other instruction, so "written below the caret" is a comparison
	// rather than a second bookkeeping pass. This is the gate the precompiler spelled as `declPos`;
	// the number was already here.
	//
	// ⚠ ONLY WHAT DEPENDS ON ORDER IS GATED. Parameters are visible throughout their body, and a
	// FUNCTION is callable from above its own declaration — so neither is asked this question.
	std::map<wxLongLong_t, unsigned int> declaredAt;

	// ⭐⭐ AND WHERE A NAME THE COMPILER MADE UP FIRST APPEARS IN THE TEXT. `a = X;` with no `var`
	// is an implicit local, and the compiler deliberately writes NO declarator for one — so the
	// gate above has nothing to compare it against, and such a name was offered at EVERY caret in
	// the body, including twelve lines ABOVE the statement that creates it (Max, 2026-09-09: `a`
	// stood in the dropdown at a caret above `a = Attribute.GetMetadata()`).
	//
	// The position was never missing, only unrecorded in that one place: every instruction carries
	// the source position it was emitted from, so the FIRST instruction that mentions the cell is
	// where the name first appears. The MINIMUM rather than the first one met, because a query's
	// instructions are not emitted in text order (a join's build loop is emitted after the clauses
	// that read its alias) — taking the first met would date such a name by its loop header.
	std::map<wxLongLong_t, unsigned int> firstTouchAt;
	{
		const ibByteCode& bc = compiled.ByteCode();
		long openEntry = -1;
		std::vector<long> openStack;

		for (size_t ip = 0; ip < bc.m_listCode.size(); ip++) {

			const ibByteUnit& code = bc.m_listCode[ip];
			const short oper = code.m_numOper % TYPE_DELTA1;

			if (oper == OPER_FUNC || oper == OPER_LFUNC) {
				openStack.push_back((long)ip);
				openEntry = (long)ip;
				continue;
			}
			if (oper == OPER_ENDFUNC || oper == OPER_ENDLFUNC) {
				if (!openStack.empty()) openStack.pop_back();
				openEntry = openStack.empty() ? -1 : openStack.back();
				continue;
			}

			// The module's own table, or the body the caret stands in — the two the list reads.
			const bool mine = (openEntry < 0)
				|| (frame != nullptr && openEntry == frame->m_lCodeLine);
			if (!mine)
				continue;

			if (oper == OPER_FUNC_LOCAL) {
				declaredAt.emplace(code.m_param1.m_numIndex, code.m_numString);
				continue;
			}

			// Any instruction, any operand: a cell of THIS frame that this instruction touches.
			// `FrameOf` folds the negative markers onto frame 0 the same way the walk does, so a
			// temporary and a named local are told apart by their index, as everywhere else.
			const ibParamRunUnit* const operands[4] = {
				&code.m_param1, &code.m_param2, &code.m_param3, &code.m_param4 };

			for (const ibParamRunUnit* operand : operands) {
				if (FrameOf(*operand) != 0 || operand->m_numIndex < 0)
					continue;
				const auto seen = firstTouchAt.find(operand->m_numIndex);
				if (seen == firstTouchAt.end())
					firstTouchAt.emplace(operand->m_numIndex, code.m_numString);
				else if (code.m_numString < seen->second)
					seen->second = code.m_numString;
			}
		}
	}

	// The open frame's formal parameters, by name — the one group the order gate must not judge
	// (see the exemption inside it). Read off the frame's own record, which carries them beside the
	// locals rather than mixed into them.
	const auto isAParameter = [frame](const wxString& name) {
		if (frame == nullptr)
			return false;
		for (const ibByteCode::ibByteParam& parameter : frame->m_listParam)
			if (stringUtils::CompareString(parameter.m_strName, name))
				return true;
		return false;
	};

	// ⚠ AT THE CARET COUNTS AS NOT YET DECLARED, and the boundary is where the word being typed
	// STARTS — which is exactly what Caret() is, the position backed up over the identifier under
	// the caret (ibCaretCompile). Without the equality the half-written word offers ITSELF: typing
	// `cat` compiles as a name this text declares, and the dropdown listed `cat` above `Catalogs`
	// (measured 2026-09-07). A name is not a candidate for its own completion.
	const auto writtenBelowCaret = [&](const ibByteCode::ibByteCodeVarInfo& var) {

		const auto at = declaredAt.find((wxLongLong_t)var.m_slotIndex);
		if (at != declaredAt.end())
			return at->second >= compiled.Caret();

		// ⭐ A NAME WITH NO DECLARATOR IS ONE THE COMPILER MADE UP — `a = X;` with no `var`, and
		// equally the word being typed, since `cat` alone on a line is a statement and an unknown
		// identifier in a statement gets an implicit variable. The compiler says so where it emits
		// declarators and skips it ("implicit creates from GetVariable's fallback don't get a tape
		// declarator").
		//
		// ⭐⭐ SO ITS POSITION IS ASKED OF THE TAPE INSTEAD, and the order gate is the SAME gate: an
		// implicit variable created BELOW the caret is not a name that can be written here, and one
		// created above it is an ordinary name that stays. Reading it off the first instruction
		// that mentions the cell is what makes the two tellable apart at all — without it the only
		// discriminator left was the typed word, so every implicit variable in the body was offered
		// at every caret above its own creation (measured 2026-09-09).
		//
		// 🛑 A PARAMETER IS EXEMPT, AND NOT AS A SPECIAL CASE: it is visible throughout its body by
		// the language's own rule, so it does not depend on order and must not be asked an order
		// question. It reaches here only because the frame's locals table holds the parameters too
		// (compileCode.cpp writes every non-temporary of the function context into m_listLocals) —
		// and it has no tape declarator either, so without this it would have been dated by its
		// FIRST USE and hidden from every caret above that.
		if (!isAParameter(var.m_strRealName)) {
			const auto touched = firstTouchAt.find((wxLongLong_t)var.m_slotIndex);
			if (touched != firstTouchAt.end())
				return touched->second >= compiled.Caret();
		}

		// Nothing on the tape mentions the cell: all that is left to say is whether this is the
		// half-written word itself, which is never a candidate for its own completion — typing
		// `cat` listed `cat` above `Catalogs` (measured 2026-09-07).
		return !compiled.TypedWord().IsEmpty()
			&& stringUtils::CompareString(var.m_strRealName, compiled.TypedWord());
	};

	// Is this spelling one that a query in THIS text bound? The queries are on the tape's own record
	// (byteCodeLINQ.h), so this asks the compile rather than re-reading the text. Used with the
	// declarator test above — see there for why one alone is not enough.
	const auto boundByAQuery = [&compiled](const wxString& name) {
		for (const ibLinqQuery& query : compiled.FullByteCode().m_listLinq)
			for (const ibLinqBinding& binding : query.m_bindings)
				if (stringUtils::CompareString(binding.name, name))
					return true;
		return false;
	};

	// ⭐⭐ ONE NAME, ONE ENTRY — AND THE NEAREST LINK WINS. The ladder is walked from the text
	// outward, and a name that exists on more than one rung is not two names: it is one name that
	// the nearer rung SHADOWS, exactly as the runtime resolves it. Without this the dropdown
	// printed the same word twice with the same icon, and a reader could only wonder which to pick.
	//
	// Measured 2026-09-08, from Max's own screen: an external data processor's module is one rung
	// further out than a configuration module, so `Data` and `Metadata` — reachable from both —
	// were offered twice. My own texts, compiled with a shorter chain, never showed it.
	//
	// ⚠ CASE-INSENSITIVELY, because the language is: `Data` and `data` are the same name here, and
	// offering both would be the same defect wearing different letters.
	std::set<wxString> alreadyOffered;
	const auto offerOnce = [&alreadyOffered, &outNames](ibCaretName&& entry) {
		if (entry.m_name.IsEmpty())
			return;
		if (!alreadyOffered.insert(entry.m_name.Lower()).second)
			return;
		outNames.push_back(std::move(entry));
	};

	const ibCompileChain& chain = compiled.Chain();

	for (size_t depth = 0; depth < chain.size(); depth++) {

		const ibCompileCode* compile = chain[depth];
		const ibByteCode& byteCode = compile->m_cByteCode;

		// A GLOBAL common module's exports are reachable with no prefix at all — the one origin
		// that cannot be told from the kind, and the class itself is what says it.
		const bool globalModule = dynamic_cast<const ibCompileGlobalModule*>(compile) != nullptr;

		for (const auto& var : byteCode.m_listVar) {

			if (var.m_strRealName.IsEmpty())
				continue;

			// Only this text's own names can be written below the caret; a module above it was
			// finished long ago.
			if (depth == 0 && writtenBelowCaret(var))
				continue;

			// ⭐⭐ A NAME A QUERY BOUND IS NOT A LOCAL OF THIS BODY, however much the symbol table
			// looks like it holds one. `from o in Goods select o;` puts `o` in m_listVar because the
			// query's own scope is a RETURN_BLOCK context and AddVariable there delegates upward to
			// the enclosing frame — but the compiler declines to write a tape declarator for exactly
			// those (compileCode.cpp: "block-scope @context vars are not real frame slots"), and
			// that missing declarator is the discriminator. So the name is offered by the query
			// block below, which knows the span it is alive in, and NOT from here, where it would be
			// alive for the rest of the module. Measured 2026-09-08: `o` was offered on the line
			// AFTER the query that bound it.
			//
			// ⚠ Both halves are required. A name with no declarator can also be an implicit variable
			// (`x = 1` with no `var`), which is a real local; and a real local may legitimately share
			// a query alias's spelling, in which case it HAS a declarator and stays.
			if (depth == 0 && boundByAQuery(var.m_strRealName)
				&& declaredAt.find((wxLongLong_t)var.m_slotIndex) == declaredAt.end())
				continue;

			// ⭐⭐ A PARENT'S PRIVATE LOCAL IS NOT IN SCOPE HERE, and the runtime already says so in
			// one line: reaching a module above is a cross-bytecode lookup, and
			// `ibByteCode::FindVariable` opens with `if (v.IsLocal()) return false;`. Everything
			// else passes — Export, PROTECTED (which exists to be visible to the modules below),
			// External, Context, ContextProp — so this is the ladder as the language draws it
			// rather than a list to keep in step. Measured 2026-09-07 on this configuration:
			// `Procedure BeforeStart(Cancel)` in the ConfigurationModule carries no `Public`, and
			// the list offered it inside a document's object module — a name the compiler would
			// have refused had anyone written it.
			if (depth > 0 && var.IsLocal())
				continue;

			// 🛑 A TRANSPARENT SCOPE CONTAINER IS NOT A NAME. `Manager` exists so that what it
			// holds surfaces into scope; writing it is not how anyone reaches those. The bytecode
			// cannot tell it from `ThisObject` — both are kind=Context, and the flag was always an
			// editor-facing fact — so it is asked of the module that was TOLD it, which is the one
			// holding the binding.
			if (var.IsContext()) {
				const auto bound = compile->m_listContextValue.find(var.m_strRealName);
				if (bound != compile->m_listContextValue.end() && bound->second.m_scopeContext)
					continue;
			}

			ibCaretName entry;
			entry.m_name      = var.m_strRealName;
			entry.m_exported  = var.IsExport();
			entry.m_protected = var.IsProtected();
			entry.m_origin    = OriginOfVar(var.m_kind, depth, chain.size(), globalModule);
			offerOnce(std::move(entry));
		}

		for (const auto& fn : byteCode.m_listFunc) {

			// An anonymous lambda is not a name anyone can write.
			if (fn.IsLambda() || fn.m_strRealName.IsEmpty())
				continue;

			// The same rule as for variables, and `ibByteCode::FindFunction` states it the same
			// way — `if (fn.IsLocal()) return false;`. A call is where it bites hardest: a name a
			// list offers and a call cannot reach is a suggestion that compiles to an error.
			if (depth > 0 && fn.IsLocal())
				continue;

			ibCaretName entry;
			entry.m_name          = fn.m_strRealName;
			entry.m_signature     = SignatureOf(fn, chain);
			entry.m_callable      = true;
			entry.m_returnsValue  = fn.m_bCodeRet;
			entry.m_exported      = fn.IsExport();
			entry.m_protected     = fn.IsProtected();
			entry.m_origin        = OriginOfFunc(fn.m_kind, depth, chain.size(), globalModule);
			offerOnce(std::move(entry));
		}
	}

	// THE OPEN FRAME LAST, because its names are the only ones that could be shadowed by nothing —
	// they belong to the body being typed in, and a caller reading top-down should meet them where
	// the caret is.
	if (frame != nullptr) {
		for (const auto& var : frame->m_listLocals) {
			if (var.m_strRealName.IsEmpty())
				continue;
			if (writtenBelowCaret(var))
				continue;

			ibCaretName entry;
			entry.m_name     = var.m_strRealName;
			entry.m_exported = var.IsExport();
			entry.m_origin   = ibNameOrigin::Declared;
			entry.m_local    = true;
			offerOnce(std::move(entry));
		}
	}

	// ⭐⭐ …AND THE NAMES A QUERY BINDS, WHICH ARE IN NO SYMBOL TABLE AT ALL.
	//
	// `from o in Goods where o.` — `o` is not a local of the module or of the function: it is a cell
	// with an alias pushed onto a compile scope that dies with the query, so a list read off the
	// symbol tables offers everything EXCEPT the one name the person is standing inside. It was the
	// most conspicuous hole in this answer and it could not be filled from the tape alone.
	//
	// It can now: the compiler KEEPS what it understood about each query (byteCodeLINQ.h), and both
	// numbers this needs are already written there. `foreachStartIp` is where the binding's loop
	// header sits; that header's fourth operand is where the loop ENDS — so the span a name is alive
	// in is the span between those two instructions, and every instruction carries the source
	// position it came from. A caret inside that span is a caret inside the query.
	//
	// Max, on why the tree was kept at all: *"IntelliSense can now build along the tree itself,
	// because the tree stays."*
	{
		const ibByteExtCode& full = compiled.FullByteCode();
		const unsigned int at = compiled.Caret();

		// A query the compiler never finished has no closing position, and then the name is alive to
		// the end of what was read. Named rather than written `(unsigned int)-1` at the two places
		// that need it: there the cast MEANT something instead of converting something, which is the
		// one use of a cast that a reader cannot check.
		const unsigned int kToTheEnd = std::numeric_limits<unsigned int>::max();

		const auto positionOf = [&full](long ip) -> unsigned int {
			return (ip >= 0 && (size_t)ip < full.m_listCode.size())
				? full.m_listCode[(size_t)ip].m_numString : 0;
		};

		// Which KIND of query the caret stands in, if any — the keyword block below writes the
		// clauses that are legal there, and a restriction takes a different (shorter) set than a
		// query does. Both spans come off the compile's own record of the text it read.
		bool insideQuery = false, insideRestrict = false;

		for (const ibLinqQuery& query : full.m_listLinq) {

			{
				const unsigned int from = query.m_textFrom;
				const unsigned int to = query.m_textTo > from ? query.m_textTo : kToTheEnd;
				if (at >= from && at <= to) {
					if (query.m_restrict) insideRestrict = true;
					else                  insideQuery = true;
				}
			}

			for (const ibLinqBinding& binding : query.m_bindings) {

				if (binding.name.IsEmpty())
					continue;

				unsigned int from = 0, to = 0;

				// ⚠ A JOIN'S HEADER IS NOT ITS SCOPE. The alias now carries the header its row comes
				// out of — which is what the VALUE door walks — but that header belongs to the pass
				// that builds the join's hash and closes before `where` is even read. Measuring the
				// name's life by it would take `b` out of scope exactly where it is written, so a
				// join keeps the query's own span, below (compileCodeLINQ.cpp says the same at the
				// place the header is recorded).
				if (binding.origin != ibLinqBinding::FromJoin
					&& binding.foreachStartIp >= 0
					&& (size_t)binding.foreachStartIp < full.m_listCode.size()) {

					// The header's own position is where the source expression was read, so the
					// name is offered from there to the loop's last instruction — a join alias is
					// not in scope above its own `join`. An unclosed loop (the text ends mid-query,
					// which is exactly when somebody is typing in one) has no end written yet, and
					// then the query reaches to the end of what was compiled.
					const ibByteUnit& header = full.m_listCode[(size_t)binding.foreachStartIp];
					const long endIp = (long)header.m_param4.m_numIndex;
					from = positionOf(binding.foreachStartIp);
					to   = endIp > binding.foreachStartIp
						? positionOf(endIp - 1) : kToTheEnd;
				}
				else if (query.m_restrict || binding.origin == ibLinqBinding::FromJoin) {
					// A RESTRICTION HAS NO LOOP, so its names are alive for as long as its TEXT is —
					// which the compiler bracketed when it read it. There is no ordering question
					// here: a restriction's alias is bound by its first clause and every clause
					// after it reads the same one.
					from = query.m_textFrom;
					to   = query.m_textTo > query.m_textFrom ? query.m_textTo : kToTheEnd;
				}
				else
					continue;                    // nothing says where this name is alive

				if (at < from || at > to)
					continue;

				ibCaretName entry;
				entry.m_name   = binding.name;
				entry.m_origin = ibNameOrigin::Declared;
				entry.m_local  = true;
				offerOnce(std::move(entry));
			}
		}

		// ⭐⭐ …AND THE WORDS THEMSELVES. A query in this language is written in KEYWORDS — the
		// method chain is the other spelling of the same thing, not the primary one — so a list
		// that offers only names leaves the person who is standing inside a `from` with nothing to
		// pick and a grammar to remember. Which words are offered is decided by WHERE the caret is,
		// which the spans above have just established: the clauses of a query inside one, the three
		// a restriction takes inside that, and the two openers everywhere else.
		//
		// 🛑 MODIFIERS ARE NOT OFFERED — `by`, `into`, `on`, `equals`, `ascending`, `descending`.
		// Each is written as part of a clause it cannot be separated from (`group X by K`), so
		// offering it standalone would suggest a line that does not compile. The clause forms
		// themselves are what `linq_methods` hands back, and that is where a whole grammar belongs.
		{
			const auto offerKeyword = [&offerOnce](int key) {
				const wxString word = ibTranslateCode::GetKeyWord(key);
				if (word.IsEmpty())
					return;
				ibCaretName entry;
				entry.m_name      = word;
				entry.m_signature = s_listKeyWord[key].m_strShortDescription;
				entry.m_origin    = ibNameOrigin::Keyword;
				offerOnce(std::move(entry));
			};

			// ⭐⭐ WHICH WORD MAY BE WRITTEN HERE DEPENDS ON THE LAST ONE THAT WAS. A query is a
			// SEQUENCE of clauses, and offering the whole set at every position says `join` after
			// `select` and `where` where the grammar wants `in` — a list that has to be filtered by
			// the reader is barely better than no list. The last query keyword before the caret is
			// the whole of what this needs, and it is read off the compile's own token stream.
			const int spoken = [&]() -> int {
				int last = -1;
				for (const ibLexem& lex : compiled.Lexems()) {
					if (lex.m_numString >= at)
						break;
					if (lex.m_lexType != KEYWORD)
						continue;
					switch (lex.m_numData) {
					case KEY_FROM: case KEY_IN: case KEY_JOIN: case KEY_ON: case KEY_EQUALS:
					case KEY_WHERE: case KEY_GROUP: case KEY_BY: case KEY_INTO:
					case KEY_ORDERBY: case KEY_ASCENDING: case KEY_DESCENDING:
					case KEY_TAKE: case KEY_SKIP: case KEY_DISTINCT: case KEY_SELECT:
					case KEY_RESTRICT:
						last = lex.m_numData;
						break;
					default: break;
					}
				}
				return last;
			}();

			// A source is being named (`from o |`, `join b |`) — the language wants `in` and
			// nothing else, and after `on <key> |` it wants `equals`. These are the positions where
			// a full clause list is not merely noisy but wrong.
			const bool wantsIn     = (spoken == KEY_FROM || spoken == KEY_JOIN);
			const bool wantsEquals = (spoken == KEY_ON);
			const bool wantsBy     = (spoken == KEY_GROUP);

			if (wantsIn)
				offerKeyword(KEY_IN);
			else if (wantsEquals)
				offerKeyword(KEY_EQUALS);
			else if (wantsBy)
				offerKeyword(KEY_BY);
			else if (insideRestrict) {
				for (const int key : { KEY_JOIN, KEY_WHERE })
					offerKeyword(key);
			}
			else if (insideQuery) {
				// The clauses that may still follow. `select` closes the query, so after it only
				// `distinct` remains; `into` belongs to a `group` that has its key.
				if (spoken == KEY_SELECT)
					offerKeyword(KEY_DISTINCT);
				else if (spoken == KEY_BY)
					for (const int key : { KEY_INTO, KEY_ORDERBY, KEY_SELECT })
						offerKeyword(key);
				else
					// `from` again: a second source is how this language spells a cross product.
					for (const int key : { KEY_WHERE, KEY_SELECT, KEY_ORDERBY, KEY_GROUP, KEY_JOIN,
					                       KEY_TAKE, KEY_SKIP, KEY_FROM })
						offerKeyword(key);
			}
			else {
				for (const int key : { KEY_FROM, KEY_RESTRICT })
					offerKeyword(key);
			}

			// ⭐⭐ …AND THE REST OF THE LANGUAGE'S WORDS, FROM HERE TOO — because there must be ONE
			// place that answers "what may be written", and the editor was a second one: it walked
			// s_listKeyWord itself and appended every word before asking this function for the
			// names. With the query clauses answered here that became visible as a plain
			// duplicate — `From` twice in the dropdown, same icon, same word (Max's screen,
			// 2026-09-08) — but the doubling was only the symptom. The disagreement was the defect:
			// this side knows WHERE the caret is and could withhold `equals` outside a join, while
			// the editor's list could not, and the MCP door got no keywords at all.
			//
			// The query words are skipped here: the block above has already decided which of them
			// this position admits, and offerOnce would keep the first answer anyway.
			//
			// 🛑 AND NOT INSIDE A QUERY AT ALL. A query is an EXPRESSION — `if`, `while`,
			// `Procedure`, `Return` cannot be written in the middle of one, and offering them there
			// says a line that will not compile. Inside, the clauses above are the whole answer;
			// the rest of the language resumes where the query ends.
			if (!insideQuery && !insideRestrict) {
				for (int key = 0; key < LastKeyWord; key++) {
					if (key >= KEY_FROM && key <= KEY_RESTRICT)
						continue;
					offerKeyword(key);
				}
			}
		}
	}

	return true;
}

//---------------------------------------------------------------------------
// The same compile, asked what it UNDERSTOOD about a QUERY.
//---------------------------------------------------------------------------

std::vector<ibQueryOutline> ibOutlineScriptQueries(const wxString& text, const wxString& moduleName,
	const ibMetaData* metaData)
{
	std::vector<ibQueryOutline> outlines;

	// The walk below reaches into the platform to ask a source for a sample of what it yields, the
	// same way the caret's own question does — so it answers under the same mode, and a refusal
	// down there goes to whoever asked rather than to the designer's message pane.
	const ibBackendException::ibEvalModeScope answering{ eval_complete };

	// ⚠ COMPILED AND DISCARDED — and the answer is taken BEFORE the compiler dies, because the tree
	// lives on the compiler's own bytecode and nowhere else. That is the whole arrangement: the
	// runtime's type has no such member (byteCode.h), so nothing downstream could hand it back.
	//
	// ⭐ TOLERANTLY, because a person writing a query is mid-text by definition. The queries read
	// before the compiler stopped are still what it understood, and a refusal is a question for
	// `ibCheckScript`, which is the door that answers it.
	ibCompileCode compiler(moduleName, wxT("outline"), false);
	compiler.SetCompileMode(ibCompileCode::ibCompileMode::Tolerant);

	ibCompileChain chain;
	chain.push_back(&compiler);

	// ⭐ NOT IN A VACUUM, when a configuration was named — the module manager it compiles against is
	// the context every module of it is parented to, and without it `Catalogs` is not a name. Same
	// reasoning, and the same trap, as ibCheckScript documents: a manager nobody compiled carries
	// an empty bytecode, so it is compiled first.
	if (metaData != nullptr) {
		if (ibValueModuleManager* manager = ibSession::EditModuleManagerFor(metaData)) {
			if (ibCompileModule* host = manager->GetCompileModule()) {
				try { host->Compile(); }
				catch (const ibBackendException&) { /* the snippet's own context, best effort */ }
				compiler.SetParent(host);
				for (const ibCompileModule* up = host; up != nullptr; up = up->GetParent())
					chain.push_back(up);
			}
		}
	}

	// A tolerant compile reports and keeps reading, so an ordinary refusal raises nothing. What CAN
	// still come out of here is the compiler's own structural refusals — recursion guards and the
	// like — and this door is called from the MCP server's thread, where taking the caller down to
	// report a malformed text is the wrong trade. Whatever WAS read is still the answer.
	try { compiler.Compile(text); }
	catch (const ibBackendException&) {}
	catch (...) {}

	for (const ibLinqQuery& query : compiler.m_cByteCode.m_listLinq) {

		ibQueryOutline outline;
		outline.m_columns          = query.m_columns;
		outline.m_isRestrict       = query.m_restrict;
		outline.m_groups           = query.m_grouped;
		outline.m_groupInto        = query.m_groupIntoName;
		outline.m_orders           = query.m_hasOrderBy;
		outline.m_orderDescending  = query.m_orderByDescending;

		// The query as it was written — see ibQueryOutline::m_text. Bounds-checked because a
		// tolerant compile may have stopped mid-query, and then there is no closing position yet.
		outline.m_textFrom = query.m_textFrom;
		outline.m_textTo   = query.m_textTo;
		if (query.m_textTo > query.m_textFrom && query.m_textTo <= (unsigned int)text.length())
			outline.m_text = text.Mid(query.m_textFrom, query.m_textTo - query.m_textFrom);
		else if (query.m_textFrom < (unsigned int)text.length())
			outline.m_text = text.Mid(query.m_textFrom);   // unfinished: what there is of it

		// The SPAN reaches the next token so that a caret in the whitespace after the last clause is
		// still inside the query it is being typed into (compileCode.h, FindQueryTextEnd). What is
		// SHOWN should not: the trailing blank is the gap, not the query.
		outline.m_text.Trim();

		for (const ibLinqBinding& binding : query.m_bindings) {
			ibQueryOutlineBinding said;
			said.m_name    = binding.name;
			said.m_rowCell = binding.valueSlot.m_numIndex;
			switch (binding.origin) {
			case ibLinqBinding::FromSource:   said.m_origin = wxT("from");     break;
			case ibLinqBinding::FromLet:      said.m_origin = wxT("let");      break;
			case ibLinqBinding::FromJoin:     said.m_origin = wxT("join");     break;
			case ibLinqBinding::FromGroup:    said.m_origin = wxT("group");    break;
			case ibLinqBinding::FromRestrict: said.m_origin = wxT("restrict"); break;
			}

			// ⭐⭐ AND WHAT THE NAME OFFERS, asked of the instructions rather than of the text.
			//
			// The number needed was already written down: `foreachStartIp` is where this binding's
			// loop header sits, and that header IS the producer of the row cell. So the walk starts
			// THERE — the row is resolved inside its own query and not by scanning the tape from
			// the end, which would leave the frame the query is compiled in and answer about
			// whatever else holds the same slot number.
			if (binding.foreachStartIp >= 0
				&& (size_t)binding.foreachStartIp < compiler.m_cByteCode.m_listCode.size()) {

				const ibCaretWalk walk(chain, /*frame*/ nullptr,
					(long)binding.foreachStartIp, metaData, /*firstCode*/ 0);

				ibValue sample;
				try {
					if (walk.ValueOfSlot(binding.valueSlot, sample)) {
						for (long i = 0; i < sample.GetNProps(); i++) {
							// The editor's own filter: a scope-local name belongs to the frame it
							// was declared in, not to a row reached through a source.
							if (sample.IsPropScoped(i))
								continue;
							said.m_offers.push_back(sample.GetPropName(i));
						}
					}
				}
				catch (...) {
					// The walk may end in the platform — and a platform function is not run while
					// designing (see the header). A name with nothing listed under it is a smaller
					// answer, not a wrong one.
				}
			}

			outline.m_bindings.push_back(said);
		}

		outlines.push_back(outline);
	}

	return outlines;
}
