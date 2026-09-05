#ifndef _IB_MCP_TOOL_H_
#define _IB_MCP_TOOL_H_

////////////////////////////////////////////////////////////////////////////
//	Description : a TOOL — one verb the platform offers a machine caller
////////////////////////////////////////////////////////////////////////////
//
// THE RULE THIS FILE EXISTS TO KEEP. A developer sees BUTTONS; a machine sees a
// SCHEMA — and both must reach the same verbs. Whatever is doable with the
// mouse has to be doable through here, or the parity decays with every button
// added afterwards.
//
// Which is why a tool is NOT a re-implementation of what a command does. It
// calls the SAME door the button calls. Where a designer command does bookkeeping
// of its own around that door (the metadata tree inserting its item after
// ibMetaData::CreateMetaObject, say), the answer is to move the notification
// DOWN to where both roads pass — never to copy the bookkeeping in here, which
// is how a second road is born.
//
// ⭐ A TOOL PUTS ITSELF ON THE LIST, one line at the bottom of its own file —
// the shape every type in this tree registers by (VALUE_TYPE_REGISTER,
// METADATA_TYPE_REGISTER, SHEET_FORMAT_REGISTER). So the server holds NO list:
// the syntax helper, the debugger, the query constructor and the composer each
// arrive by registering, and the server is not edited to receive them.
//
// A tool holds NO STATE. It is a door, and the thing it opens onto lives in the
// subsystem that owns the verb.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"
#include "backend/serialize/dataBuilder.h"

#include <functional>   // an argument may carry a function that writes its shape
#include <vector>

#include <wx/string.h>

// ⭐⭐ TEXT THAT GOES DOWN THE WIRE — English, never translated, and a `wxString` rather than a bare
// literal so it can be stored, formatted and compared like any other.
//
// 🛑 WHY IT IS NOT `_()`. The translation macro resolves to `wxASCII_STR` here, and that eats every
// character outside ASCII: on Windows a mark came back as `?`, on Linux the WHOLE STRING came back
// EMPTY — three tools shipped with no description at all, and only the CI contract test noticed
// (2026-09-02, `syntax_search has no description`). Nothing in this layer is read by the person at
// the designer: descriptions, argument texts and refusals are read by an assistant, in English, so
// there was never anything to translate. What IS read by a person — the lines the server publishes
// into the designer's window — stays `_()` in mcpServer.cpp, and stays ASCII.
#define ibMcpText(text) wxString(wxT(text))

class BACKEND_API ibMcpTool {
public:

	virtual ~ibMcpTool() = default;

	// ⭐ ONE ARGUMENT, DECLARED ONCE — see Arguments() below for why this is a class and not a
	// string. It is the only thing that knows the argument's name, and it does both jobs with it:
	// writes the JSON-Schema entry for the caller, and pulls the value out of what the caller sent.
	//
	// The KIND is an enumerator rather than the schema's word, so `"integer"` cannot be typed
	// `"interger"` in one of eighty-four places and reach a client as a type nobody validates.
	class BACKEND_API ibMcpArgument {
	public:

		// The four shapes an argument arrives in, plus the list. Enumerators rather than the
		// schema's own words, so `"integer"` cannot be typed `"interger"` in one of eighty places.
		// `Any` is the one that is not a shape: an argument whose type is decided by SOMETHING ELSE
		// IN THE CALL, not by this schema. metadata_set's `value` is the case it exists for — what
		// it may hold is the PROPERTY's business (a switch takes true/false, a number a number, a
		// closed set one of its words), and the argument itself cannot say which.
		//
		// ⚠ IT WAS DECLARED `Text` AND DESCRIBED AS "in its own type: true/false for a switch, a
		// number for a number". The description told the truth about the platform and the schema
		// told the truth about the gate, and they disagreed — so `value: true` was refused with
		// "takes string, and a boolean came" while the documentation beside it said to send exactly
		// that. A published shape nothing honours is worse than none: the caller acts on it.
		enum class Kind { Text, Whole, Flag, Words, Many, Node, Any };

		// What an argument that carries a STRUCTURE looks like. Not written by hand: handed a
		// function that writes an EMPTY one, which for everything in this tree is the ib*Memory
		// pair that already writes it to a file. See the note on m_shape below.
		typedef std::function<bool(ibDataValue&)> ibMcpShape;

		ibMcpArgument(const wxString& name, Kind kind, const wxString& description,
			bool required = false, const std::vector<wxString>& values = std::vector<wxString>(),
			const ibMcpShape& shape = ibMcpShape())
			: m_name(name), m_kind(kind), m_description(description)
			, m_required(required), m_values(values), m_shape(shape) {
		}

		const wxString& Name() const { return m_name; }

		// WHAT SHAPE IT WAS DECLARED AS, and the closed set of words when it has one. Read by the
		// gate that holds the published schema to its word (ibMcpArgumentFault) — a declaration
		// nothing enforces is not a contract, it is a claim the caller acts on.
		Kind KindOf() const { return m_kind; }
		const std::vector<wxString>& Values() const { return m_values; }

		// WHAT IT IS FOR, in the caller's words. Read by the finder, which searches a tool by
		// everything it says about itself: half of what a verb does is said in its arguments and
		// nowhere else, and a finder blind to them knows the tool less well than its own answer does.
		const wxString& Description() const { return m_description; }

		bool IsRequired() const { return m_required; }

		// Into the schema ROOT — the frame is this class's business too: `type: object`, the
		// `properties` child, and the `required` array, which used to be three more lines in every
		// tool and a second place to spell a name that is already spelled.
		void Declare(ibDataNode& schema) const;

		// Out of what the caller sent, by the same name it was declared under.
		bool     Given(const ibDataNode& params) const;
		wxString Text(const ibDataNode& params) const;
		s64      Whole(const ibDataNode& params) const;
		bool     Flag(const ibDataNode& params) const;

	private:
		wxString              m_name;
		Kind                  m_kind;
		wxString              m_description;
		bool                  m_required;
		std::vector<wxString> m_values;   // the closed set, when the argument has one

		// ⭐⭐ THE SHAPE OF A STRUCTURED ARGUMENT — and it is not written here either.
		//
		// An argument declared `object` tells a caller a structure goes there and nothing about
		// which one. Writing that shape out by hand would be the same defect one layer up from the
		// one this class removed: a second description of something already described, drifting the
		// moment the real one gains a field.
		//
		// So the shape is produced the way anything in this tree is asked what it holds — build an
		// EMPTY one and let it write itself (Max, 2026-09-01: *"you need a type — you create an
		// empty one, write it into your stream, and you have the list of what it accepts"*). For a
		// description that is its ib*Memory::WriteNode; the schema then carries a real, empty
		// instance as the example, which is also exactly what ReadNode will take back.
		ibMcpShape            m_shape;
	};

	// The name the caller invokes, in the protocol's own spelling
	// (lower_snake_case): "script_check", "metadata_list".
	virtual wxString GetName() const = 0;

	// ⭐⭐ DOES THIS VERB BELONG ON THE MAIN THREAD? Almost every one does: it touches the
	// configuration, the designer's objects, a window — all of which are the main thread's, which
	// is why the server hands tools to it (ibMcpServer::RunTool).
	//
	// 🛑 AND ONE KIND MUST NOT BE THERE: a tool that WAITS for something the main thread itself has
	// to deliver. The debugger's evaluation is exactly that — the request goes down the socket and
	// the answer comes back through wxTheApp::CallAfter, so waiting for it ON the main thread is
	// waiting for a message only that thread can dispatch. It never arrives; the wait always
	// expires, and the caller is told "the runtime did not answer in time" about a runtime that
	// answered at once (2026-09-01, measured at two different stops with a constant expression).
	//
	// A deadlock by construction cannot be fixed by a longer deadline; the tool has to say where it
	// may run, and this is where it says it.
	virtual bool NeedsMainThread() const { return true; }

	// One sentence, written for the CALLER — it is what a machine reads to
	// decide whether this is the verb it wants.
	virtual wxString GetDescription() const = 0;

	// ⭐⭐ WHAT THIS VERB TAKES — declared ONCE, and read through the same declaration.
	//
	// An argument's name used to be written twice: in DescribeInput, for the caller, and again in
	// Call, as the key handed to params.GetValue. Two spellings of one fact, in two functions, with
	// nothing keeping them equal — and both ways of drifting are SILENT. Declare `path` and read
	// `filePath` and the argument the caller was told to send is never read; the undeclared-argument
	// gate (mcpServer.cpp) meanwhile refuses the one the tool actually wants, because that gate is
	// asked of the declaration. The tool answers as if it had been given nothing.
	//
	// So an argument is a THING, not a string: ibMcpArgument below carries the name, the type, the
	// sentence and whether it is required, and it both DECLARES itself into the schema and READS
	// itself out of the params. The name exists in one place, so there is nothing to keep in step.
	//
	// A tool lists them here and gets its schema for free.
	virtual const std::vector<ibMcpArgument>& Arguments() const;

	// The input contract as JSON Schema, filled into the node (the provider renders it).
	//
	// The default writes it from Arguments(), which is what a tool should need. Overridden only by
	// the few whose input is not a flat list of named values — a nested object, an array of
	// records — where the shape itself is the thing being described.
	virtual void DescribeInput(ibDataNode& schema) const;

	// DO IT. `params` is what the caller sent; fill `result` with the answer.
	//
	// Answer false for a REFUSAL — a name that does not resolve, an argument
	// that makes no sense, an action this session may not take — and put the
	// reason in `refusal`, in words a person could act on. A refusal is not an
	// exception: it is an ordinary answer, and the caller is a machine that
	// will try something else.
	virtual bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const = 0;

	// WHAT TO SAY IN THE WINDOW A PERSON IS WATCHING. Not the protocol name —
	// `metadata_create` is what a machine calls this, and it tells the developer
	// sitting in the designer nothing about what is happening to their
	// configuration. A sentence, in their language, naming the thing being acted
	// on: "creating catalog Goods", "reading the journal".
	//
	// ASKED OF THE TOOL because the tool is the only thing that knows what its
	// own arguments mean. A central table of phrases would have to be kept in
	// step by hand, and the first tool added after it would be missing.
	//
	// The default names the tool, which is better than nothing and worse than an
	// override; every tool a person will actually watch should have one.
	virtual wxString GetActivity(const ibDataNode& params) const { return GetName(); }

	// ⭐ THE HEADLINE ABOVE, AND THIS IS WHAT IS UNDER IT. "Writing 40 lines into the module
	// 'ObjectModule'" says something happened and nothing about WHAT — and the person watching is
	// the developer whose configuration it happened to. They want to read the code, the way they
	// would read it in the editor.
	//
	// ⚠ TWO QUESTIONS, NOT ONE LONGER ANSWER. A headline is scanned; a detail is read. Folding the
	// text into GetActivity would make every line in that window as tall as its largest argument,
	// and the window is a running log — the one shape it cannot afford.
	//
	// ASKED OF THE TOOL, like the headline, because only it knows which of its arguments is the
	// SUBSTANCE — the module text, the note being written — and deserves to be shown as the editor
	// would show it. That reasoning is why this is virtual, and it was also why it defaulted to
	// nothing: a server-side rule that guessed at substance would print a search query as if it
	// were a change.
	//
	// 🛑 AND THE DEFAULT WAS WRONG ANYWAY. Three tools of seventy overrode it, so sixty-seven
	// headlines had nothing under them at all: "listing what it can do" three times in a row, with
	// no way to tell which object, which kind, which id. The mechanism was built and connected
	// almost nowhere — and a manual list of overrides is exactly the shape that stays at three.
	//
	// ⭐ SO THE DEFAULT STOPPED GUESSING AND STARTED QUOTING. It shows the arguments the call
	// actually carried, which is not a guess about substance — it is the input, verbatim. A tool
	// with something better to show still overrides; nothing has to.
	//
	// Returns MARKDOWN — the transcript renders it.
	virtual wxString GetDetail(const ibDataNode& params) const;

	// ⭐ IS THIS ONE HANDED OVER AT THE HANDSHAKE, or found when it is wanted?
	//
	// Sixty-seven tools with their schemas is some fifty kilobytes a client must take in before
	// it can do anything at all — spent in full on every session, to be used a dozen tools at a
	// time. So `tools/list` answers with the few that let a caller FIND the rest, and the rest
	// are fetched by name when they are wanted (mcp_search) and invoked through one door
	// (mcp_call, unwrapped by the server so everything downstream is unchanged).
	//
	// ⚠ THIS IS TRANSPORT, NOT MEANING. The narrow question is what makes a refusal visible, and
	// nothing here widens it: the tool that answers is still the specific one, refusing in its own
	// words. What is shared is the envelope, and only that.
	//
	// The default is false — a tool is found, not announced. Overridden by the two that do the
	// finding, since a caller with neither has no way in.
	virtual bool IsAlwaysListed() const { return false; }

	// ⭐⭐ WHAT THIS TOOL KNOWS, for the finder to search — over and above its description.
	//
	// A description says what a VERB does, in the words of this platform. A caller arrives with the
	// words of the JOB, and for most tools the two meet: somebody looking for "lock" finds
	// `lock_list` because the description is about locks. It fails exactly where the tool is a DOOR
	// ONTO A CORPUS, because then the description is about the door.
	//
	// 🛑 MEASURED, and it was the worst possible miss (Claude Code, 2026-09-02, first session):
	// `mcp_search "stock balance"` answered `query_sources` and NOT `pattern_read` — whose `shapes`
	// entry says, in those words, that "what is left" is an AccumulationRegister with RegisterType
	// = balances and that a balance must never be summed by hand. `mcp_search "print"` answered
	// six spreadsheet verbs and not the `printing` pattern. The one thing that would have kept a
	// newcomer from building a stock ledger out of a catalogue was unreachable by every word they
	// would have used to look for it.
	//
	// So a tool that CARRIES text answers with it here, and the finder matches against that too.
	// Not a keyword list written beside the corpus — the corpus itself, so a pattern added
	// tomorrow is findable by its own words the day it is written and nobody has to remember.
	//
	// ⚠ DECLARED LAST ON PURPOSE. An optional virtual added in the middle of this class moves every
	// slot below it, and a partial build then links objects that disagree about the layout — the
	// vtable skew whose symptom is a stack that will not unwind. Last, it only ever appends.
	virtual wxString GetSearchText() const { return wxEmptyString; }

	// ⭐⭐ WHAT IS INSIDE THIS TOOL, AS ADDRESSES — so one search answers both kinds of question.
	//
	// There were two doors: `mcp_search` found VERBS, `pattern_read {query}` found PLACES IN THE
	// CORPUS, and a caller had to know which of the two their question was before asking it. That
	// is a choice nobody can make correctly from outside: "how do I print a report" is both, and
	// "how much is left" is only the second — the very question a newcomer arrives with.
	//
	// So a tool that carries a body of text can also say WHERE in it the answer lives, and the
	// finder asks every tool that can. The tool still owns the shape of its own addresses; this
	// only says they exist and can be handed back beside the verbs.
	//
	// ⚠ APPENDED AFTER GetSearchText, for the same reason that one is last (see above).
	virtual void FindInside(const wxString& query, std::vector<ibDataValue>& places) const {}

	// ⭐ MAY THIS RUN WHILE THE HOST IS WAITING ON A MODAL DIALOG? Almost nothing may: the
	// application is mid-question, and a command that opens a document or edits metadata underneath
	// it lands in a state nobody designed (see ibMcpBusyWith).
	//
	// The exceptions are the verbs that GET OUT of it — seeing what is standing, dismissing it,
	// reading what was said, saying something to the person, asking what state the platform is in.
	// Refusing those would leave a caller with no way to recover and nothing to tell the person.
	//
	// ⚠ APPENDED LAST, like the two above.
	virtual bool RunsWhileBusy() const { return false; }
};


// AN ID IS NOT A NAME. Tools address objects by NodeId because that is what
// survives a rename; a person watching the window has never seen one. This turns
// the id in `field` back into what the object is called — "Goods", "StockBalance",
// "ObjectModule" — so an activity line can say what was touched.
//
// Falls back to "#1723" when the id resolves to nothing: a number a person cannot
// read is still better than a sentence with a hole where the object should be.
BACKEND_API wxString ibMcpNameOf(const ibDataNode& params,
	const wxString& field = wxT("id"));

// ⭐⭐ HOW MANY OF THE CALLER'S WORDS LANDED — the one rule every finder here matches by, so the
// tool search and the corpus search cannot come to different conclusions about the same words.
//
// 🛑 REQUIRING ALL OF THEM ANSWERS NOTHING FAR TOO OFTEN, and an empty answer reads as "there is
// nothing about this" — the most expensive wrong answer a finder can give. Measured on this
// server (2026-09-02): "moved back and forth" — nothing, though transfers between warehouses are
// written up in full; "lots cost path movements" — nothing, with a whole entry on exactly that.
// What worked was the caller first translating their problem into the corpus's own nouns, which
// is the search doing its job backwards.
//
// So the COUNT is returned instead of a verdict, and the caller ranks by it: everything that
// matched every word if anything did, and otherwise whatever matched the most. A word is tried at
// shrinking lengths down to four characters, so "distributing" meets "distribution".
BACKEND_API size_t ibMcpWordsFound(const wxString& haystack, const wxString& query,
	size_t* asked = nullptr);

// WHAT A COMPOSITION STILL LACKS to produce a report somebody can read — a nameless variant, no
// output, nothing selected. Written where the report verbs live (mcpToolReport.cpp) and declared
// here because the configuration-wide audit asks the same question of every composer there is: two
// implementations of "what is missing" would answer differently within a week.
BACKEND_API void ibMcpComposerComplaints(const class ibCompositionDescription& composition,
	std::vector<wxString>& missing);

// ⭐⭐ WHICH REQUIRED ARGUMENT DID NOT COME — empty when they all did. Asked OF THE ARGUMENTS, not of
// the published schema: ibMcpArgument is where `required` is stated, and `Given()` is the same
// both-areas lookup a tool's own body uses, so "present" means the same on both sides of the gate.
//
// 🛑 `IsRequired()` WAS DECLARED, PUBLISHED AND READ BY NOBODY, so a call arriving without one
// reached the tool with an empty string where a name should be: `report_output` added a nameless
// output and answered success, `form_accepts` took the designer down (2026-09-01).
//
// ⚠ IT LIVED IN THE SERVER until 2026-09-02 — where it was first needed, not where it belongs. Two
// costs, and the second is why it moved: the transport owned a rule about arguments, and a test
// could not call it, so the suite's third question compared the schema with itself while its NAME
// promised it checked the refusal.
BACKEND_API wxString ibMcpMissingArgument(const class ibMcpTool* tool, const ibDataNode& arguments);

// ⭐ THE MODULE BEHIND AN ID, or nothing and the refusal to hand back. Four verbs asked this - two
// in module_read / module_write, one in debug_breakpoint, one in script_complete - and each of them
// wrote the same cast, the same null test and the same sentence. A question repeated four times is a
// question with four chances to be spelled differently, and it already had two spellings of the
// refusal.
//
// ⚠ The CAST stays, and belongs: what is wanted here is not "what kind are you" but the module's own
// interface, which is what a cast is for. What moved is the REPEAT.
BACKEND_API class ibValueMetaObjectModuleBase* ibMcpModuleOf(class ibValueMetaObject* object,
	wxString& refusal);

// ⭐⭐ IS WHAT ARRIVED WHAT THE SCHEMA SAID IT WOULD BE? Empty when every declared argument that came
// matches its own declaration; otherwise the sentence to refuse with.
//
// 🛑 THE SCHEMA WAS A CLAIM NOBODY HELD ANYONE TO. A tool publishes `type: integer`, `type: object`,
// `enum: [equal, notEqual, …]` — and nothing checked any of it, while the readers answer with a
// DEFAULT for the wrong shape: `Whole()` gives 0 for "abc", `Flag()` gives false for "true",
// `Text()` gives empty for an object. So a caller that mistyped an argument was not refused; it was
// answered about id 0, or about a module cleared by an empty text. One instance of this was fixed
// where it hurt (module_write checks its own text); the CLASS is closed here, once, for every tool
// and every tool to come — the same reasoning that put the undeclared-name gate in the server.
//
// ⚠ THE SHAPE, NOT THE MEANING. Whether a string names a real metatype stays the tool's business —
// it knows, and this does not. What is checked is only what the schema itself declared.
BACKEND_API wxString ibMcpArgumentFault(const class ibMcpTool* tool, const ibDataNode& arguments);

// ⭐ THE FIRST LINE THAT NAMES SOMETHING, as a WHOLE WORD — for an answer that says where a hit is
// rather than only that there was one. A caller judges a hit by the line and fetches only what
// survives that; a bare count sends them to open everything.
//
// ⚠ WHOLE WORD, because a name inside a longer identifier is a different thing entirely: `Goods`
// must not be reported as used by `GoodsReceipt`, or every answer becomes noise in a base whose
// naming is any good. Empty when the name is nowhere in the text.
BACKEND_API wxString ibMcpLineNaming(const wxString& text, const wxString& name);

// Is this query written as a REGULAR EXPRESSION rather than as words? Asked by everything that
// wants to show WHERE a match landed, so the highlight and the match cannot disagree about how
// the query was read.
BACKEND_API bool ibMcpIsRegex(const wxString& query);

// ⭐⭐ THE ONE PLACE THAT SAYS AN OBJECT. Every tool that answers about a metaobject answers with
// the same handful of facts, and each of them was spelling the tags out by hand: `name`, `kind`,
// `id`, and sometimes the texts — seventy-odd sites across nineteen files, with nothing but
// attention keeping one of them from saying `objectName` where the rest say `name`. A caller then
// has to hold TWO vocabularies in mind at once, the metaobject's and this server's, and the second
// one is not written down anywhere.
//
// ⭐ AND IT ASKS THE OBJECT, not the serializer. `BuildDataNode` is how a configuration is STORED —
// its keys are storage keys, it carries every internal name a metatype happens to have, and it runs
// the whole subtree. None of that is what a caller asked for, and binding to it means a storage
// rename becomes a protocol change. The five facts below come from the object's own methods, which
// is the surface the platform already promises to keep.
//
// `withText` adds the two texts when there is something in them — a listing does not want them,
// a single object does.
// ⭐⭐ THE ARGUMENTS THE SHARED DOORS READ — declared HERE, beside the code that reads them.
//
// A hardcoded name is legitimate when the writing and the reading sit in one file, where they can
// be seen to agree (Max, 2026-09-01). These three could not: a tool declared `value` and `language`
// in its own schema, and the thing that actually reads them is ibMcpSetProperty over in mcpTool.cpp
// — one spelling in each file, and neither file able to check the other. `id` was worse: eight
// tools spelled it, and ibMcpObjectNamed read it.
//
// So the door owns its arguments and a tool splices them into its own list:
//
//   const std::vector<ibMcpArgument>& Arguments() const override {
//       static const std::vector<ibMcpArgument> args = { ibMcpIdArgument(), ibMcpValueArgument() };
//       return args;
//   }
//
// The tool still says WHICH of them it takes — that is its own contract — but it no longer says
// what any of them is CALLED.
BACKEND_API const ibMcpTool::ibMcpArgument& ibMcpIdArgument();
BACKEND_API const ibMcpTool::ibMcpArgument& ibMcpValueArgument();
BACKEND_API const ibMcpTool::ibMcpArgument& ibMcpLanguageArgument();

BACKEND_API void ibMcpSayObject(const class ibValueMetaObject* object, ibDataNode& node,
	bool withText = false);

// ⭐⭐ EVERY PROPERTY THE OBJECT HAS — asked of the object, so a property added tomorrow is in the
// answer tomorrow, with nothing here edited.
//
// THIS IS THE INSURANCE, and it is the reason the answer must not come from the serializer. A
// metatype writes its own state in `WriteData`, property by property, by hand — so a description
// built from a saved node shows exactly the properties somebody remembered to write, and a new one
// is simply absent, silently, from everything that reads it. `ibPropertyObject` knows its whole
// list: `GetPropertyCount` / `GetProperty(i)`, and each one answers its own name, label, help,
// whether it may be edited, the closed set of words it accepts when it has one, and its value.
//
// Both trees with properties on them are `ibPropertyObject`s — a metaobject and a form control —
// so this serves either. Fills `node` with `properties` (an array), `count`, and nothing else, so a
// caller can put it beside whatever else it is answering.
//
// `only` narrows to one property by name (empty = all); `editableOnly` drops the read-only ones.
BACKEND_API void ibMcpSayProperties(const class ibPropertyObject* object, ibDataNode& node,
	const wxString& only = wxEmptyString, bool editableOnly = false);

// …AND ONE PROPERTY THAT IS SAID ITS OWN WAY: a caption, as a `value` of one text PER LANGUAGE.
// Both the walk above and the answer to a write call this, so a caller sees the same shape however
// it arrived — and neither hands over the stored template, which is what `GetNodeValue` would give.
BACKEND_API void ibMcpSayCaption(const class ibPropertyTString* caption, ibDataNode& into);

// ⭐ WHAT THE OBJECT ITSELF SAYS AGAINST BEING STORED — from BOTH places it may
// say it. A metaobject reports "not ready" either into the restructuring ledger
// or through the message pane, and which one it picks is a matter of when that
// metatype was written. A caller that reads one of them reads silence half the
// time and calls the object finished.
//
// Asked by every tool that CHANGES something, not by a verb of its own: a check
// somebody has to remember to run is a check that is not run on the day it
// matters. Empty means the object is content, which is the answer as much as a
// complaint is.
// ⭐ HOW A PROPERTY TAKES A VALUE — in one place, because there are now two trees
// with properties on them (metadata and forms) and the rules must not drift apart.
//
// A scalar goes straight through. A property whose values are a CLOSED SET —
// an enumeration, or a list the owner fills — is set BY ITS WORD, and a wrong
// word is refused WITH the right ones. Two vocabularies are accepted for the same
// value (the inspector's "Within second", the language's `WithinSecond`) because
// both are the platform's own.
//
// `params` supplies "value"; `result` is filled with what the property now holds,
// read back rather than echoed. False means refused, with words in `refusal`.
// ⭐ THE CLOSING HALF OF THE INSPECTOR'S SEQUENCE, for a caller that had to set the value through
// the property's OWN typed setter — a binding takes an ibMetaDescription, which no wxVariant a
// caller can build will carry.
//
// The telling still belongs in one place: the owner learns what changed and the property's real
// owner learns a child changed, exactly as ibObjectInspector::ModifyProperty does it. Without this
// the value lands and the object never reacts — and for a BINDING that means the far end of the
// relationship is never updated.
BACKEND_API void ibMcpNotifyChanged(class ibProperty* property, const wxVariant& oldValue);

// ⭐ THE WHOLE SEQUENCE, IN ONE CALL — ask the owner, set, tell the owner, tell the property's real
// owner. Exported because it stopped being one tool's private business: every verb that places a
// value now goes through it (a choice, a type description, a caption's language cell, a register
// record), and a second copy of a four-step sequence is four chances to drop a step.
BACKEND_API bool ibMcpApplyByHand(class ibProperty* property, const wxVariant& newValue, wxString& refusal);

// ⭐ THE PATTERN CORPUS, FOR SURFACES THAT ARE NOT TOOLS. `prompts` is served by the server and is
// the one place the platform can put words in front of a PERSON without a model relaying them — so
// the corpus has to be readable from outside the tool that reads it (mcpToolPatterns.cpp).
// The index is (name, summary); the text is one entry, empty when there is no such name.
BACKEND_API std::vector<std::pair<wxString, wxString>> ibMcpPatternIndex();
BACKEND_API wxString ibMcpPatternText(const wxString& name);

BACKEND_API bool ibMcpSetProperty(class ibProperty* property,
	const ibDataNode& params, ibDataNode& result, wxString& refusal);

// 🛑 "NOTHING HAS ID 0" IS THE WRONG COMPLAINT. A caller that forgot the argument gets the DEFAULT
// zero looked up and is told, confidently, that no object has that id — so they go looking for
// object zero instead of noticing they never named one. Two different mistakes, one sentence.
//
// Eight places did this lookup by hand. FIVE of them guarded the absent case and three did not —
// which is the worse shape than all-wrong: the correct version was sitting in the same folder,
// and copying picked up the lookup without the guard. A rule kept by a majority is a rule that is
// already leaking.
//
// So it is a function: absent is refused as absent, missing is refused as missing, and the
// configuration check every one of them opened with comes along.
//
// Null means refused, with words already in `refusal`.
//
// ⭐ AND IT IS NOT CALLED `…Argument`, which is what it was. Three things wore that word: the
// DECLARATION of an argument (ibMcpArgument), the writing of one into a schema
// (ibMcpSchemaArgument), and this — which is not an argument at all but a LOOKUP that happens to
// start from one. The middle one is gone from this header entirely: with every tool declaring its
// arguments, nothing outside mcpTool.cpp writes a schema entry by hand, so it folded into
// ibMcpArgument::Declare. This reads as the pair of ibMcpNameOf above: that one turns an id into a
// name for a person, this one turns it into the object for a verb.
BACKEND_API class ibValueMetaObject* ibMcpObjectNamed(const ibDataNode& params,
	wxString& refusal, const wxString& field = wxT("id"));

BACKEND_API std::vector<wxString> ibMcpComplaints(class ibValueMetaObject* object);

// ⭐ THE FACTS THAT DECIDE HOW TO WRITE ANYTHING — dialect, languages, compatibility version,
// build, who is logged in, what the configuration is called.
//
// ⚠ THEY CHANGE WHILE A CLIENT IS CONNECTED. The syntax dialect above all: it is a property of the
// configuration and a person can switch it mid-session, after which everything the handshake said
// about it is wrong — and wrong in the way that produces code which reads correctly and does not
// compile. So the orientation handed out at `initialize` is a SNAPSHOT, and this is the same
// question asked again at any moment.
//
// One function, two readers: the orientation renders it into prose, `platform_state` returns it as
// it stands. Two places computing the dialect would agree until the day they did not.
BACKEND_API void ibMcpDescribePlatform(ibDataNode& into);

// ⭐ A DETAIL IS SHOWN, NOT DUMPED. The window is a running log: a five-hundred-line module pasted
// into it scrolls everything else out of reach, and the reader loses the thing they were watching
// for. So a long text is cut and SAYS it was cut — a silent truncation is worse than none, because
// the reader trusts what they see to be all of it.
//
// ⚠ INDENTED, NOT FENCED. The window this lands in styles markdown without hiding it, so a
// ```fence``` shows up as backticks around the thing it was meant to frame. `language` is kept
// because it is a true statement about the text and a real renderer would want it — it simply has
// no reader today, and inventing one on screen was worse than having none.
BACKEND_API wxString ibMcpFencedExcerpt(const wxString& text, const wxString& language,
	size_t maxLines = 40);

// The same, folded into an answer under a stable name. Does nothing when there
// is nothing to say, so a clean result stays clean.
BACKEND_API void ibMcpReportComplaints(ibDataNode& result, class ibValueMetaObject* object);

// ⭐⭐ WHOEVER KNOWS SOMETHING WORTH CHECKING REGISTERS IT. `config_check` asks what is half-built
// in a configuration, and the answer is not one file's business: composers, fields and reports are
// the backend's, and a composition held BY A FORM — a list, a composer dropped onto a gridbox — is
// the frontend's, because a form's controls are a blob to anything that cannot build them
// (Max, 2026-09-02: *"a composer can be on a form too, like a list"*).
//
// 🛑 AND AN AUDIT THAT CANNOT SEE HALF THE WORLD IS WORSE THAN NONE, because it answers "nothing
// half-built" about a configuration with a broken form in it — a false clean is acted on, an
// absent check is not. So the checks are a registry rather than a function: the DLL that
// understands a kind of object contributes the question, and config_check asks whoever is linked.
class BACKEND_API ibMcpAudit {
public:
	virtual ~ibMcpAudit() = default;

	// Says a fault by naming the OBJECT it is about — a fault with no address cannot be acted on.
	typedef std::function<void(const class ibValueMetaObject*, const wxString&)> ibComplain;

	virtual void Check(class ibMetaData* metaData, const ibComplain& complain) const = 0;
};

BACKEND_API void ibRegisterMcpAudit(const ibMcpAudit* audit);
BACKEND_API const std::vector<const ibMcpAudit*>& ibMcpAudits();

// ⭐⭐ IS THE HOST IN THE MIDDLE OF SOMETHING A PERSON HAS TO ANSWER? While a modal dialog stands,
// the application is not in a state anybody can act on: its window is waiting for a click, its
// documents are half-open, and a command arriving from outside lands in the gap.
//
// 🛑 THIS IS A NEW CLASS OF FAULT, and it exists because of what this server is. A person cannot
// click in two places at once, so the situation never arose — one pair of hands, one modal, one
// answer. An assistant that can open anything at any moment reproduces states the platform was
// never in (Max, 2026-09-02, watching the designer die: he had a modal open and a tool opened a
// document underneath it — `ibCodeEditor::ActivateEditor` on a document whose metaobject was
// nowhere).
//
// So the server asks before it dispatches, and the answer is the dialog's own title — something the
// caller can put in front of the person, or clear with `window_dismiss`. Empty means free.
// Maintained by whoever owns windows (a wxModalDialogHook in the designer); an atomic count, so it
// can be read from the thread a request arrives on.
BACKEND_API void ibMcpBusyEnter(const wxString& what);
BACKEND_API void ibMcpBusyLeave();
BACKEND_API wxString ibMcpBusyWith();

// ⭐⭐ WHAT MAY RUN BEFORE A CLIENT HAS SAID WHO IT IS — and the answer is: exactly what saying so
// requires. There is a person in front of this designer, and a connection is invisible to them until
// something speaks; a client reading their whole configuration in silence is indistinguishable from
// one that never arrived. Asking for a greeting did not work in any of its gentler shapes, so it is
// a DOOR: everything else is refused, with the four verbs named, until `chat_say` has happened.
//
// The list is here rather than in the server because it is a fact about the TOOLS - which of them a
// greeting is made of - and because the contract test reads it to check every name is real.
BACKEND_API bool ibMcpRunsBeforeGreeting(const wxString& toolName);
BACKEND_API const std::vector<wxString>& ibMcpGreetingVerbs();


// The registry. Registration happens during static construction, so the store
// is a function-local static inside the .cpp — a namespace-scope container
// would not be built yet when the first registrar runs.
BACKEND_API void ibRegisterMcpTool(const ibMcpTool* tool);
BACKEND_API const std::vector<const ibMcpTool*>& ibMcpTools();
BACKEND_API const ibMcpTool* ibFindMcpTool(const wxString& name);

//     MCP_TOOL_REGISTER(ibMcpToolScriptCheck);
#define MCP_TOOL_REGISTER(tool_class)                                        \
	namespace {                                                              \
	struct tool_class##Registrar {                                           \
		tool_class##Registrar() { ibRegisterMcpTool(&m_tool); }              \
		tool_class m_tool;                                                   \
	};                                                                       \
	static tool_class##Registrar s_##tool_class##Registrar;                  \
	}

//     MCP_AUDIT_REGISTER(ibMcpAuditForms);   — the same idiom, for a check rather than a verb
#define MCP_AUDIT_REGISTER(audit_class)                                      \
	namespace {                                                              \
	struct audit_class##Registrar {                                          \
		audit_class##Registrar() { ibRegisterMcpAudit(&m_audit); }           \
		audit_class m_audit;                                                 \
	};                                                                       \
	static audit_class##Registrar s_##audit_class##Registrar;                \
	}

#endif // _IB_MCP_TOOL_H_
