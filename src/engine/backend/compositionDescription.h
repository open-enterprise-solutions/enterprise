#ifndef __COMPOSITION_DESCRIPTION_H__
#define __COMPOSITION_DESCRIPTION_H__

// ⚠ THE INCLUDES ARE THE POINT. A description is the BOTTOM of the stack — the composer, the list
// and the settings dialog all take it, and it takes none of them. So: the core types, the value (a
// filter tree IS one) and the type description (the parameter's declared type IS one, and it is a
// description of the same family). Nothing from the composer, nothing from the model.
#include "backend/backend_core.h"     // ibMetaID, wxNOT_FOUND, ibClassID
#include "backend/compiler/value.h"   // ibValue — a filter's right-hand side travels as one
#include "backend/serialize/dataBuilder.h"   // ibDataNode — a stored value is a BLOB, never a runtime value
#include "backend/typeDescription.h"  // ibTypeDescription — the sibling description a parameter declares
#include "backend/guid.h"             // ibGuid — the stable key a select is identified by
// ⭐ THE UNFOLD IS THE LANGUAGE'S OWN WORD, and this header exists so every tier can name it without
// dragging a tier down (query/queryUnfold.h). A twin enum here would be a second vocabulary for one
// fact — and it WAS one: the runtime enumeration is registered over ibQueryDimUnfold, so a window
// speaking the twin got an enumeration nobody had (an assert in CreateAndConvertEnumObjectRef).
#include "backend/query/queryUnfold.h"

#include <vector>

class ibDataNode;
class ibDataValue;

// ---------------------------------------------------------------------------
// WHAT A COMPOSITION IS, AS DATA — the third family beside ibSourceDescription
// and ibTypeDescription, written to the same rule: a description is DUMB. It
// stores; it resolves nothing, opens nothing, describes no query.
//
// ⭐⭐ IT IS PRIMARY, AND IT HOLDS EVERYTHING (Max, 2026-08-23). This is the
// SKELETON OF THE STORED DATA: the composer, the dynamic list and the settings
// window all work with THIS — they change it and they save it. Nothing here is a
// projection of a live object; the live objects are built from it. That is why it
// takes nothing from the tiers above it and states every part in its own types.
//
// ⭐⭐ ONE PART, ONE DESCRIPTION, ONE PAIR (Max, 2026-08-23). A filter is a
// description with its own read/write; so is a sort, so is a grouping. The
// composition does not KNOW how any of them is written — it COMBINES them. That
// is what makes a list and a report the same thing:
//
//     main table (a NUMBER, the list's own) + composer (both)
//
// A LIST is a DEGENERATE composer: one main table, and it reuses the very same
// filter / sort / grouping, only building them a little differently. Not a
// different breed — the same description with the rest left empty.
//
// ⚠ AND THE MAIN TABLE IS A NUMBER, deliberately. `wxNOT_FOUND` means "nothing
// picked" — a state a description must be able to HOLD, because today the id is
// a by-product of a successful resolve (`SetQueryable(factory->ResolveById(id))`,
// and `SetQueryable(nullptr)` writes wxNOT_FOUND back), so a composition read
// before its metatype registered does not merely fail to describe its query — it
// FORGETS WHICH TABLE IT WAS.
// ---------------------------------------------------------------------------

// --- THE DESCRIPTION'S OWN ENUMERATIONS ------------------------------------
// ⭐ An enumeration living here is legitimate (Max, 2026-08-23) — a description states what it holds,
// and "how does this unfold" is part of what it holds. Declared HERE rather than taken from the
// query and composer tiers, because those take this file and not the other way round.
//
// (⛔ THERE WAS A TWIN OF `ibCompositionLevelKind` HERE — `ibDescriptionLevelKind`, "the same values,
//  so the two sides cast across without a table". Nothing ever used it: the description holds the
//  real enum, 280 lines below. A second vocabulary for one concept is exactly what this file's own
//  unfold comment was written to prevent, and it grew one anyway.)

// The comparison a condition makes. PLAIN enum (not enum class): the runtime enumeration uses it as
// a map key AND converts it to a number.
//
// ⚠ APPENDED, NEVER INSERTED — the kind is serialised BY NUMBER in every saved setting, so a value
// taken in the middle re-reads old settings as a different comparison.
enum ibComparisonKind {
	ibComparisonKind_Equal = 0,
	ibComparisonKind_NotEqual,
	ibComparisonKind_Greater,
	ibComparisonKind_Less,
	ibComparisonKind_GreaterEqual,
	ibComparisonKind_LessEqual,
	ibComparisonKind_Contains,       // → LIKE
	ibComparisonKind_In,             // membership
	ibComparisonKind_InHierarchy,    // membership that walks down
};

// The sort direction.
enum ibSortDirection {
	ibSortDirection_Ascending = 0,
	ibSortDirection_Descending,
};

// HOW A GROUP JOINS ITS CHILDREN. A filter is a TREE: the root is a group, and a group holds
// conditions and other groups. Without this every filter is an implicit AND of a flat list, which
// cannot say "this AND (that OR the other)" — the shape most real filters take the moment they stop
// being trivial.
enum ibFilterGroupKind {
	ibFilterGroupKind_And = 0,
	ibFilterGroupKind_Or,
	ibFilterGroupKind_Not,      // negates the AND of its children
};

// ⭐⭐ WHAT ONE ROW OF A SELECTED-FIELDS TABLE IS. A FIELD — or `Auto`, which stands for *everything
// the storey above chose, HERE*.
//
// ⚠ APPENDED, NEVER INSERTED, like every other kind in this file: the row is serialised BY NUMBER.
enum ibSelectedFieldKind {
	ibSelectedFieldKind_Field = 0,
	ibSelectedFieldKind_Auto,
};

// HOW A USER MEETS A CONDITION. A property of the SETTING, not of the data: the same condition is
// applied either way; this says whether the user is offered it, and where.
enum ibFilterDisplayMode {
	ibFilterDisplayMode_Normal = 0,     // in the settings form
	ibFilterDisplayMode_QuickAccess,    // also in the list header — the ones people change daily
	ibFilterDisplayMode_Inaccessible,   // applied, never shown
};

// (NO RUNTIME HERE — not the enumerations' script faces, not a settings object, nothing that can be
//  called. A description STORES; the words above are what it stores, and the pickers that offer them
//  live with the runtime, in system/value/composition/valueComposerSettings.h. Max, 2026-08-23: "the
//  enums are legitimate, they exist; everything to do with runtime is not here — built later".)

// --- FILTER ----------------------------------------------------------------
// ⭐⭐ THE FILTER IS DATA, LIKE EVERY OTHER PART (Max, 2026-08-23: "filters and sorts all travel
// through this composer description now"). It used to be an ibValue holding a live tree of
// ibValueFilterGroup / ibValueFilterItem objects — a SECOND model of the same thing, half-built,
// with no array and nothing to walk. What a filter is, is lines; what a tree adds is which line
// sits under which group.
//
// ⭐ AND IT STAYS A TREE, IN MEMORY AND ON DISK (Max, 2026-08-23: "a filter is a tree structure —
// filter A has children inside it; the SAVE is tree-shaped at the very least"). A group OWNS its
// children: `std::vector<ibFilterNodeDescription>` inside the node it belongs to, which C++17 allows
// for exactly this shape. Copying a description therefore deep-copies the whole filter with it,
// which is the one thing every window relies on — edit a copy, put it back on OK.
//
// The EXPRESSION the engine runs is derived from these lines and deliberately not stored: an
// expression cannot be edited back into the lines a person wrote.

// ONE SIDE of a condition — a FIELD (a path into the source) or a plain VALUE. Both sides are the
// same shape, which is what lets a filter say `Price > Cost` and not only `Price > 100`.
struct ibFilterOperandDescription {
	wxString          m_path;                    // non-empty = a field; empty = the value below
	ibValue           m_value;                   // the literal, when this side is not a field
	ibMetaID          m_leafId = wxNOT_FOUND;    // what the path resolved to, when it did
	ibTypeDescription m_type;                    // …and its type — what makes the OTHER side editable
	wxString          m_presentation;            // what the picker showed; empty = derive from the path

	bool IsField() const { return !m_path.IsEmpty(); }
	bool operator==(const ibFilterOperandDescription& o) const {
		return m_path == o.m_path && m_value == o.m_value && m_leafId == o.m_leafId
			&& m_presentation == o.m_presentation;
	}
	bool operator!=(const ibFilterOperandDescription& o) const { return !(*this == o); }
};

// A NODE of the filter tree — a CONDITION or a GROUP, said by its KIND rather than by a bit. A
// group carries its children; a condition carries its two sides. Order matters and interleaves — a
// group can stand between two conditions — which is why there is one child list and not two.
enum ibFilterNodeKind {
	ibFilterNodeKind_Condition = 0,
	ibFilterNodeKind_Group,
};

struct ibFilterNodeDescription {
	ibFilterNodeKind    m_kind    = ibFilterNodeKind_Condition;
	bool                m_use     = true;
	ibFilterDisplayMode m_display = ibFilterDisplayMode_Normal;
	wxString            m_presentation;           // the user's own label; empty = generate it

	// …when it is a CONDITION
	ibFilterOperandDescription m_left;
	ibComparisonKind           m_comparison = ibComparisonKind_Equal;
	ibFilterOperandDescription m_right;

	// …when it is a GROUP: how it joins what is under it, and what is under it.
	ibFilterGroupKind                    m_groupKind = ibFilterGroupKind_And;
	std::vector<ibFilterNodeDescription> m_children;

	bool operator==(const ibFilterNodeDescription& o) const {
		return m_kind == o.m_kind && m_use == o.m_use
			&& m_display == o.m_display && m_presentation == o.m_presentation
			&& m_left == o.m_left && m_comparison == o.m_comparison && m_right == o.m_right
			&& m_groupKind == o.m_groupKind && m_children == o.m_children;
	}
	bool operator!=(const ibFilterNodeDescription& o) const { return !(*this == o); }
};

// ⭐ EVERY PART COMPARES ITSELF, and the whole compares by comparing its parts. A property asks
// "changed or the same" of the description (ibVariantDataComposition::Eq), and that question can
// only be answered where each part knows what it is made of.
struct ibFilterDescription {
	// The ROOT is not a node: every filter has one, it can never be removed, and giving it an index
	// would mean every parent link had to know whether 0 meant "the root" or "the first line".
	ibFilterGroupKind m_rootKind = ibFilterGroupKind_And;
	std::vector<ibFilterNodeDescription> m_nodes;

	bool IsOk() const { return !m_nodes.empty(); }
	void Clear() { m_nodes.clear(); m_rootKind = ibFilterGroupKind_And; }

	// ADD A CONDITION to a group — the root's own children by default, any group by handing it its
	// child list. One way in, so a line can never be built half-filled.
	static ibFilterNodeDescription& Append(std::vector<ibFilterNodeDescription>& into,
		const wxString& path, ibComparisonKind comparison, const ibValue& value, bool use = true) {
		ibFilterNodeDescription node;
		node.m_use = use;
		node.m_left.m_path = path;
		node.m_comparison = comparison;
		node.m_right.m_value = value;
		into.push_back(node);
		return into.back();
	}
	ibFilterNodeDescription& Append(const wxString& path, ibComparisonKind comparison,
		const ibValue& value, bool use = true) {
		return Append(m_nodes, path, comparison, value, use);
	}

	// …AND A GROUP, which is the same act with children instead of sides.
	static ibFilterNodeDescription& AppendGroup(std::vector<ibFilterNodeDescription>& into,
		ibFilterGroupKind kind = ibFilterGroupKind_And) {
		ibFilterNodeDescription node;
		node.m_kind = ibFilterNodeKind_Group;
		node.m_groupKind = kind;
		into.push_back(node);
		return into.back();
	}

	bool operator==(const ibFilterDescription& o) const {
		return m_rootKind == o.m_rootKind && m_nodes == o.m_nodes;
	}
	bool operator!=(const ibFilterDescription& o) const { return !(*this == o); }
};

// --- SORT ------------------------------------------------------------------
// A sort line is DATA, not an object: a path and a direction. (The live lists
// have two modes — a dialog's buffer and a model's composer — and in the second
// there is no line object at all, which is why nothing here needs one.)
struct ibSortLineDescription {
	wxString m_path;
	bool     m_ascending = true;
	bool operator==(const ibSortLineDescription& o) const { return m_path == o.m_path && m_ascending == o.m_ascending; }
	bool operator!=(const ibSortLineDescription& o) const { return !(*this == o); }
};

struct ibSortDescription {
	std::vector<ibSortLineDescription> m_lines;
	bool IsOk() const { return !m_lines.empty(); }
	void Clear() { m_lines.clear(); }
	void Append(const wxString& path, bool ascending) { m_lines.push_back({ path, ascending }); }
	bool operator==(const ibSortDescription& o) const { return m_lines == o.m_lines; }
	bool operator!=(const ibSortDescription& o) const { return !(*this == o); }
};

// --- GROUPING --------------------------------------------------------------
// A grouping line carries its UNFOLD kind, and that is load-bearing: a hierarchy
// grouping IS what makes a list a tree, so dropping the kind reloads every tree
// as a flat grouping and reads as data loss.
// ⭐⭐ GROUPED BY PERIODS — the stored twin of `ibQueryTotalPeriods` (queryAst.h), which is what the
// query text already says: `BY <field> PERIODS(Month, &From, &To)`. Same three parts, one shape, so
// the settings window and the query constructor cannot mean different things by one word.
//
// ⭐ THE BOUNDS ARE TEXT HERE, and that is the difference from the AST, not an omission: a
// description is written to a file, and an expression tree is not. What a person types is a
// parameter — `&From` — or a literal, and it becomes an expression when the level is rendered.
//
// ⭐ AND "IS THERE PERIODICITY" IS ASKED OF THE UNIT, not of a flag beside it. A unit with no
// periodicity is nothing, and periodicity with no unit is impossible — so the content IS the
// answer, the way a filter operand is a field exactly when it has a path.
struct ibGroupPeriodsDescription {
	wxString m_unit;   // the word — the same vocabulary ibPeriodUnits() offers, verbatim
	wxString m_from;   // empty = the earliest period in the data
	wxString m_to;     // empty = the latest

	bool IsOk() const { return !m_unit.IsEmpty(); }
	void Clear() { m_unit.clear(); m_from.clear(); m_to.clear(); }
	bool operator==(const ibGroupPeriodsDescription& o) const {
		return m_unit == o.m_unit && m_from == o.m_from && m_to == o.m_to;
	}
	bool operator!=(const ibGroupPeriodsDescription& o) const { return !(*this == o); }
};

struct ibGroupLineDescription {
	wxString            m_path;
	// ⭐ THE UNFOLD, AS THE TYPE IT IS. It was a bare int "the enum value, as the plain number it
	// is" — and a number is what every reader then had to cast back, which is the shape of a type
	// that was never stated. Only the SERIALISER writes it as a number, which is what a file is.
	ibQueryDimUnfold m_kind = ibQueryDimUnfold::Elements;
	// Set = this line is grouped BY PERIODS. Beside the unfold rather than inside it: a hierarchy is
	// how a REFERENCE opens up, periods are how a DATE does, and the two never meet on one field.
	ibGroupPeriodsDescription m_periods;
	bool operator==(const ibGroupLineDescription& o) const {
		return m_path == o.m_path && m_kind == o.m_kind && m_periods == o.m_periods;
	}
	bool operator!=(const ibGroupLineDescription& o) const { return !(*this == o); }
};

struct ibGroupDescription {
	std::vector<ibGroupLineDescription> m_lines;
	bool IsOk() const { return !m_lines.empty(); }
	void Clear() { m_lines.clear(); }
	void Append(const wxString& path, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements) {
		m_lines.push_back({ path, kind });
	}
	bool operator==(const ibGroupDescription& o) const { return m_lines == o.m_lines; }
	bool operator!=(const ibGroupDescription& o) const { return !(*this == o); }
};

// --- SETTINGS = the three of them together ---------------------------------
// What a list calls "its settings" and what a variant stores are the same three
// parts. Combined here rather than in either of them, so neither owns the shape.
struct ibSettingsDescription {
	ibFilterDescription m_filter;
	ibSortDescription   m_sort;
	ibGroupDescription  m_group;

	// ⭐⭐ …AND THE STRUCTURE, BECAUSE THE STRUCTURE **IS** THE SETTING (Max, 2026-08-24). The report's
	// outputs, the two axes under each, the ladder of levels on them and every level's own filter,
	// sort and grouping — that whole tree is what a person arranged, so it travels wherever the
	// setting travels. A variant NAMES one of these; the reader saves one of these.
	//
	// 🛑 IT SAT ON THE VARIANT, and the settings window handed back only the flat half — so a
	// composition-wide filter came back and everything set on a NODE stayed in the window's copy and
	// died with it (Max, live: *"the main filter is kept, the nodes cannot be kept"*). Nothing
	// forwards a structure anywhere now; it is simply part of what was set.
	//
	// ⚠ RECURSIVE BY NATURE — a level's own settings are this same type. A level fills the flat
	// three and leaves this empty: the tree hangs off the levels, not off their settings. That is
	// also why the three functions below are defined AFTER ibOutputDescription: at this point the
	// element type is still incomplete, which `std::vector` allows for the member but not for the
	// bodies that touch it.
	std::vector<struct ibOutputDescription> m_structure;

	// ⭐⭐ …AND THE FIELDS, for the same reason as the three above: "I want to see these columns" is a
	// setting a READER makes, exactly like a filter or a sort (Max, 2026-08-28).
	//
	// 🛑 IT HAD NOWHERE TO GO. The root row of the settings tree is the COMPOSITION, so its field
	// table was `ibCompositionDescription::m_selected` — the AUTHOR's half. On the reader road the
	// window edits a copy of the composition and the caller keeps only the SETTING, so a person
	// chose their columns, pressed OK and lost them, with nothing raised anywhere: the edit landed
	// in an object that was about to be dropped.
	//
	// The chain is now four storeys, each the same table with the same `Auto` row:
	//   composition (the author) → THIS (the reader) → output → node.
	//
	// ⚠ THE ELEMENT TYPE IS STILL INCOMPLETE HERE, and named as `struct …` for exactly the reason
	// `m_structure` above is: std::vector allows an incomplete element for a MEMBER, though not for
	// the bodies that touch it — which is why the three functions below are defined further down.
	std::vector<struct ibSelectedFieldDescription> m_selected;

	// ⭐⭐ …AND THE PARAMETER VALUES THE READER FILLED IN — for exactly the reason the fields above
	// have their own table. The author declares a parameter and ticks "For user"; the person running
	// the report puts a value beside it, and that value is part of what THEY set (Max, 2026-08-29:
	// "if I leave it for the user, a tab appears, they choose a value next to that parameter, and it
	// goes into the query when the composition happens").
	//
	// 🛑 IT HAD NOWHERE TO GO. The parameters live on the COMPOSITION — the author's half — and the
	// reader's window edits a copy of that which the caller drops, keeping only this setting. So the
	// value was typed into an object about to be thrown away, with nothing raised anywhere: the same
	// hole the selected fields had, one page along.
	//
	// Only the NAME and the stored value travel: what a parameter IS — its type, its expression, who
	// may fill it in — is the author's declaration and stays there.
	std::vector<struct ibParameterDescription> m_parameters;

	// NOTHING SET AT ALL — which is a state of its own, not an accident, and the ONE question that
	// answers "has anybody saved a setting": a composer whose reader has not runs on `m_variants[0]`.
	bool IsOk() const;
	void Clear();

	bool operator==(const ibSettingsDescription& o) const;
	bool operator!=(const ibSettingsDescription& o) const { return !(*this == o); }
};

// --- PARAMETER -------------------------------------------------------------
// What the query asks for and who fills it in. The TEXT is one of its two authors (`&Period` in the
// query means there is a parameter called Period), so `m_fromQuery` records which of the two put it
// here — a re-parse may drop what the text stopped mentioning and must not touch a hand-made one.
struct ibParameterDescription {
	wxString         m_name;
	wxString         m_expression;

	// ⭐⭐ THE VALUE IS A BLOB — the packed form, and nothing else (Max, 2026-08-29: "m_value is a
	// blob"; "the schema is just a store of the values we serialised").
	//
	// A description is DATA. A live `ibValue` here is runtime the moment somebody puts a reference in
	// it: a session behind it, a register of identities, a row to read. And a description is read
	// while the configuration is still LOADING — metaobject by metaobject — so building that runtime
	// there asks for a type three branches away that does not exist yet, and the load dies on a value
	// it wrote itself ("Unknown value type '<id>' in the data").
	//
	// So the store keeps what was written, and the runtime value is built FROM it at execution, by
	// whoever is running the composition, against the configuration it runs in — ibStoredValue /
	// ibStoreValue below.
	ibDataNode       m_value;

	ibTypeDescription m_type;
	bool             m_userSettable = false;
	bool             m_fromQuery    = false;
	bool operator==(const ibParameterDescription& o) const {
		// ⭐ THE VALUE IS COMPARED AS THE NODE IT IS. Nothing is built to answer it: what a store holds
		// is what was written, and two writings that say the same thing are the same value.
		return m_name == o.m_name && m_expression == o.m_expression && m_value == o.m_value
		    && m_userSettable == o.m_userSettable && m_fromQuery == o.m_fromQuery;
	}
	bool operator!=(const ibParameterDescription& o) const { return !(*this == o); }
};

// ⭐ THE TWO DOORS BETWEEN A STORE AND A RUNTIME VALUE — the only places where one becomes the other.
// Reading needs the configuration the value is read against (references and enum members are built
// by the metadata, not by the value factory); writing needs nothing, a value packs itself.
BACKEND_API ibValue ibStoredValue(const ibDataNode& stored, const class ibMetaData* metaData);
BACKEND_API void    ibStoreValue(ibDataNode& stored, const ibValue& value);

// ⭐⭐ THE TITLE A NAME IMPLIES — `DataVersion` → "Data Version", `Number` → "Number". A name is
// written for the language (one word, no spaces); a title is written for a reader, and nobody
// should have to type one out to get a report that can be read (Max, 2026-08-26: "if the title is
// not given, it is generated from the name — the capitals are where the spaces go").
//
// Here, in the composition's own vocabulary, because a title IS a composition's word: the query
// tier below knows names and aliases and has no business inventing captions.
BACKEND_API wxString ibTitleFromName(const wxString& name);

// THE NAME A PATH IMPLIES — its last segment. `Partner.Contract` is the field "Contract"; the walk
// to it belongs to the path, not to what the field is called.
BACKEND_API wxString ibNameFromPath(const wxString& path);

// --- RESOURCE --------------------------------------------------------------
// An aggregate the levels fold. `m_func` empty means the path IS the whole expression.
struct ibResourceDescription {
	wxString m_func;
	wxString m_path;
	// ⭐ THE NAME THE FIGURE IS READ BACK UNDER — `res["Qty"]`, the twin of a level's alias, which
	// this language already has. It travels INTO the query (`TOTALS SUM(Amount) AS Qty`), because a
	// name is what the query tier deals in. Empty = the engine names it after the argument.
	wxString m_alias;

	// ⭐⭐ OVER WHAT IT IS COMPUTED — the name of a grouping this composition declares, or empty.
	//
	// Empty is the ordinary resource: its area is the LADDER, so it means one figure on each heading
	// and follows whatever the reader re-grouped. Named, the figure belongs to that level and is
	// constant inside it — which is how a share gets its denominator, and what other systems reach
	// through an expression evaluated "in the context of a grouping".
	//
	// Travels INTO the query as `TOTALS SUM(Amount) OVER Item`, like everything else this tier says:
	// the text is the one seam downward, and nothing below has to be told about resources. A branch
	// qualifies the name where two of them carry a level of the same name (`Branch.Level`).
	wxString m_scope;

	// (⚠ AND NO TITLE HERE. A resource is BUILT ON A FIELD — `SUM(Amount)` is a reading of Amount —
	//  so what it is called comes from that field, through m_path (Max, 2026-08-26: "a resource
	//  refers to a field; it is the field that should hold the title, and the resource reaches it
	//  through the field"). A caption of its own would be a second place to say one thing, and the
	//  two would drift the first time a person renamed the field.)
	bool operator==(const ibResourceDescription& o) const {
		return m_func == o.m_func && m_path == o.m_path && m_alias == o.m_alias && m_scope == o.m_scope;
	}
	bool operator!=(const ibResourceDescription& o) const { return !(*this == o); }
};

// ⭐⭐ A FIELD OF THE COMPOSITION, AND WHAT IT IS CALLED. This is the entity a resource, a grouping
// level and a printed column all REFER TO — they name a path, and the path is this. So the title
// lives here, once, and everything that mentions the field reads the same answer.
//
// TWO MEMBERS, because "no title" and "an empty title" are different answers, and because the
// generated one has to stay live: until somebody says otherwise, the caption follows the name. Which
// is what `m_useTitle` says — a person took it over.
// THREE THINGS, AND THEY ARE THREE (Max, 2026-08-26):
//   * the PATH TO THE DATA — where the value comes from, qualified by the named package when two
//     of them offer the same word. That is the whole job of `ONTO`: it settles a clash of names,
//     nothing else.
//   * the NAME — the short word everything else says: a resource names a field, a level groups by
//     one, a script reads one back.
//   * the TITLE — what a person reads. Generated from the name until somebody takes it over.
// (A role — opening balance, closing balance — is expected to join them; it is the same table.)
struct ibFieldDescription {
	wxString m_path;
	// Empty = the name IS the path's last segment, which is the ordinary case and is why nothing
	// has to be written down for nearly every field of nearly every report.
	wxString m_name;
	bool     m_useTitle = false;
	wxString m_title;
	// (⏭ AND ITS ROLE — opening balance, closing balance — and its periodicity, which is what this
	//  table is FOR beyond captions: an output can then say "this figure is the opening balance"
	//  instead of the report re-deriving it from a name (Max, 2026-08-26). One entry per field,
	//  whatever ends up being said about it; the readers all come through here already.)

	// THE NAME IN FORCE — what was written down, else the PATH itself: a name is assembled as the
	// package's name plus the field's (`Sales.Qty`), and that assembly IS the path (Max, 2026-08-26).
	// Asked rather than read off the member, because absence means something and every caller would
	// otherwise re-state it.
	wxString NameInForce() const { return m_name.IsEmpty() ? m_path : m_name; }

	// …AND WHAT A READER SEES. What a person took over, else the field's own name read out loud.
	wxString TitleInForce() const {
		return m_useTitle ? m_title : ibTitleFromName(ibNameFromPath(NameInForce()));
	}

	bool operator==(const ibFieldDescription& o) const {
		return m_path == o.m_path && m_name == o.m_name
		    && m_useTitle == o.m_useTitle && m_title == o.m_title;
	}
	bool operator!=(const ibFieldDescription& o) const { return !(*this == o); }
};

// ⭐⭐ ONE SELECT OF THE QUERY, AND WHAT ITS FIELDS ARE CALLED (Max, 2026-08-26: "you are storing the
// SELECTs — each of them has its own description inside").
//
// A composition's query is a package of selects. `ONTO <name>` names one of them, and the name is
// what qualifies a path when two selects offer the same word — that is the whole job of the keyword:
// it settles a clash of names, nothing else. Which is why the fields live HERE and not in one flat
// table on the composition: two selects may both have a `Qty`, and they are two fields.
//
// ⚠ NAMED AFTER WHAT IT IS, NOT AFTER THE KEYWORD THAT NAMES IT. `OntoDescription` was the first
// spelling and it describes only the case where somebody wrote `ONTO`: a select exists — and needs
// an entry — whether or not it has been named.
//
// ⭐ AND THE IDENTITY IS NOT THE NAME. A select exists whether or not anybody named it, so it
// carries an id that never changes, and the name is written ON that id — rename it and every path
// that referred to it still refers to it, because they refer by identity and the name is what they
// are RENDERED with (Max: "you change the value at the input and everything downstream sees the new
// name"). With one select a name is optional; with two it is required, because without it neither
// can be addressed. (Both rules are the settings window's to enforce.)
struct ibSelectDescription {
	// Stable, opaque, and never shown — the thing a path actually points at. Empty on a record that
	// predates this, which is exactly the ONE unnamed select a composition starts with.
	wxString m_id;
	// The word `ONTO` gave it. Empty = unnamed, which is legitimate while it is the only one.
	wxString m_name;
	// What its fields are called — only the ones somebody has said something about. The fields
	// themselves are whatever the select projects, so listing them here would be a copy that goes
	// stale the moment the text is edited.
	std::vector<ibFieldDescription> m_fields;

	// A KEY NOBODY EVER READS OUT — made in one place so "how is identity minted" has one answer.
	static wxString NewId() { return ibGuid(ibGuid::newGuid()).str(); }

	// ⭐ IS THIS THE SELECT SOMETHING MEANT? Asked by IDENTITY first and by name second, which is the
	// order the whole design rests on: the id is what a reference IS, the name is what it is written
	// with. A caller holding an id keeps its select across a rename; one holding a name is looking at
	// text, and text is what a rename changes.
	bool Matches(const wxString& idOrName) const {
		if (!m_id.IsEmpty() && m_id.IsSameAs(idOrName, false))
			return true;
		return !m_name.IsEmpty() && m_name.IsSameAs(idOrName, false);
	}

	bool operator==(const ibSelectDescription& o) const {
		return m_id == o.m_id && m_name == o.m_name && m_fields == o.m_fields;
	}
	bool operator!=(const ibSelectDescription& o) const { return !(*this == o); }

	// THE FIELD AN UNQUALIFIED NAME MEANS, inside this select. Null when nobody has said anything
	// about it — which is the ordinary case and not an error: its title is then its name.
	const ibFieldDescription* Find(const wxString& name) const {
		for (const ibFieldDescription& field : m_fields)
			if (ibNameFromPath(field.NameInForce()).IsSameAs(name, false))
				return &field;
		return nullptr;
	}
};

// WHICH SELECT A PATH SPEAKS OF — the one it names, or the only one there is. Null when neither
// answers, which is exactly what an unqualified path means once a second select exists.
BACKEND_API const ibSelectDescription* ibSelectOfPath(const std::vector<ibSelectDescription>& selects,
                                                      const wxString& path);

// ⭐ THE TITLE IN FORCE FOR A PATH — ONE function, so the description and the running composer
// cannot answer differently (they hold the same selects; they must not each work out what a column
// is called). What a person set, else the field's name read out loud.
//
// ⚠ A PATH NOBODY HAS AN ENTRY FOR IS NOT AN ERROR — it is the ordinary case, and it is also what a
// field that has GONE looks like. Either way the answer is its name, so a report whose query lost a
// column still prints: the schema degrades field by field, never as a whole.
BACKEND_API wxString ibTitleForPath(const std::vector<ibSelectDescription>& selects, const wxString& path);

// ⭐⭐ AND `Auto` IS A ROW, NOT A FLAG. It stands for everything the storey above chose, and it
// stands SOMEWHERE: a row has a position, so the inherited fields can sit before this node's own,
// after them, or between them. A boolean "inherit" could not say where. Take the row out and the
// node defines its whole composition by hand — which is the thing that could not be said at all
// while inheritance was a blind pile-up.
//
// 🛑 A FLAG BY THIS NAME EXISTED AND WAS REMOVED on 2026-08-24 (`m_selectedAuto`), and it is not
// what is coming back. Then, inheritance REPLACED, and the flag existed to say "do not replace";
// when replacing became adding, the flag had nothing left to mean and went. What returns is the
// choice itself — with a place in the list.
//
// ⭐ AND THIS IS WHAT OPTIMISES THE QUERY. The selected fields decide what takes part in the
// selection, so a field nobody named is not read, not fetched and not rendered: the report gets
// simpler for free (Max: "we get the report simplified for nothing").
struct ibSelectedFieldDescription {
	ibSelectedFieldKind m_kind = ibSelectedFieldKind_Field;
	wxString            m_path;   // the field; empty for Auto, which names nothing

	bool IsAuto() const { return m_kind == ibSelectedFieldKind_Auto; }

	static ibSelectedFieldDescription Field(const wxString& path) {
		ibSelectedFieldDescription row;
		row.m_path = path;
		return row;
	}
	static ibSelectedFieldDescription Auto() {
		ibSelectedFieldDescription row;
		row.m_kind = ibSelectedFieldKind_Auto;
		return row;
	}

	bool operator==(const ibSelectedFieldDescription& o) const {
		return m_kind == o.m_kind && m_path == o.m_path;
	}
	bool operator!=(const ibSelectedFieldDescription& o) const { return !(*this == o); }
};

// THE LIST AS A WHOLE — asked rather than walked, because "does this inherit" is one question and
// every reader of it would otherwise spell the search itself.
inline bool ibSelectedInherits(const std::vector<ibSelectedFieldDescription>& rows) {
	for (const ibSelectedFieldDescription& row : rows)
		if (row.IsAuto())
			return true;
	return false;
}

// ⚠ AN EMPTY LIST INHERITS. Saying nothing is not the same as saying "nothing": a node that has
// never been touched shows what the storey above shows, and that is the state every node starts in.
// Refusing to inherit is a thing somebody DOES — they take the Auto row out — and taking a row out
// of an empty table is not possible, which is exactly why empty cannot mean it.
inline bool ibSelectedInheritsOrIsEmpty(const std::vector<ibSelectedFieldDescription>& rows) {
	return rows.empty() || ibSelectedInherits(rows);
}

// --- STRUCTURE -------------------------------------------------------------
// ⭐⭐ THE WHOLE STRUCTURE LIVES HERE, in its own types. It used to be described with the composer's
// (ibDataComposer::Output / GroupNode), and that was the rule broken: a description is the BOTTOM of
// the stack — the composer takes it, it does not take the composer. Everything below is stated in
// core types and in siblings of the same family (ibValue, ibTypeDescription), and in nothing else.
//
// The flat filter / sort / grouping cannot say any of this: a level made of several fields is
// indistinguishable there from several levels, and an output beside the first — or an axis of
// columns — has nowhere to go at all.

// (WHAT A LEVEL GROUPS BY IS ITS GROUPING. A `struct ibLevelFieldDescription { path; unfold; }` used
//  to stand here — the same pair ibGroupLineDescription already was, one section up, and the level
//  then held it in a member of its own BESIDE its settings. So a node had a grouping in two places:
//  the array in m_settings.m_group and the array in m_fields. What a level groups by IS its grouping
//  — Max, 2026-08-23: "a grouping holds an array of filters, an array of GROUPINGS, of sorts, of
//  available fields" — so it is m_settings.m_group and there is nothing beside it.)

// ⭐ WHAT A LEVEL OF THE LADDER IS — a GROUPING or the DETAIL RECORDS (Max: a detail record IS an
// empty grouping). The rows themselves are a level: they sit at the bottom of the ladder, under the
// deepest heading, and the settings tree writes them as a node like any other.
//
// SAID WITH A TYPE, not with "the fields are empty". Emptiness happens by accident too — a level
// whose fields stopped resolving loses them, and CollapseEmptyLevels drops it precisely so a
// nameless heading does not swallow every row. One emptiness, two opposite meanings; the node says
// which it is, and nothing downstream has to guess.
//
// ⚠ IT LIVES HERE, WITH THE STORED SHAPE, and the composer takes it from here. It used to be
// declared in dataComposer.h with the description carrying "the same value as a number" beside it —
// a twin vocabulary, which is exactly what the unfold above stopped being.
enum class ibCompositionLevelKind
{
	Grouping,   // a heading: fold by this level's fields
	Details,    // the rows as they are, under the level above
};

// ⭐⭐ WHAT AN OUTPUT IS — levels down the page, or levels down AND across it.
//
// ⚠ AND IT IS STORED, which it did not use to be: `Kind()` read it off `m_columnGroups.empty()`,
// and that was right for exactly as long as the kind meant "has it been filled in". It stopped
// being right the day a person could ADD A TABLE (Max, 2026-08-25: the structure gets "add
// grouping" and "add table"; a table opens with two undeletable nodes, Rows and Columns). A table
// that was just added is empty on BOTH axes and is a table all the same — the kind became what
// somebody DECIDED, and a decision is not derivable from what has been typed since.
//
// It lives here for the reason the level kind does: it is part of what an output IS, so it belongs
// with the stored shape rather than beside it.
enum class ibCompositionOutputKind
{
	Grouping,   // levels one under another; a level with no fields is its detail rows
	Table,      // levels down AND across — the cross-table
};

// ⭐⭐ ONE NODE OF AN OUTPUT — a grouping, or the detail records. What Max drew, 2026-08-23:
//
//     output 1        (arrays of filters, sorts, available fields)
//       grouping 1    (arrays of filters — a tree of nodes —, of GROUPINGS, of sorts, of available)
//         grouping 2  (the same, again)
//           details   (arrays of filters, sorts, available)
//     output 2 …
//
// Every entry in those brackets is an ARRAY, and every one of them is in m_settings: a filter that
// is a tree of conditions and groups, a sort of many lines, and the grouping — what this node folds
// by. A node is the SAME shape at every depth, which is why one type says all four rows.
struct ibLevelDescription {
	ibCompositionLevelKind          m_kind = ibCompositionLevelKind::Grouping;

	// ⭐⭐ WHAT THIS NODE SHOWS, AND IT IS ADDED TO WHAT STANDS ABOVE IT (Max, 2026-08-24: "if a
	// sub-node has additional selected fields, they are laid on top of the existing ones"). A node
	// does not restate the whole list to add one column — it says the one, and the ones its output
	// and its composition already said keep standing.
	//
	// 🛑 AVAILABLE FIELDS ARE GONE — there is no such entity (Max: "available tells us nothing, it is
	// the same thing understood in a harder way"). Selected IS the statement: these are the fields we
	// want to see, and they are what actually reaches the nodes. A second set saying what one COULD
	// have selected had no reader on the run path and no answer a person needed.
	//
	// ⭐ …AND `Auto` IS A ROW OF THIS TABLE (2026-08-28). It says where the storey above lands; take
	// it out and this node states its whole composition itself. See ibSelectedFieldDescription for
	// why that is a row and not the flag this once had.
	std::vector<ibSelectedFieldDescription> m_selected;

	// ⭐⭐ A NODE HAS SETTINGS — THE WHOLE OF THEM. Not one condition and one order: the SAME
	// ibSettingsDescription the composition itself has, one storey down. WHAT THIS NODE GROUPS BY IS
	// m_settings.m_group — it used to be a second array beside the settings (m_fields, of a twin type
	// spelling the same {path, unfold} pair), so a node stated its grouping in two places.
	//
	// ⚠ AND THE WINDOW USED TO KEEP THE REST OF THE SHAPE BESIDE IT: a std::map<node, settings>
	// standing next to the structure, filled at open and written back at commit. The settings a node
	// has belong to the node, so they are ON it, and the editors are pointed straight at its parts.
	ibSettingsDescription           m_settings;

	// ⭐⭐ AND A NODE UNFOLDS. A grouping is not a rung on a ladder — it is a node with nodes under
	// it, each with its own grouping and its own settings, and the nesting is written down rather
	// than inferred. It used to be a FLAT vector on the axis where "the order IS the nesting":
	// readable only while nothing forks, and Max's output 3 — two groupings side by side, each with
	// its own detail records under it — could not be said at all.
	//
	// Recursive by value, which a struct may be through a vector: the element type only has to be
	// complete where the vector is USED, and std::vector is specified to allow it.
	std::vector<ibLevelDescription> m_children;

	// ⭐⭐ IS THIS LEVEL THE DETAIL RECORDS — asked HERE, of the level itself, because it is a question
	// about a level and nothing else (Max, 2026-08-28: *"what worries me is that three places check
	// the same thing"*).
	//
	// A level with NO GROUPING FIELDS is the records: it writes no `BY`, it folds nothing, and what
	// it prints is the rows under the heading above it. The KIND says the same thing deliberately —
	// it is how a person adds a records node before typing anything into it — so either answer is
	// yes, and one of them being absent is not a no.
	//
	// 🛑 IT WAS ASKED IN FOUR PLACES AND MEANT TWO THINGS. The settings window called a fieldless
	// level "the detail records" in its Field cell; the engine looked only at the kind
	// (`DetailLevelOf`, `DetailAxisOf`, `OutputWrites`, `WantsDetails`). So the window promised rows
	// and the read never asked for them — and when the read was taught the new rule, the walk still
	// held the old one and printed a single total line over 125 invisible records. Two of the three
	// were fixed at a time, twice, which is what a question with several homes does.
	bool IsDetailRecords() const {
		return m_kind == ibCompositionLevelKind::Details || m_settings.m_group.m_lines.empty();
	}

	bool operator==(const ibLevelDescription& o) const {
		return m_kind == o.m_kind && m_selected == o.m_selected
		    && m_settings == o.m_settings && m_children == o.m_children;
	}
	bool operator!=(const ibLevelDescription& o) const { return !(*this == o); }
};

// One output — what a report SHOWS: its rows, its columns, and what it reads.
struct ibOutputDescription {
	wxString                           m_name;
	// WHAT THIS OUTPUT IS — decided when it was added, not read back off what has been filled in
	// since. See ibCompositionOutputKind for why this one fact cannot be derived.
	ibCompositionOutputKind            m_kind = ibCompositionOutputKind::Grouping;
	// (`m_sourceText` DELETED — a per-output query of its own. It was written to the file, read back
	//  and compared for equality, with NO PRODUCER AND NO CONSUMER anywhere in the tree: nothing
	//  ever set it and nothing ever asked. A composition reads ONE source and folds it several ways,
	//  which is what an output is; a second source would be a second composition. The composer's own
	//  `m_sourceText` is a different member and is the author's verbatim query — that one is live.)
	// What this output shows — the same table a level has, and read by the same rule: an `Auto` row
	// is where the COMPOSITION's choice lands, and an output with no such row states its own
	// composition whole. Empty is the ordinary case and it inherits.
	std::vector<ibSelectedFieldDescription> m_selected;
	// AN OUTPUT HAS SETTINGS TOO — filters and sorts. Its m_group stays empty and that
	// is the difference between the two storeys: an output does not fold, its NODES do. They were
	// missing here entirely while a level had two thirds of them, so an output-wide filter or sort
	// had nowhere to be stored and only ever existed in the running composer.
	ibSettingsDescription              m_settings;
	// THE ROOTS OF EACH AXIS — and roots, not a ladder: an output may open with two groupings side
	// by side, each unfolding into its own (Max's output 3). What is under a node is on the node.
	std::vector<ibLevelDescription>    m_rowGroups;
	std::vector<ibLevelDescription>    m_columnGroups;

	bool operator==(const ibOutputDescription& o) const {
		return m_name == o.m_name
		    && m_kind == o.m_kind
		    && m_selected == o.m_selected
		    && m_settings == o.m_settings
		    && m_rowGroups == o.m_rowGroups && m_columnGroups == o.m_columnGroups;
	}
	bool operator!=(const ibOutputDescription& o) const { return !(*this == o); }
};

// ⭐⭐ THE SETTING'S OWN THREE, NOW THAT AN OUTPUT IS A COMPLETE TYPE. Declared up there, defined
// here, because a setting CONTAINS the outputs and an output contains settings — the recursion is
// the shape of the thing, not an accident of the file: *"the output is defined by the settings, and
// every output has its own groupings, sorts, its own fields and its own filters"* (Max, 2026-08-24).
inline bool ibSettingsDescription::IsOk() const {
	return m_filter.IsOk() || m_sort.IsOk() || m_group.IsOk() || !m_structure.empty()
	    || !m_selected.empty() || !m_parameters.empty();
}
inline void ibSettingsDescription::Clear() {
	m_filter.Clear(); m_sort.Clear(); m_group.Clear(); m_structure.clear(); m_selected.clear();
	m_parameters.clear();
}
inline bool ibSettingsDescription::operator==(const ibSettingsDescription& o) const {
	return m_filter == o.m_filter && m_sort == o.m_sort && m_group == o.m_group
	    && m_structure == o.m_structure && m_selected == o.m_selected
	    && m_parameters == o.m_parameters;
}

// --- VARIANT ---------------------------------------------------------------
// ⭐⭐ A VARIANT IS A KIND OF THE REPORT, WITH ITS SETTINGS — laid down by development (Max,
// 2026-08-23), not a snapshot of somebody's session. "Sales", and "Sales with profitability": ONE
// SOURCE, two ways of showing what it holds. That is what a variant is for — getting the most out
// of a single report instead of writing three of them.
//
// ⚠ WHICH IS WHY THE QUERY IS NOT HERE. The source is the composition's, shared by every variant;
// a variant carries only what makes it a different KIND — its settings, its structure, and the
// parameter values it was laid down with.
//
// 🎯 THE USER REUSES ONE AS A STARTING POINT and then SAVES — and what they save is theirs, kept on
// the storage side, over the author's variant rather than inside it. That is the whole reason this
// description has to serialise on its own: the author's half travels with the configuration, the
// user's half travels to the database, and both are the same shape.
// ⭐⭐ A VARIANT IS A WRAPPER OVER A SETTING, and that is the whole of it (Max, 2026-08-24: *"a
// variant differs from a setting only in that it lives on top of it, and it has a name and a synonym
// for that setting"*). It is not executable and nothing composes "on a variant": setting a variant
// IS setting a SETTING — `SetUserSettingsDesc(variants[n].m_settings)` — and the composer runs on
// that, knowing nothing about where it came from.
//
// 🛑 IT CARRIED THE STRUCTURE AND ITS OWN PARAMETERS. The structure is the report's SHAPE and there
// is one of it — it moved to the composition; the parameters were a second list beside the
// composition's own. What was left is what a variant is FOR: naming a setting so a person can pick
// it by that name.
struct ibVariantDescription {
	wxString              m_name;
	wxString              m_synonym;   // what the picker shows; empty = derive it from the name
	ibSettingsDescription m_settings;

	bool operator==(const ibVariantDescription& o) const {
		return m_name == o.m_name && m_synonym == o.m_synonym && m_settings == o.m_settings;
	}
	bool operator!=(const ibVariantDescription& o) const { return !(*this == o); }
};

// --- THE COMPOSITION -------------------------------------------------------
struct ibCompositionDescription {

	// ⭐⭐ THE ANCHOR — the MAIN source, the one everything else joins onto (Max, 2026-08-24). A bare
	// id, and this is where it LIVES: the Source property on a list is a view of this field, filled
	// from it on load and read into it on save, exactly as the Query tab is a view of `m_query`.
	//
	// 🛑 IT WAS SET BY NOBODY until 2026-08-24 — serialised in both directions and written by no line
	// in the tree, while the picked source sat in a property the list's own WriteProperty never
	// wrote. The anchor a person chose therefore reached the file nowhere, and the list came back
	// with no source at all.
	//
	// A report leaves it alone — its sources are however many its queries name, and how many is none
	// of its business.
	ibMetaID m_mainTable = wxNOT_FOUND;

	// THE COMPOSER HALF, shared by both. A LIST fills the query and the settings and leaves the rest
	// empty; a REPORT uses all of it. One format, one reader, one writer — the difference is which
	// fields happen to be filled.
	wxString                            m_query;
	std::vector<ibParameterDescription> m_parameters;
	std::vector<ibResourceDescription>  m_resources;

	// ⭐⭐ THE SELECTS OF THE QUERY — one entry each, holding what ITS fields are called. This is the
	// section every other one points into: a resource names a field, a level groups by one, a column
	// prints one, and each of them says a path that is resolved HERE.
	//
	// Empty is the ordinary state, and it means exactly what it says: nobody has named anything, so
	// every field is titled by its name. It is also where a field's role — opening balance, closing
	// balance — and, later, its translation will go.
	std::vector<ibSelectDescription>    m_selects;

	// THE TITLE IN FORCE FOR A PATH — the one door, so a printer, a header and a chart cannot answer
	// differently. What a person set, else the name read out loud (ibTitleFromName).
	//
	// The path may be QUALIFIED by a package (`Sales.Qty`) or bare (`Qty`). Qualified, it names the
	// package that says so; bare, it can only mean the one package there is — which is precisely why
	// a second package makes the name compulsory.
	//
	// ⚠ A PATH NOBODY HAS AN ENTRY FOR IS NOT AN ERROR — it is the ordinary case, and it is also
	// what a field that has GONE looks like. Either way the answer is its name, so a report whose
	// query lost a column still prints: the schema degrades field by field, never as a whole.
	wxString TitleForPath(const wxString& path) const { return ibTitleForPath(m_selects, path); }

	// THE SELECT AN ID NAMES — the door a caller uses when it holds identity rather than text.
	ibSelectDescription* SelectById(const wxString& id) {
		if (id.IsEmpty())
			return nullptr;
		for (ibSelectDescription& select : m_selects)
			if (select.m_id.IsSameAs(id, false))
				return &select;
		return nullptr;
	}

	// ⭐⭐ RENAMING A SELECT IS ONE WRITE — the point of an identity that is not a name. The entry
	// keeps its id, so everything that refers to it BY ID still does; what changes is the word it is
	// rendered with.
	//
	// ⏭ AND THE PATHS IN THE SETTINGS ARE NOT THERE YET. A grouping, a resource and a sort store a
	// path as TEXT (`Sales.Qty`) because that same text is what goes into the query, so today they
	// refer to a select by its NAME and a rename would have to rewrite every one of them — the very
	// chase this design exists to avoid. Closing that means storing the qualifier as an ID and
	// RENDERING the name at the moment the query text is written; it is an arc of its own, and it
	// starts here, at the one place a rename passes through.
	bool RenameSelect(const wxString& id, const wxString& name) {
		ibSelectDescription* select = SelectById(id);
		if (select == nullptr)
			return false;
		select->m_name = name;
		return true;
	}

	// ⭐⭐ THE VARIANTS, AND THERE IS ALWAYS ONE. A composition with no variant would have nowhere to
	// keep its settings, so the vector starts with one — the same trick `ibDataComposer::m_outputs`
	// uses, and for the same reason: an invariant held by CONSTRUCTION needs no window to remember it.
	//
	// 🛑 THE AUTHOR'S SETTINGS USED TO BE A MEMBER BESIDE THIS — `m_settings`, a MIRROR of
	// `m_variants[m_activeVariant].m_settings`, kept in step by hand at six sites and by a legacy
	// branch in the serialiser that writes one **or** the other. Two members holding one truth is the
	// drift class this file's own comments keep warning about; the accessor below is now the whole of
	// the story (2026-08-24).
	//
	// 🛑 AND THERE IS NO "ACTIVE" ONE HERE. Which variant is chosen is a FRONTEND setting, not a
	// server one (Max, 2026-08-24) — a stored index would have been a THIRD place a setting can be
	// in force, beside the composer's two sections, and the server has no business remembering which
	// template a particular reader liked. See `GetCompositionSettingsDesc` below for where the
	// choice actually goes.
	std::vector<ibVariantDescription>   m_variants = std::vector<ibVariantDescription>(1);

	// (NO STRUCTURE HERE EITHER — the outputs are part of a SETTING, not of the composition. *"Each
	//  user setting defines its own output, and every output has its own groupings, sorts, fields and
	//  filters"* (Max, 2026-08-24). So a variant that names a setting names its outputs with it, and
	//  the reader who saves one saves theirs.)

	// ⭐ WHAT THE WHOLE COMPOSITION SHOWS — the BOTTOM of the chain an output resolves against and a
	// node resolves against again. It lived on the running composer alone, so what a person set on
	// the Report row was read from a buffer and written back nowhere permanent.
	//
	// ⚠ AN `Auto` ROW HERE STANDS FOR NOTHING — there is no storey above the composition — so it is
	// simply skipped. Kept legal rather than refused: a table is a table, and a row that means
	// "everything above" in a place with nothing above is empty, not wrong.
	std::vector<ibSelectedFieldDescription> m_selected;

	// (THE ACTIVE SETTINGS ARE NOT HERE. What a read runs on is a COPY the live object holds — the
	//  composer, or the model behind a tablebox — taken once from these and driven in as the basis
	//  for execution (Max, 2026-08-23). It is not stored: it is the state of a thing that is open,
	//  and a description is what a thing IS, not what one run of it happens to be doing.)

	// ⭐ THE SETTINGS PART, BY REFERENCE — the family's own way of handing a part out (GetTypeDesc,
	// GetSourceDesc). Everyone who wants the filter, the sort or the grouping asks for this and
	// takes what it needs; reaching for the member spells the same thing in as many places as there
	// are callers, and the first one to be renamed is the one that gets missed.
	// ⭐⭐ A VARIANT IS A TEMPLATE OF THE AUTHOR'S SETTINGS (Max, 2026-08-24). Not "the current state
	// of the report": it is what the composition SHIPS with, loaded into the composer's declared
	// section — and the reader then takes it and puts their own on top, in the composer's other
	// section. So the author's settings ARE a variant's settings, and there is one place they live.
	//
	// ⭐⭐ AND IT IS THE ZEROTH ONE, ALWAYS (Max, 2026-08-24: the author's setting IS variant zero).
	// Not a default a picker will later override here — the reader's choice never lands in this
	// vector at all. Variants are shown in the DESIGNER; a run always takes the first.
	//
	// ⏭ WHAT A PICKER DOES, when it lands: taking variant N means putting `m_variants[N].m_settings`
	// into the composer's USER section — the same door "save my settings" goes through, because it
	// is the same act. Choosing nothing leaves that section empty and the report runs on this one.
	// So the whole feature is a menu and one existing setter; nothing in this file changes for it.
	ibSettingsDescription& GetCompositionSettingsDesc() {
		return m_variants.front().m_settings;
	}
	const ibSettingsDescription& GetCompositionSettingsDesc() const {
		return m_variants.front().m_settings;
	}


	// ⭐ THE PAIR: SET A QUERY, OR SET A TABLE. Two ways of saying WHAT TO READ, and the composer
	// simply keeps whichever it was given — a bare id for the table, the text for the query. Neither
	// is a mode the rest of the code has to branch on; a list says the second, a report says the
	// first, and both end up in the same description.
	void SetQuery(const wxString& text) { m_query = text; }
	void SetMainTable(const ibMetaID& id) { m_mainTable = id; }

	bool HasQuery() const { return !m_query.IsEmpty(); }
	bool HasMainTable() const { return m_mainTable != wxNOT_FOUND; }

	void ClearQuery() { m_query.Clear(); }
	void ClearMainTable() { m_mainTable = wxNOT_FOUND; }

	// ⭐ EQUALITY IS WHAT IT DESCRIBES — the family's own rule (ibSourceDescription compares its id
	// path, ibTypeDescription its clsid list). A property asks this to tell "changed" from "the
	// same", and comparing the LIVE objects instead answers "different" for two cells holding
	// identical settings.
	bool operator==(const ibCompositionDescription& o) const {
		return m_mainTable == o.m_mainTable
		    && m_query == o.m_query
		    // (the settings are the FIRST VARIANT's, and the variants are compared below —
		    //  comparing them again here would be comparing one fact twice)
		    && m_parameters == o.m_parameters
		    && m_resources == o.m_resources
		    && m_selects == o.m_selects
		    && m_variants == o.m_variants
		    // …AND THE COMPOSITION-WIDE SETS. A property asks this to tell "changed" from "the same",
		    // so a member left out of here is a member whose edit leaves the configuration looking
		    // unmodified — and Save with nothing to do.
		    && m_selected == o.m_selected;
	}
	bool operator!=(const ibCompositionDescription& o) const { return !(*this == o); }

	// ⭐⭐ IS THERE ANYTHING TO READ — one question for both, and the reason the two are one format.
	// TECHNICALLY A MAIN TABLE IS `SELECT * FROM <table>` (Max, 2026-08-23): a list's table is not a
	// second kind of source beside the query, it is the DEGENERATE one. So "has a source" is "has a
	// query or has a table", and nothing downstream has to ask which sort of thing it is looking at.
	//
	// ⚠ The table is still kept as a NUMBER rather than being folded into the text, because a list
	// takes more than rows from it: its commands, its icon and the value a choice hands back are the
	// TABLE's, and none of those can be recovered from a rendered `SELECT *`.
	bool IsOk() const { return HasMainTable() || !m_query.IsEmpty(); }
};

// ---------------------------------------------------------------------------
// THE *Memory PAIRS — one per description, exactly as ibSourceDescriptionMemory
// and ibTypeDescriptionMemory are one per theirs. Each knows only its own part;
// the composition's pair COMBINES them and knows no part's shape.
//
// 🛑 THEY ARE IN ONE FILE ON PURPOSE (Max: "take all the serialisation you have
// spread around the settings and put it in one file"). What a composition
// consists of used to be written in three places at once — the composition, the
// list settings and the dynamic list — so "what is in a saved composition" had
// no single answer, and a format change had to be made three times to stay one
// format.
// ---------------------------------------------------------------------------
// ⭐⭐ EVERY PAIR SPEAKS NODES, all the way down — and that is the point of the whole change. The
// STRUCTURE is the spreadsheet's (a matryoshka of descriptions, each with its own pair); the
// STORAGE is not. A spreadsheet still writes its node as one Binary blob, and a blob reaches JSON
// as base64: unreadable, undiffable, uneditable. It simply has not been moved over yet.
//
// A node costs nothing extra and buys the file: ibBinaryProvider writes the same tree as bytes,
// ibJsonProvider writes it as JSON, so SAVING A COMPOSER TO A FILE is handing its node to a
// provider — and whether that file is JSON or binary becomes the caller's choice rather than a
// property of the format.
class BACKEND_API ibFilterDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, ibFilterDescription& filter, const class ibMetaData* metaData = nullptr);
	static bool WriteNode(ibDataNode& node, const ibFilterDescription& filter);
};

class BACKEND_API ibSortDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, ibSortDescription& sort);
	static bool WriteNode(ibDataNode& node, const ibSortDescription& sort);
};

class BACKEND_API ibGroupDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, ibGroupDescription& group);
	static bool WriteNode(ibDataNode& node, const ibGroupDescription& group);
};

// THE COMBINATION — the three parts, each into its own node. What a variant stores and what a list
// calls its settings is this, and neither of them says how any part is written.
class BACKEND_API ibSettingsDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, ibSettingsDescription& settings, const class ibMetaData* metaData = nullptr);
	static bool WriteNode(ibDataNode& node, const ibSettingsDescription& settings);
};

// ⭐ SETTINGS THAT CANNOT BE APPLIED ARE REFUSED, not silently dropped. A condition with no field,
// or a value whose type the field cannot hold, would either narrow the list by nothing or make the
// query lie — so this RAISES (ibBackendCoreException) rather than answering false.
//
// ONE CHECK, TWO FACES: the runtime gets an exception it can catch; a settings window catches the
// same one and shows the message as a warning instead of closing. Nothing is validated twice, and
// the rules live where the data lives.
BACKEND_API void ibValidateSettings(const ibSettingsDescription& settings);

// ⭐ ONE PAIR PER NODE, then combined upwards (Max, 2026-08-23): a level writes its settings through
// their pair and its CHILDREN through itself, an output combines its two axes of levels, the
// structure combines outputs — and the composition combines everything. No storey of that chain
// knows the shape of the one below it.
//
// (A LEVEL-FIELD pair stood here, writing {path, unfold}. What a level groups by is its GROUPING, so
//  ibGroupDescriptionMemory writes it — the second pair went with the second type.)
class BACKEND_API ibLevelDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, ibLevelDescription& level, const class ibMetaData* metaData = nullptr);
	static bool WriteNode(ibDataNode& node, const ibLevelDescription& level);
};

class BACKEND_API ibOutputDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, ibOutputDescription& output, const class ibMetaData* metaData = nullptr);
	static bool WriteNode(ibDataNode& node, const ibOutputDescription& output);
};

// THE STRUCTURE of a variant — the outputs, one after another.
class BACKEND_API ibStructureDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, std::vector<ibOutputDescription>& outputs, const class ibMetaData* metaData = nullptr);
	static bool WriteNode(ibDataNode& node, const std::vector<ibOutputDescription>& outputs);
};

// THE PARAMETERS. `full` writes the declared type and the two flags as well — a VARIANT keeps only
// what it owns (name, value, expression), because the declared type is stated once, on the
// composition's own list, and a variant that restated it could disagree with it.
class BACKEND_API ibParameterDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, std::vector<ibParameterDescription>& parameters,
	                     const class ibMetaData* metaData = nullptr);
	static bool WriteNode(ibDataNode& node, const std::vector<ibParameterDescription>& parameters,
	                      bool full, const class ibMetaData* metaData = nullptr);
};

// THE RESOURCES — composition-level, because that is what they are today: one list in one store,
// shared by every variant. Writing them per variant would claim a snapshot nobody takes.
class BACKEND_API ibResourceDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, std::vector<ibResourceDescription>& resources);
	static bool WriteNode(ibDataNode& node, const std::vector<ibResourceDescription>& resources);
};

// THE PACKAGES — the query's selects, each with what its own fields are called. Written only where
// somebody said something, so an untouched composition adds nothing to its file.
class BACKEND_API ibSelectDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, std::vector<ibSelectDescription>& selects);
	static bool WriteNode(ibDataNode& node, const std::vector<ibSelectDescription>& selects);
};

// THE VARIANTS — each its own node, carrying settings + structure + its own parameter values.
class BACKEND_API ibVariantDescriptionMemory {
public:
	static bool ReadNode(const ibDataNode& node, std::vector<ibVariantDescription>& variants, const class ibMetaData* metaData = nullptr);
	static bool WriteNode(ibDataNode& node, const std::vector<ibVariantDescription>& variants);
};

// ⭐ THE OUTERMOST ONE — the door a property, a metaobject or a file goes through. It combines the
// parts and knows the shape of none of them.
class BACKEND_API ibCompositionDescriptionMemory {
public:
	// ⚠ THE OUTER DOOR TAKES A VALUE, not a node — the shape ibSourceDescriptionMemory and
	// ibTypeDescriptionMemory have, so a property is two one-line methods:
	//
	//     return ibCompositionDescriptionMemory::ReadNode(value, GetValueAsCompositionDesc());
	//
	// The node inside the value is this description's OWN: everything it writes is a child or a
	// property of that node, and nothing of it reaches the metaobject that carries the property.
	static bool ReadNode(const class ibDataValue& value, ibCompositionDescription& composition, const class ibMetaData* metaData = nullptr);
	static bool WriteNode(class ibDataValue& value, const ibCompositionDescription& composition);

	// …and the same over a node already in hand — what a LIST reads, since its own node carries the
	// composer's parts beside its own (the view, the dynamic-read flag).
	static bool ReadNode(const ibDataNode& node, ibCompositionDescription& composition, const class ibMetaData* metaData = nullptr);
	static bool WriteNode(ibDataNode& node, const ibCompositionDescription& composition);
};

// ⭐⭐ WHAT THIS NODE IS, IN WORDS — the reverse of the ids this file writes, empty for anything else.
//
// A composition's parts are typed by SYNTHETIC clsids (`make_clsid("CompositionOutput", …)`): they
// identify a node when it is read back and they are registered nowhere, because nothing constructs a
// value from them. That is right for the FORMAT and wrong for a READER: rendered as JSON the tree
// came out `"NodeType": 67799176431653306`, seventeen digits where every other answer in the system
// says a word, and nothing anywhere could turn one into the other (measured over MCP, 2026-09-02).
//
// The names exist — they are the literals the ids are made FROM, ten lines up from here. This hands
// them back, so a reader of the tree sees `"NodeType": "CompositionOutput"`.
BACKEND_API wxString ibCompositionNodeName(ibClassID clsid);

#endif // !__COMPOSITION_DESCRIPTION_H__
