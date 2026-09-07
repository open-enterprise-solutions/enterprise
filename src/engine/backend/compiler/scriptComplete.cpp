#include "backend/compiler/scriptComplete.h"

#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"        // and with it codeDef, value, compileContext
#include "backend/compiler/compileModule.h"      // ibCompileModule — the contextual compile
#include "backend/metaData.h"                    // ibCompileValueCache::FindCompileModule
#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject::GetMetaData
#include "backend/moduleInfo.h"                  // ibRuntimeModuleDataObject::GetCompileModule
#include "backend/diagnostics/journal.h"         // where the refusals are read out when nothing resolved
#include "backend/typeDescription.h"             // ibTypeDescription - a type, possibly several
#include "backend/objCtor.h"                     // ibCtorMetaValueType - one door: the metaobject AND the maker
#include "backend/metaCollection/metaObjectComposite.h"   // where fields live: catalog, document, register, tabular section

#include <memory>

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

			return ValuesOfSlot(code.m_param2, out);
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
	bool ValueOfSlot(const ibParamRunUnit& slot, ibValue& out, int depth = 0,
		ibNameOrigin* outOrigin = nullptr) const
	{
		std::vector<ibCaretValue> values;
		if (!ValuesOfSlot(slot, values, depth) || values.empty())
			return false;
		if (outOrigin != nullptr)
			*outOrigin = values.front().m_origin;
		out = std::move(values.front().m_value);
		return true;
	}

	// ⭐⭐ EVERYTHING `slot` RESOLVED TO. One entry is the ordinary case; several mean a composite
	// declaration was crossed on the way here, and the set travels onward rather than being
	// collapsed at the step that made it.
	bool ValuesOfSlot(const ibParamRunUnit& slot, std::vector<ibCaretValue>& out, int depth = 0) const
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

		for (long ip = m_lastCode; ip >= m_firstCode; --ip) {

			const ibByteUnit& code = m_byteCode.m_listCode[(size_t)ip];
			const short oper = code.m_numOper % TYPE_DELTA1;

			if (oper == OPER_ENDFUNC || oper == OPER_ENDLFUNC) { foreign++; continue; }
			if (oper == OPER_FUNC || oper == OPER_LFUNC) {
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
			return step == ibStep::Value;
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

		std::vector<ibCaretValue> parents;
		if (!ValuesOfSlot(code.m_param2, parents, depth + 1))
			return ibStep::Failed;

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
			if (!ValueOfSlot(code.m_param2, parent, depth + 1))
				return ibStep::Failed;

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
		case OPER_FOREACH: {
			ibValue collection;
			if (!ValueOfSlot(code.m_param2, collection, depth + 1))
				return ibStep::Failed;

			const std::shared_ptr<ibValueIteratorState> state = collection.CreateIterator();
			if (!state)
				return ibStep::Failed;

			try { return state->PeekSample(out) ? ibStep::Value : ibStep::Failed; }
			catch (...) { return ibStep::Failed; }
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
			return ValueOfSlot(code.m_param2, out, depth + 1, outOrigin) ? ibStep::Value : ibStep::Failed;

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
			return LinqStep(code, out, depth) ? ibStep::Value : ibStep::Failed;

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
			return CallStep(code, out, depth, outOrigin);

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
	bool LinqStep(const ibByteUnit& code, ibValue& out, int depth) const
	{
		ibValue receiver;
		if (!ValueOfSlot(code.m_param2, receiver, depth + 1))
			return false;

		try {
			receiver.DispatchLinqMethod(
				static_cast<ibValue::ibLinqMethod>(code.m_param3.m_numIndex), out, nullptr, 0);
			return out.GetType() != ibValueTypes::TYPE_EMPTY;
		}
		catch (...) {
			return false;   // a speculative question must not surface as an error
		}
	}

	// Walk INTO the callee: its entry is `m_param2.m_numIndex` (procUnit reads the same operand as
	// `cRunContext.m_lStart`), its body ends at its own closer, and what it hands back is the
	// operand of its `OPER_RET`. A procedure has none, and then there is nothing to step to.
	ibStep CallStep(const ibByteUnit& code, ibValue& out, int depth,
		ibNameOrigin* outOrigin = nullptr) const
	{
		const long entry = (long)code.m_param2.m_numIndex;
		if (entry < 0 || (size_t)entry >= m_byteCode.m_listCode.size())
			return ibStep::Failed;

		for (long ip = entry + 1; (size_t)ip < m_byteCode.m_listCode.size(); ip++) {

			const short oper = m_byteCode.m_listCode[(size_t)ip].m_numOper % TYPE_DELTA1;

			if (oper == OPER_ENDFUNC || oper == OPER_ENDLFUNC || oper == OPER_END)
				return ibStep::Failed;          // reached the end without a return: a procedure

			if (oper != OPER_RET)
				continue;

			// The return's own slot, resolved WITHIN the callee — bounded at its entry so a name
			// that means one thing in B is not answered from the caller's frame.
			const ibCaretWalk callee(m_chain, m_byteCode.FindFunctionByEntry(entry), ip, m_metaData, entry);
			return callee.ValueOfSlot(m_byteCode.m_listCode[(size_t)ip].m_param1, out, depth + 1, outOrigin)
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
		return numMethod != wxNOT_FOUND ? value.GetMethodHelper(numMethod) : wxEmptyString;
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

		wxString window;
		for (long ip = point.m_instruction - 4; ip <= point.m_instruction + 1; ip++) {
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

		ibJournalInfo(wxT("complete"), wxT("caret %u -> instruction %ld, %s%s%s"),
			compiled.Caret(), point.m_instruction,
			resolved ? wxT("resolved") : wxT("REFUSED"), window, walkTrace);
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
			}
			else if (oper == OPER_ENDFUNC || oper == OPER_ENDLFUNC) {
				if (!openStack.empty()) openStack.pop_back();
				openEntry = openStack.empty() ? -1 : openStack.back();
			}
			else if (oper == OPER_FUNC_LOCAL) {
				// The module's own table, or the body the caret stands in — the two the list reads.
				const bool mine = (openEntry < 0)
					|| (frame != nullptr && openEntry == frame->m_lCodeLine);
				if (mine)
					declaredAt.emplace(code.m_param1.m_numIndex, code.m_numString);
			}
		}
	}

	// ⚠ AT THE CARET COUNTS AS NOT YET DECLARED, and the boundary is where the word being typed
	// STARTS — which is exactly what Caret() is, the position backed up over the identifier under
	// the caret (ibCaretCompile). Without the equality the half-written word offers ITSELF: typing
	// `cat` compiles as a name this text declares, and the dropdown listed `cat` above `Catalogs`
	// (measured 2026-09-07). A name is not a candidate for its own completion.
	const auto writtenBelowCaret = [&](const ibByteCode::ibByteCodeVarInfo& var) {

		const auto at = declaredAt.find((wxLongLong_t)var.m_slotIndex);
		if (at != declaredAt.end())
			return at->second >= compiled.Caret();

		// ⭐ A NAME WITH NO DECLARATOR IS ONE THE COMPILER MADE UP, and the word being typed is
		// exactly that: `cat` on its own line is a statement, and an unknown identifier in a
		// statement gets an implicit variable — the compiler says so where it emits the declarator
		// and skips it ("implicit creates from GetVariable's fallback don't get a tape
		// declarator"). With no declarator there is no position, so the order gate above cannot see
		// it, and the dropdown listed `cat` above `Catalogs` — a name offering itself as its own
		// completion (measured 2026-09-07).
		//
		// The word is the discriminator, not the missing declarator: an implicit variable created
		// EARLIER in the text is a real name and stays.
		return !compiled.TypedWord().IsEmpty()
			&& stringUtils::CompareString(var.m_strRealName, compiled.TypedWord());
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
			outNames.push_back(std::move(entry));
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
			outNames.push_back(std::move(entry));
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
			outNames.push_back(std::move(entry));
		}
	}

	return true;
}
