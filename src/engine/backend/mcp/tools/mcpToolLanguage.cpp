////////////////////////////////////////////////////////////////////////////
//	Description : the type tools — the vocabulary this platform speaks
////////////////////////////////////////////////////////////////////////////
//
// WHY THIS EXISTS. Everything a caller writes — a variable's declared type, an
// attribute's type, a query's CAST — is spelled with a name that means
// something only here. Without a way to ASK, a generating caller invents names
// that look right and are not, and finds out at compile time if it is lucky.
//
// The answer comes from the class factory, which is the one place every type
// registers itself (VALUE_TYPE_REGISTER, METADATA_TYPE_REGISTER and their
// family). No list is kept here: a type added tomorrow is answerable the day it
// registers, and a type that stops existing stops being offered.
//
// ⭐ IT ANSWERS THE CLASSIFICATION, not a bare list of words. A caller needs to
// know that "Catalog" is a metatype it can create and "Array" is a value it can
// construct — a distinction it would otherwise have to guess from the spelling.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/compiler/translateCode.h"  // ibTranslateCode::GetKeyWord — the language spells its own words
#include "backend/metadataConfiguration.h"   // the configuration's own type registry
#include "backend/objCtor.h"

namespace {

// The factory's kinds, in the caller's words. Kept as a table so the tool that
// FILTERS and the answer that LABELS cannot disagree — they read the same rows.
struct KindRow {
	const wxChar*     m_word;
	ibCtorObjectType  m_kind;
};

const KindRow s_kinds[] = {
	{ wxT("primitive"), ibCtorObjectType_object_primitive },
	{ wxT("value"),     ibCtorObjectType_object_value     },
	{ wxT("control"),   ibCtorObjectType_object_control   },
	{ wxT("system"),    ibCtorObjectType_object_system    },
	{ wxT("enum"),      ibCtorObjectType_object_enum      },
	{ wxT("context"),   ibCtorObjectType_object_context   },
	{ wxT("metadata"),  ibCtorObjectType_object_metadata  },
	{ wxT("metaValue"), ibCtorObjectType_object_meta_value },
};

// The name filter, in one place so the engine's types and the configuration's own are narrowed by
// the same rule. An empty query keeps everything — "no filter" is not "match nothing".
bool NameWanted(const wxString& name, const wxString& query)
{
	return query.IsEmpty() || ibMcpWordsFound(name, query) > 0;
}

void AppendKind(const KindRow& row, const wxString& query, std::vector<ibDataValue>& types)
{
	for (const ibCtorAbstractType* ctor : ibValue::GetListCtorsByType(row.m_kind)) {

		if (ctor == nullptr)
			return;

		if (!NameWanted(ctor->GetClassName(), query))
			continue;

		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		entry->SetValue(wxT("name"), ctor->GetClassName());
		entry->SetValue(wxT("kind"), wxString(row.m_word));
		entry->AddField(wxT("id"), ibDataValue::UInt((u64)ctor->GetClassType()));

		types.push_back(ibDataValue::Child(entry));
	}
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("The type's name as type_list gives it - 'Array', 'Structure', 'ValueTable'... "
			"Give this OR `typeId`."));
	return s_a;
}

// ⭐⭐ A TYPE ADDRESSED BY ITS CLASS ID — and the id is not a label, it is the whole handle.
//
// type_list answers with an `id` beside every name and there was no door that took one back. For a
// configuration type that matters twice over: the NAME of a reference type carries the object's own
// name (`CatalogRef.Goods`), so a rename breaks every note that quoted it, while the id does not
// move (Max, 2026-09-09: *"a converter from classID to metaID, name, kind and so on"*).
//
// And the id resolves to more than the type: through the ctor it names the METAOBJECT that declares
// it and WHAT KIND of thing it is — a reference, a manager, an object — so one number answers
// "what is this, whose is it, and what can it do" in a single call.
const ibArg& ArgTypeId()
{
	static const ibArg s_a(wxT("typeId"), ibArg::Kind::Whole,
		ibMcpText("The type's CLASS ID instead of its name - the `id` type_list gives beside each. "
			"Steadier than the name, because a reference type's name contains the object's own name "
			"and a rename moves it. Answers the same as `name` does, plus what the id resolves to: "
			"the metaobject that declares the type, and whether it is a reference, a manager, an "
			"object, a selection or a record set."));
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// type_list
//---------------------------------------------------------------------------
class ibMcpToolTypeList : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("type_list"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("looking through the available types");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Every type this platform knows, with what each one IS: a primitive, a value "
			"class you can construct, a metadata kind you can create, a control, an "
			"enumeration. Ask before writing a type name - the vocabulary is this "
			"configuration's, not a general one.");
	}

	// 🛑 AN ARGUMENT THE TOOL READS AND DOES NOT DECLARE IS AN ARGUMENT NOBODY CAN USE — and worse,
	// it makes every OTHER word look accepted: a caller passing `query` (the word every other
	// search here takes) got the whole list back and no complaint, because an undeclared name is
	// checked against nothing. `kind` was read in Call and declared nowhere.
	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const ibMcpArgument s_kind(wxT("kind"), ibMcpArgument::Kind::Text,
			ibMcpText("Narrow it to one kind of type: primitive, value, control, system, enum, "
				"context, metadata, metaValue, configuration. Omit for all of them. This is not a "
				"search - it filters by kind, and a word that is not one of these is refused."));

		// ⭐ NARROWING BY NAME, because the question is almost never "list everything" — it is
		// "does this platform HAVE an X". Answering that with the whole vocabulary costs the caller
		// several kilobytes to learn one yes or no (measured 2026-09-09: asking whether a `Map`
		// exists returned some three hundred names). The matcher is the SAME one mcp_search uses,
		// so `map`, `Ref\..*` and `table|array` mean here exactly what they mean there — a second
		// spelling of "search" is a second thing for a caller to hold.
		static const ibMcpArgument s_name(wxT("name"), ibMcpArgument::Kind::Text,
			ibMcpText("Narrow it to types whose NAME matches - words, or a regular expression when "
				"the query carries | \\ [ ] ^ $ or .* ('map', 'table|array', 'CatalogRef\\..*'). "
				"Combines with `kind`. Omit for all of them; a query that matches nothing answers "
				"with an empty list rather than a refusal, because 'this platform has no such type' "
				"is the answer."));

		static const std::vector<ibMcpArgument> s_arguments = { s_kind, s_name };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString wanted = params.GetValue<wxString>(wxT("kind"));
		const wxString query = params.GetValue<wxString>(wxT("name"));

		std::vector<ibDataValue> types;
		bool matched = false;

		for (const KindRow& row : s_kinds) {
			if (!wanted.IsEmpty() && !wanted.IsSameAs(row.m_word, false))
				continue;

			matched = true;
			AppendKind(row, query, types);
		}

		// A KIND NOBODY HAS IS A REFUSAL, not an empty answer: "there are no
		// controls" and "control is not a word here" are different, and only one
		// of them means try again differently.
		if (!matched && !wanted.IsSameAs(wxT("configuration"), false)) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a kind of type. Try: primitive, value, control, system, enum, "
				  "context, metadata, metaValue, configuration."), wanted);
			return false;
		}

		// ⚠ AND THE CONFIGURATION'S OWN, which the loop above cannot see.
		//
		// The global factory holds what the ENGINE registers; a configuration's
		// types — CatalogRef.Goods, a document's object, a register's record set —
		// live in the metadata's own registry, because they exist only against the
		// tree that declared them. Asked only of the factory, this answered with
		// 43 metatypes and not one type of the configuration in front of it, while
		// `CatalogRef.Goods` resolved perfectly well by name: the vocabulary was
		// there and the listing did not show it.
		if (wanted.IsEmpty() || wanted.IsSameAs(wxT("configuration"), false)) {

			if (activeMetaData != nullptr && activeMetaData->IsConfigOpen()) {
				for (const ibCtorMetaValueType* ctor : activeMetaData->GetListCtorsByType()) {

					if (ctor == nullptr)
						continue;

					if (!NameWanted(ctor->GetClassName(), query))
						continue;

					std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					entry->SetValue(wxT("name"), ctor->GetClassName());
					entry->SetValue(wxT("kind"), wxString(wxT("configuration")));
					entry->AddField(wxT("id"), ibDataValue::UInt((u64)ctor->GetClassType()));

					types.push_back(ibDataValue::Child(entry));
				}
			}
		}

		result.AddField(wxT("found"), ibDataValue::Int((s64)types.size()));
		result.AddField(wxT("types"), ibDataValue::Array(types));

		// ⭐ "THIS PLATFORM HAS NO SUCH TYPE" IS AN ANSWER, and it is usually the one being asked
		// for. Said in words rather than left as an empty array, because an empty array is also
		// what a mistyped filter looks like — and the two want opposite next moves.
		if (types.empty() && !query.IsEmpty()) {
			result.SetValue(wxT("note"), wxString::Format(
				ibMcpText("No type's name matches '%s'. If that was the question, the answer is that this "
				  "platform has none - do not reach for it in script. If it was a guess at the "
				  "spelling, ask again without `name` and read the vocabulary."), query));
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolTypeList);

//---------------------------------------------------------------------------
// type_members
//---------------------------------------------------------------------------
//
// WHAT AN OBJECT IS MADE OF — the same question the editor answers after a dot,
// asked without a caret. A value carries its own member table: methods with the
// call form the platform itself would show, their arity, and whether each
// RETURNS anything (a function) or does not (a procedure); properties beside
// them.
//
// ⚠ IT CREATES ONE TO ASK IT. Members are per-instance here — a type fills its
// table when it is built — so the only honest way to answer is to build one and
// look. Anything that cannot be built without arguments says so instead of
// answering an empty list, because "no members" and "I could not make one" are
// different facts and only one of them means stop asking.
//
class ibMcpToolTypeMembers : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("type_members"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("looking at what %s can do"),
			ArgName().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("What a type offers: its methods with their call form and whether each returns a "
			"value, and its properties. This is what the editor shows after a dot, asked by "
			"name instead of by cursor position.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgName(), ArgTypeId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		wxString name = ArgName().Text(params);

		// BY ID: turned into the name this tool already works in, so there is ONE road below and
		// the two addresses cannot answer differently.
		if (const ibDataValue* asked = params.FindField(ArgTypeId().Name());
			asked != nullptr && asked->Kind() == ibDataKind::Number) {

			const ibClassID clsid = (ibClassID)asked->AsUInt();

			if (activeMetaData != nullptr && activeMetaData->IsConfigOpen())
				if (const ibCtorMetaValueType* byId = activeMetaData->GetTypeCtor(clsid))
					name = byId->GetClassName();

			if (name.IsEmpty())
				if (const ibCtorAbstractType* byId = ibValue::GetAvailableCtor(clsid))
					name = byId->GetClassName();

			if (name.IsEmpty()) {
				refusal = wxString::Format(
					ibMcpText("No type carries the class id %s. type_list gives the id beside every name."),
					asked->AsNumber().ToString());
				return false;
			}

			result.AddField(wxT("typeId"), ibDataValue::UInt((u64)clsid));
		}

		if (name.IsEmpty()) {
			refusal = ibMcpText("No type named - give `name`, or `typeId` for the class id type_list "
				"answers with.");
			return false;
		}

		result.SetValue(wxT("name"), name);

		ibValue value;

		if (ibValue::GetAvailableCtor(name) != nullptr) {
			try {
				value = ibValue::CreateObject(name);
			}
			catch (...) {
				// ⭐ WHY, NOT JUST THAT. This used to say "cannot be built without arguments", which
				// reads as a limitation of this verb and sends a caller looking for the arguments.
				// There are none to find: what is left here after 2026-09-09 is two small families,
				// and neither has a value to show.
				refusal = wxString::Format(
					ibMcpText("'%s' has no empty form, so there is nothing to read members off. Either it "
					  "is a type CONSTRAINT - what a value is ALLOWED to be, like `Any` - and there was "
					  "never a value behind it; or it exists only against the thing that owns it (a "
					  "module unit, a row of a list), and one standing alone would answer about "
					  "nothing. If you are HOLDING one, ask what it offers where you got it: the "
					  "value's own members are in script_complete after a dot."), name);
				return false;
			}
		}
		// ⭐⭐ …AND THE CONFIGURATION'S OWN TYPES ARE BUILT THROUGH THE CONFIGURATION'S REGISTRY.
		//
		// 🛑 TWO REGISTRIES, ONE VOCABULARY — and this verb used to answer out of one of them and
		// REFUSE the other. type_list lists the configuration's types (`CatalogManager.Goods`,
		// `DocumentObject.GoodsReceipt`) because they are real names a script writes; they live in
		// the metadata's registry, so asking the engine alone answered "not a type this platform
		// knows" about a name type_list had just given. That was softened once into a refusal that
		// pointed at metadata_get — better words for the same silence.
		//
		// ⚠ AND IT IS THE MANAGERS THAT MADE IT COST SOMETHING. `Catalogs.<name>` is where DATA IS
		// WRITTEN — CreateElement, CreateFolder, CreateGroup, the finders — and the one verb that
		// exists to answer "what can this value do" refused precisely there. Measured 2026-09-09:
		// the way to find the item-creating verb was to guess `CreateItem`, be refused by the
		// runtime, and guess again. A vocabulary that cannot be asked is a vocabulary somebody
		// brings from another platform.
		//
		// The ctor knows how to build one (ibCtorMetaValueType::CreateObject), which is the same
		// road the runtime itself takes when a script names the type — so the answer is the one a
		// script would get, not a description of it.
		else if (activeMetaData != nullptr && activeMetaData->IsConfigOpen()) {

			ibCtorMetaValueType* const ctor = activeMetaData->GetTypeCtor(name);

			if (ctor == nullptr) {
				refusal = wxString::Format(
					ibMcpText("'%s' is not a type this platform knows. Use type_list to see what is."), name);
				return false;
			}

			// HELD IN AN ibValue, never on the stack as a raw pointer: these are refcounted, and a
			// bare pointer here is the shape that has produced heap corruption in this tree before.
			ibValue* const made = ctor->CreateObject();

			if (made == nullptr) {
				refusal = wxString::Format(
					ibMcpText("'%s' is a type of this configuration, but one cannot be built to ask - it "
					  "exists only against the object that declares it. Read that object with "
					  "metadata_get, its fields with query_fields."), name);
				return false;
			}

			value = ibValue(made);

			// ⭐⭐ AND THE CLASS ID SAYS MORE THAN "IT EXISTS" — it says WHAT KIND OF THING this is
			// and WHICH METAOBJECT declares it, and both are answers a caller otherwise infers from
			// the spelling of the name (Max, 2026-09-09: *"from the class id you can find out that
			// it is a reference, that it is a manager, that it is an object — and even pull out the
			// metadata that holds that reference"*).
			//
			// Inferring it from the name is exactly the habit this platform keeps refusing: a name
			// is text and a role is a fact the ctor already holds. `CatalogManager.Goods` is where
			// data is WRITTEN, `CatalogRef.Goods` is what a field HOLDS, `CatalogObject.Goods` is
			// what is edited and saved — three different vocabularies behind three similar words,
			// and the answer now says which one arrived.
			const auto roleWord = [](ibCtorObjectMetaType kind) -> const char* {
				switch (kind) {
				case ibCtorObjectMetaType::ibCtorObjectMetaType_Reference:     return "reference";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_Manager:       return "manager";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_Object:        return "object";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_Selection:     return "selection";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_List:          return "list";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection: return "tabularSection";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet:     return "recordSet";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_RecordKey:     return "recordKey";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager: return "recordManager";
				case ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic: return "characteristic";
				}
				return "";
			};

			if (const char* const role = roleWord(ctor->GetMetaTypeCtor()); *role != '\0')
				result.SetValue(wxT("role"), wxString::FromAscii(role));

			// WHOSE IT IS — said by the one function that says an object, so the id, the name and
			// the kind read the same here as everywhere else.
			if (const ibValueMetaObject* const owner = ctor->GetMetaObject()) {
				std::shared_ptr<ibDataNode> declaredBy = std::make_shared<ibDataNode>();
				ibMcpSayObject(owner, *declaredBy);
				result.AddField(wxT("declaredBy"), ibDataValue::Child(declaredBy));
			}
		}
		else {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a type this platform knows. Use type_list to see what is."), name);
			return false;
		}

		std::vector<ibDataValue> methods;
		for (long i = 0; i < value.GetNMethods(); i++) {

			// (No scope filter on METHODS, matching what the editor does after a
			//  dot: it drops scope-local PROPERTIES and lists every method. The
			//  member table knows IsMethodScoped, but the value does not forward
			//  it — and inventing a way round that would make this answer differ
			//  from the one a person sees, which is the whole thing to avoid.)
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), value.GetMethodName(i));

			// THE CALL FORM the platform itself would show — the one place a
			// caller learns what the arguments MEAN, since the table keeps their
			// count but not their names.
			const wxString helper = value.GetMethodHelper(i);
			if (!helper.IsEmpty())
				entry->SetValue(wxT("signature"), helper);

			entry->AddField(wxT("parameters"), ibDataValue::Int((s64)value.GetNParams(i)));
			// Function or procedure — not cosmetic: calling a procedure where a
			// value is expected is a compile error, and the two are told apart
			// only here.
			entry->AddField(wxT("returnsValue"), ibDataValue::Bool(value.HasRetVal(i)));

			methods.push_back(ibDataValue::Child(entry));
		}

		std::vector<ibDataValue> properties;
		for (long i = 0; i < value.GetNProps(); i++) {
			if (value.IsPropScoped(i))
				continue;
			properties.push_back(ibDataValue::String(value.GetPropName(i)));
		}

		result.SetValue(wxT("name"), name);
		result.AddField(wxT("methods"), ibDataValue::Array(methods));
		result.AddField(wxT("properties"), ibDataValue::Array(properties));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolTypeMembers);

//---------------------------------------------------------------------------
// linq_methods
//---------------------------------------------------------------------------
//
// THE PIPELINE VOCABULARY. A query can be written as a chain over a source
// rather than as text, and the operations that chain admits are a closed set
// the engine already keeps — with a one-line description each, written for the
// editor's tooltip (ibValue::GetLinqMethodTable).
//
// It is answered from that table and not from a list here, for the usual
// reason: an operation added to the engine appears in this answer with nothing
// edited, and one removed stops being offered.
//
class ibMcpToolLinqMethods : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("linq_methods"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("looking at the collection operations");
	}

	// The words somebody looks for this with — see ibMcpToolScriptQuery::GetSearchText, where the
	// measurement is written down. This is the door a query is WRITTEN at, so it answers to the
	// words for writing one, in both languages the product is used in.
	wxString GetSearchText() const override
	{
		// ⚠ MATCHED WHOLE, so the FORMS a caller types are listed rather than one stem — `folding`
		// does not find `fold`. And the words are the ones a question arrives in: somebody asks
		// about a value table or an array, not about "a collection".
		return wxT("linq query queries lambda callback clause clauses keywords "
			"write a query how to write fold folding roll up group grouping sort sorting "
			"filter filtering join joining array value table structure tabular section in memory "
			"row level access restrict RLS");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("HOW A QUERY IS WRITTEN HERE, in all three of its forms. `methods` - the "
			"pipeline operations a chain over a source may use (Where, Select, GroupBy, Join and "
			"the rest), each with one line saying what it does. `clauses` - the written form, "
			"`from x in src ... select ...`, clause by clause in the order they are read. "
			"`restrict` - the access-policy form, which is a query that narrows another one.\n\n"
			"Reach for it before writing a query rather than after: this is the whole vocabulary, "
			"and anything not in it is not part of the language. `script_query` is the other half - "
			"it takes a query you have written APART and says what the compiler made of it.\n\n"
			"WHICH OF THE TWO FORMS THE JOB WANTS, because they are not alternatives:\n"
			"  * A QUERY IS A FULL WAY TO ASK THE BASE, not a convenience on top of one. "
			"`Data.<collection>.<name>` is a source like any other, and the answer is an ordinary "
			"value table - so a query REPLACES a written query rather than decorating it. What it "
			"can do that a written one cannot is JOIN THE BASE TO WHAT IS ALREADY IN MEMORY: an "
			"array, a value table, a document's tabular section stand beside a catalog in the same "
			"`from`/`join`, and the answer comes back as a table you go on working with. Norms in "
			"memory, items in the base, matched on the code, one line - measured 2026-09-08 on a "
			"live base. That is the case it is best at, and the one a written query cannot serve at "
			"all.\n"
			"    THE TWO COMBINE rather than compete. A written query is heavy - its own text, its "
			"own object, parameters set one by one - and worth that weight when the DATABASE should "
			"do the work: a balance, a slice as of a date, a period total. It hands back a value "
			"table, and from there this is the lighter instrument: fold, sort, group, splice a "
			"second source in, a line at a time, INSIDE the code that needs the answer. Take the "
			"rows with the one, shape them with the other.\n"
			"  * A QUERY (`from ... select ...`) is for MIXING TABLES and folding them where they "
			"already are. The job it replaces is the familiar one: read a table, roll it up, read "
			"another, roll that up, match them by hand - several passes and a page of code. Here "
			"the same thing is one line, and the folds (`group ... into`, `join ... on ... equals`, "
			"`orderby`, `take`) are written where they are meant rather than assembled. Reach for "
			"it whenever data from more than one place has to meet, or whenever a roll-up would "
			"otherwise be spelled as a loop over rows.\n"
			"  * A RESTRICTION (`restrict s in Source ...`) is not a general tool at all: it is "
			"ROW-LEVEL SECURITY and nothing else. It lives in an access-policy handler, takes the "
			"query somebody else is about to run, and narrows it - typically by joining a table of "
			"rights. Written anywhere else it is the wrong shape; needed there, nothing else will "
			"do.\n\n"
			"THE DOORS AROUND THEM, in the order a job uses them: `linq_methods` (this one - the "
			"vocabulary and the clause order), `script_check` (does it compile), `script_query` "
			"(what the compiler UNDERSTOOD - the bindings, their origin, the columns, where the "
			"parse stopped), `script_complete` (what may be written at a caret, including the "
			"clause keywords that position admits), `code_run` (run it against the live base), and "
			"`role_restrict` for composing a restriction into a role's handler.\n\n"
			"FOUR THINGS THAT COST A RUN TO FIND OUT (measured 2026-09-08 against a live base):\n"
			"  * A QUERY'S ANSWER IS A TABLE - `TypeOf` says `Table`, its rows are `TableValueRow`, "
			"and its columns are what `select` named. So `Count()`, `Columns`, `Sort`, `[i]` and "
			"`foreach` all work on it; `First()` does not - a table has no such method.\n"
			"  * PUT THE ANSWER IN A VARIABLE BEFORE ASKING IT ANYTHING. A parenthesised expression "
			"cannot be continued with a dot: `(from ... ).Count()` does not compile, while "
			"`var q = from ... ; q.Count()` does.\n"
			"  * A FUNCTION HELD IN A FIELD OR AN ELEMENT IS CALLED THROUGH A VARIABLE. `s.f(1)` "
			"and `a[0](1)` do not compile - `var g = s.f; g(1)` does. (`s.f` is a Function; it is "
			"the CALL written in place that the grammar refuses.)\n"
			"  * A VALUE TABLE'S COLUMN IS A STRING UNLESS ITS TYPE IS GIVEN: "
			"`AddColumn(\"Q\", Type(\"Number\"))`. Left to the default, `r.Q > 3` compares a string "
			"with a number and quietly matches nothing.\n\n"
			"BUILDING ONE YOURSELF, END TO END - there is a road for this and it needs no new verb:\n"
			"  1. this tool for the vocabulary, the clause order and which form the job wants;\n"
			"  2. write the query IN KEYWORDS (shorter than the chain, and no lambda per verb);\n"
			"  3. `script_check` - does it compile (the verdict is about YOUR text; a configuration "
			"module that is mid-edit reports separately, under `inTheConfiguration`);\n"
			"  4. `script_query` - what the compiler UNDERSTOOD: the bindings and where each came "
			"from, the columns the answer will have, whether it groups or orders, and how far the "
			"parse got. Compare that against what you meant - a query that reads correctly and "
			"binds the wrong thing is caught HERE, not by running it;\n"
			"  5. `code_run` to run it against the live base, `module_patch` / `module_write` to put "
			"it into a module.\n"
			"A RESTRICTION IS COMPOSED THE SAME WAY, with one door more: `role_restrict` writes the "
			"whole access handler around the clauses you give it (role, where, joins, alias, "
			"operation) and hands it back without installing it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& WXUNUSED(params), ibDataNode& result, wxString& WXUNUSED(refusal)) const override
	{
		std::vector<ibDataValue> methods;

		for (const ibValue::ibLinqMethodInfo& info : ibValue::GetLinqMethodTable()) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), wxString(info.name));
			entry->SetValue(wxT("does"), wxString(info.helper));

			methods.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("methods"), ibDataValue::Array(methods));

		// ⭐⭐ …AND THE WRITTEN FORM, WHICH THE CHAIN VOCABULARY DOES NOT DESCRIBE. A verb list says
		// what operations exist; it does not say that a query OPENS with `from`, that a `join` owes
		// an `on … equals …`, or that `group … into` starts a second half. That is a shape, and
		// somebody writing one needs the shape before the vocabulary.
		//
		// 🛑 THE WORDS ARE ASKED OF THE TRANSLATOR, never typed here. `ibTranslateCode::GetKeyWord`
		// spells them off the one table the parser itself indexes (translateCode.cpp,
		// s_listKeyWord), so a keyword renamed there is renamed in this answer and a scheme that
		// no longer parses cannot be handed out.
		const auto word = [](int key) { return ibTranslateCode::GetKeyWord(key); };

		const auto clause = [](const wxString& form, const wxString& does) {
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("form"), form);
			entry->SetValue(wxT("does"), does);
			return ibDataValue::Child(entry);
		};

		std::vector<ibDataValue> clauses;
		clauses.push_back(clause(
			word(KEY_FROM) + wxT(" <name> ") + word(KEY_IN) + wxT(" <source>"),
			ibMcpText("Opens the query and binds a name to one row of the source. Required, and it "
				"comes first. Written again inside the query it nests: every row of the second "
				"source is visited for every row of the first.")));
		clauses.push_back(clause(
			word(KEY_VAR) + wxT(" <name> = <expr>"),
			ibMcpText("Names a value worked out per row, so the rest of the query can read it "
				"without repeating the expression.")));
		clauses.push_back(clause(
			word(KEY_JOIN) + wxT(" <name> ") + word(KEY_IN) + wxT(" <source> ")
				+ word(KEY_ON) + wxT(" <key> ") + word(KEY_EQUALS) + wxT(" <key>"),
			ibMcpText("Matches rows of a second source by a key. The left key reads the rows bound "
				"above it, the right key reads the row this clause is binding. A row with no match "
				"contributes nothing; a key matching several rows contributes one result row each.")));
		clauses.push_back(clause(
			word(KEY_WHERE) + wxT(" <condition>"),
			ibMcpText("Keeps the rows the condition holds for. Written before any projection it is "
				"offered to the source itself, which then filters where the data is.")));
		clauses.push_back(clause(
			word(KEY_GROUP) + wxT(" <expr> ") + word(KEY_BY) + wxT(" <key> [")
				+ word(KEY_INTO) + wxT(" <name>]"),
			ibMcpText("Collects rows under a key. Without `into` the grouping ENDS the query and it "
				"answers with the groups - no clause may follow, `select` included. With it, the "
				"name is bound to one group at a time and the query goes on: the clauses after it "
				"read `<name>.Key` and `<name>.Values`. So a grouped query that projects wants "
				"`into` (measured 2026-09-08: `group o by o.Unit select {...}` does not compile).")));
		clauses.push_back(clause(
			word(KEY_ORDERBY) + wxT(" <expr>[, <expr>...] [") + word(KEY_ASCENDING) + wxT("|")
				+ word(KEY_DESCENDING) + wxT("]"),
			ibMcpText("Orders the answer by values worked out per row. SEVERAL KEYS, comma-separated, "
				"in the order they decide: `orderby Warehouse, Item` sorts by warehouse and, within "
				"each, by item. Ascending is the default, and the direction written after the last "
				"key applies to the whole ordering.")));
		clauses.push_back(clause(
			word(KEY_SKIP) + wxT(" <n> / ") + word(KEY_TAKE) + wxT(" <n>"),
			ibMcpText("A window over what survived.")));
		clauses.push_back(clause(
			word(KEY_SELECT) + wxT(" <expr>   |   ") + word(KEY_SELECT)
				+ wxT(" { <name> = <expr>, ... }"),
			ibMcpText("What each surviving row becomes, and it NAMES THE COLUMNS of the answer. The "
				"braced form names them outright; a single expression is named after the field it "
				"reads, or Field1, Field2 when it reads none. Omitted, the row itself is the answer.")));
		clauses.push_back(clause(
			word(KEY_DISTINCT),
			ibMcpText("Drops repeats: it compares rows by their content, so two projected rows "
				"with the same fields are one.")));

		result.AddField(wxT("clauses"), ibDataValue::Array(clauses));

		// ⭐⭐ THE ORDER IS SHOWN ONCE, AS A QUERY — not asserted clause by clause. Each entry above
		// says what its own word MEANS, which is a fact about that word; where it may stand is a
		// fact about the GRAMMAR, and the grammar lives in the compiler. Said twice it drifts, and
		// it did: `distinct` was listed beside skip/take, which put it before `select` in every
		// example built from this list — written there the query does not compile, and that one
		// line was the only thing that made a query written from scratch fail (measured
		// 2026-09-08, 6 of 7 otherwise correct first time).
		//
		// One line that COMPILES is the cheapest thing that cannot drift silently: it is checkable
		// by script_check in a second, where prose about positions is not.
		result.SetValue(wxT("order"),
			word(KEY_FROM) + wxT(" <row> ") + word(KEY_IN) + wxT(" <source>")
			+ wxT("  ") + word(KEY_JOIN) + wxT(" <row> ") + word(KEY_IN) + wxT(" <source> ")
				+ word(KEY_ON) + wxT(" <key> ") + word(KEY_EQUALS) + wxT(" <key>")
			+ wxT("  ") + word(KEY_WHERE) + wxT(" <cond>")
			+ wxT("  ") + word(KEY_GROUP) + wxT(" <row> ") + word(KEY_BY) + wxT(" <key> ")
				+ word(KEY_INTO) + wxT(" <group>")
			+ wxT("  ") + word(KEY_ORDERBY) + wxT(" <key>[, <key>...] [") + word(KEY_DESCENDING) + wxT("]")
			+ wxT("  ") + word(KEY_SKIP) + wxT(" <n>  ") + word(KEY_TAKE) + wxT(" <n>")
			+ wxT("  ") + word(KEY_SELECT) + wxT(" { <name> = <expr>, … }")
			+ wxT("  ") + word(KEY_DISTINCT));

		// ⭐ THE ACCESS-POLICY FORM — a query whose whole job is to narrow another one. It is stated
		// separately because it is not something a query CONTAINS: it takes a source that is already
		// a query and hands back the same query with the joins and the filter folded into it, so it
		// is written where a policy is written and read wherever that source is read.
		std::vector<ibDataValue> restrictForm;
		restrictForm.push_back(clause(
			word(KEY_RESTRICT) + wxT(" <name> ") + word(KEY_IN) + wxT(" <source>"),
			ibMcpText("Opens it and binds a name to one row of the query being narrowed.")));
		restrictForm.push_back(clause(
			word(KEY_JOIN) + wxT(" <name> ") + word(KEY_IN) + wxT(" <table> ")
				+ word(KEY_ON) + wxT(" <condition>"),
			ibMcpText("Brings a second table in on a condition over the two names - a rights table, "
				"typically. May be written more than once.")));
		restrictForm.push_back(clause(
			word(KEY_WHERE) + wxT(" <condition>"),
			ibMcpText("The condition the remaining rows must hold. Folded into the source query, "
				"so it travels to the database with it rather than filtering afterwards.")));

		result.AddField(wxT("restrict"), ibDataValue::Array(restrictForm));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolLinqMethods);
