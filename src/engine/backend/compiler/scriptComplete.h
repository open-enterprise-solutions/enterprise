#ifndef _IB_SCRIPT_COMPLETE_H_
#define _IB_SCRIPT_COMPLETE_H_

////////////////////////////////////////////////////////////////////////////
//	Description : the VALUE an expression has arrived at, at a caret
////////////////////////////////////////////////////////////////////////////
//
// WHAT THE EDITOR IS ACTUALLY ASKING after a dot. Not "what type is this" and not "what does the
// grammar allow here" — it holds a VALUE and asks that value what it offers, which is why a type a
// plugin registered answers exactly as well as a built-in one.
//
// ⭐⭐ AND IT IS THE SAME COMPILER, NOT A SECOND READER. The text is compiled — tolerantly, so a
// refusal ends its declaration rather than the module — and the walk then steps over the
// INSTRUCTIONS that compile produced. `Catalogs.Goods.` emits "read the scope member Catalogs" and
// "get the attribute Goods" before the trailing dot refuses, so what the caret stands on is already
// there to be read. Nothing re-reads the text, and nothing re-implements the language: this is the
// arc's whole point — one place to fix, and the editor cannot drift from the compiler because it IS
// the compiler (compiler-ast-arc.md § Update 2026-09-07).
//
// ⚠ IT MAY EXECUTE, AND THE LINE IS DRAWN IN ONE SENTENCE: a method on a value that is already in
// hand may be called; a BARE NAME call is a user function and is never run. That is the rule the
// reverted arc arrived at too, and it is a rule rather than a list of opcodes because a list has to
// be kept in step with the language and a rule does not. Arguments are passed as empty slots — the
// methods this reaches (Select, Get, CreateElement …) answer with their SHAPE regardless, and
// evaluating an argument would mean running arbitrary code for a question the editor asked
// speculatively.
//
// 🛑 A PLATFORM FUNCTION IS NOT RUN IN THE DESIGNER, and that is a wall somebody built on purpose,
// not a gap here: `ibValueSystemFunction::CallAsFunc` opens with `if (!appData->DesignerMode())`,
// because the designer has no runtime. So a chain that passes through `CurrentDate()`,
// `String(x)` or `GetCommonTemplate(…)` ends there while designing, and the same walk answers it
// where a runtime exists. Measured 2026-09-07: this accounts for most of what still refuses in real
// modules — and the walk reaches the call correctly, resolves the receiver (SystemManager), finds
// the method and its arity; the dispatcher declines. Do not look for the defect in the walk.
//
// ⭐⭐ …BUT A USER FUNCTION IS STILL WALKED THROUGH, and the difference is the whole point: running
// it and READING it are not the same act. `v = B(); v.` is answered by stepping into B's
// instructions, finding what B returns and resolving THAT — one level down, in B's own frame, with
// nothing executed. So `A calls B, and I have B's value` holds all the way down a chain of calls
// (Max, 2026-09-07: *"that way I can literally compute the whole cycle"*), while the rule above
// still stands: nothing user-written is ever run.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"
#include "backend/clsid.h"
#include "backend/compiler/value.h"   // a branch HOLDS its value, and holding it is owning it

#include <wx/string.h>

#include <vector>

class ibValueMetaObject;

// ⭐⭐ WHERE A NAME CAME FROM — and it is READ, not stamped. Every distinction below is already two
// facts the bytecode carries: the entry's KIND (ibVarKind / ibFnKind — local, export, external,
// context, a context's property) and WHICH module of the chain held it. The precompiler carried an
// enum of its own, set by hand at fourteen injection sites; that enum said the same thing a second
// time, and a second saying is a thing to keep in step.
//
// WHY IT IS ANSWERED AT ALL: most of these names are identical at every caret in every module of
// every configuration. The few that make THIS place different are what somebody is looking for, and
// without the origin they sit in the middle of the alphabet indistinguishable from `Chars`.
enum class ibNameOrigin {
	Declared = 0,   // written in this text — a `var`, a function, a parameter
	Platform,       // the manager's context: Catalogs / Documents / Enumerations, and the system functions
	Global,         // the manager's extern map: global constants, common modules reachable by name
	GlobalModule,   // exported by a GLOBAL common module — flat, reachable with no prefix
	Context,        // a context bind on this module's descriptor: ThisObject / ThisForm
	Bound,          // an export or local bind: RegisterRecords / Filter / DataSource / a constant's Value
	Member,         // the object's own surface: its attributes and tabular sections
	Inherited,      // exported by a module ABOVE this one in the descriptor chain
};

// One name in scope. `m_local` is deliberately NOT an origin: origin says where the name came FROM,
// local says it belongs to the body the caret stands in rather than to the module. Folding one into
// the other would make a frame position look like a source.
//
// ⭐ ACCESS RIDES BESIDE THE ORIGIN FOR THE SAME REASON. Exported and protected say WHO MAY WRITE
// this name, not where it came from — a protected function of the module above and an exported one
// arrive from the same place and differ only in how far they reach. Both are read off the entry's
// kind (ibVarKind / ibFnKind), so neither is a second saying of anything.
struct ibCaretName {
	wxString     m_name;
	wxString     m_signature;              // callables only — the call form, built from the declared parameters
	ibNameOrigin m_origin = ibNameOrigin::Declared;
	bool         m_callable = false;
	bool         m_returnsValue = false;   // a procedure where a value is wanted is a compile error
	bool         m_exported = false;
	bool         m_protected = false;      // visible to the modules below, not to the configuration
	bool         m_local = false;
};

// ⭐⭐ ONE BRANCH OF THE ANSWER: what the path arrived at, and BY WHICH ROAD — in the same words the
// name list uses, because it is the same question asked one hop further along.
//
// The origin is READ OFF THE STEP that produced the value, never decided afterwards by looking at
// what came out: the step is the only place that still knows. `Documents.GoodsIssue` and a form's
// `ThisForm` and a document's own `Warehouse` all arrive as values that describe themselves
// identically — a manager, a form, a reference — while the difference a reader wants is WHOSE they
// are: the platform's, this module's binding, or the configuration object's own data.
//
//   Member   — the METADATA declares this field on the owner (an attribute, a dimension, a resource)
//   Bound    — the module's own: an export or a native bind (ThisObject.RegisterRecords, Filter)
//   Context  — a context handle of the module the caret stands in (ThisObject / ThisForm)
//   Platform — the value's built-in surface, identical in every configuration
//
// 🛑 A BRANCH IS BUILT WHERE IT WILL LIVE. A copy of an ibValue that holds an object points AT THE
// SOURCE and takes a reference on it (value.cpp, Copy) — so a branch assembled in a local and then
// pushed would point at a stack slot. The owning constructor is here for exactly that reason: the
// value is made IN the element, from the pointer, and never travels.
struct ibCaretValue {
	ibCaretValue() = default;
	ibCaretValue(ibValue* owned, ibNameOrigin origin)
		: m_value(owned), m_origin(origin) {}

	ibValue      m_value;
	ibNameOrigin m_origin = ibNameOrigin::Platform;
};

// The value the expression at `caret` arrived at, or false when the caret stands on nothing that
// resolves — an empty module, a name the configuration does not know, a step the walk refuses.
//
// `moduleObject` is the module the text LIVES IN, and it is PASSED rather than reached for: several
// configurations can be open and the active one is not necessarily the caller's. It is the whole
// context — its own bindings (ThisObject, the object's own names) and, through its parent chain,
// the common modules and the globals. A caller with no module in mind emulates one carrying the
// text (ibScratchModule, mcpToolComplete.cpp); null answers about the bare language, which is the
// most that can honestly be said about a text with no home.
// ⭐⭐ AND THE BREAK COMES BACK. `outRefusal` is what the compile refused, one line per refusal with a
// position on each — enough to see roughly where the chain stopped. A refusal used to travel on an
// exception and reach the caller that way; a tolerant compile raises nothing, so it is handed back
// instead (Max, 2026-09-07: *"only the break you must return back to yourself"*).
//
// An EMPTY string with a false return is itself an answer: the text compiled and the WALK stopped,
// which is a different fault from a text that would not compile.
// ⭐⭐ AND IT IS A LIST, BECAUSE A PATH CAN ARRIVE AT MORE THAN ONE THING. A field declared with a
// COMPOSITE type is one name in the text and several things to walk into, so the branches are
// computed SIDE BY SIDE and all of them come back (Max, 2026-09-08: *"there you have to compute in
// parallel"*). The set WIDENS at such a field and NARROWS at every field after it — a branch that
// has not got the next one drops out, which is what makes the end of a long path unambiguous again:
//
//     value(composite) . field1(both have it) . field2(also) . field3(only one, so one remains)
//
// Two references are two answers; two dates are two dates. A dropdown merges them and a caller that
// means to step further picks one — but neither is made to guess which branch it was given.
//
// Empty with a false return means nothing resolved; one entry is the ordinary case.
BACKEND_API bool ibValueAtCaret(const wxString& text, unsigned int caret,
	const ibValueMetaObject* moduleObject, std::vector<ibCaretValue>& outValues,
	wxString* outRefusal = nullptr);

// ONE DECLARED TYPE OF A FIELD: the class the metadata registered, and the name a script writes for
// it. Both, because the two readers want different halves and neither can recover the other's — a
// caller listing names cannot turn a class id into `CatalogRef.Counterparties` without the same
// lookup, and the walk cannot make a value out of a name.
//
// 🛑 THE NAME IS ASKED OF WHOEVER REGISTERED THE TYPE. `ibValue::GetNameObjectFromID` goes through
// the PLATFORM registry, which does not know configuration types at all — and it does not answer
// "no", it THROWS (`Object with id '…' is not exist`, valueFactory.cpp). Measured 2026-09-08: it
// took out every chain whose object had a reference attribute.
struct ibFieldType {
	ibClassID m_class = 0;
	wxString  m_name;
};

// ⭐⭐ WHAT A FIELD IS DECLARED AS — and it is the SAME question the walk asks when it widens, so it
// is asked in one place. A reader listing what an object offers wants the type beside each name;
// the walk wants the types to make a branch out of each. Two callers, one lookup: a composite field
// answers with several types here and becomes several branches there, and neither can drift.
//
// 🛑 ASKED OF THE METADATA, NEVER OF THE VALUE. A live object builds its source explorer on demand
// and that reaches the DATABASE — for an object with no record yet it throws and leaves the object
// half-built, so the next question about it throws too, outside any guard (measured 2026-09-08: it
// cost three working chains and a wrong revert). The declaration was never the instance's to hold.
//
// False when the owner is not a thing with fields of its own, or has no such field — a platform
// property rather than a declared one. `moduleObject` is the door to the metadata, as above.
BACKEND_API bool ibFieldTypesOf(const ibValueMetaObject* moduleObject, const ibValue& owner,
	const wxString& field, std::vector<ibFieldType>& outTypes);

// ⭐ THE OTHER HALF OF THE SAME QUESTION, off the same compile: what may be written HERE when the
// caret is not after a dot — every name in scope at that point. The two doors differ in what they
// read out of one artefact, not in how they read the text: a PATH is walked over the instructions,
// a LIST is read off the symbol tables of the same bytecode and of the modules above it.
//
// The declaration gate needs no rule of its own: every named local carries the source position of
// its declarator, so "written below the caret" is a comparison rather than a second bookkeeping
// pass. `moduleObject` means what it means above.
//
// ⭐⭐ AND THE LIST IS WHAT THE RUNTIME ITSELF SEES — asked the way the runtime asks it. Reaching a
// module above the caret is a CROSS-BYTECODE lookup, and `ibByteCode::FindVariable` settles that in
// one line (`if (v.IsLocal()) return false;`, and `FindFunction` the same for calls): a parent's
// own PRIVATE local is not a name that can be written here, while its exports and its PROTECTED
// are — protected exists precisely to be visible to the modules below. So the answer is locals,
// globals, the parent's exports and its protected, which is the ladder as the language draws it
// (Max, 2026-09-07). Listing more than that would offer names that cannot compile, and a completion
// list that suggests an error is worse than one that says nothing.
BACKEND_API bool ibNamesAtCaret(const wxString& text, unsigned int caret,
	const ibValueMetaObject* moduleObject, std::vector<ibCaretName>& outNames);

#endif
