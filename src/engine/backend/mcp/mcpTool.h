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
		enum class Kind { Text, Whole, Flag, Words, Many, Node };

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

#endif // _IB_MCP_TOOL_H_
