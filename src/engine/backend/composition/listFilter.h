#ifndef __LIST_FILTER_H__
#define __LIST_FILTER_H__

#include "backend/compiler/enumUnit.h"
#include "backend/composition/compositionField.h"   // the LEFT side of a filter line — a FIELD, not a string
#include "backend/query/queryAst.h"    // ibQueryDimUnfold — the grouping VID (Elements / Hierarchy / HierarchyOnly), lifted to L5

#include <functional>                  // std::function — the FACADE-mode refresh callback (model owns the refresh)

class ibValueMetaObjectGenericData;
class ibDataDBComposer;
class ibDataComposer;
class ibValueModel;
class ibDataNode;

// Filter / Order / Group come in TWO MODES (Max: "the form is transactional; the runtime wrapper mutates
// immediately; refresh lives in the model"):
//   * BUFFER  — own storage, edited transactionally; the settings DIALOG's copy (load on open, commit on OK).
//   * FACADE  — a live view over the composer (the store): Add/Clear write the composer IMMEDIATELY and fire the
//               model's refresh callback; the script's `list.Settings` is this. Default-ctor = buffer; the
//               (composer, onChange) ctor = facade.

// ---------------------------------------------------------------------------
// Dynamic-list settings — runtime (script-visible) objects mirroring the
// reference "List settings": Filter, Order (Sort), Group, held by a
// ListSettings container (≈ SettingsComposer).
//
// Everything is a runtime value: the comparison kind / sort direction are
// runtime enumerations; the filter / sort / group lines and collections are
// runtime values. The fetch path reads them and feeds ibDataDBComposer
// (.Filter / .Sort / .TotalBy). Field is a PATH (dot-walk like "Ref.Owner"
// resolves to an auto-JOIN on the door). Source stays the main table for now;
// the named DynamicList class and the designer settings form come next.
// ---------------------------------------------------------------------------

// ============================ enumerations =================================

// The comparison kind. PLAIN enum (not enum class): ibValueEnumeration<T> uses
// T as a map key AND converts it to ibNumber (enum→int→ibNumber).
enum ibComparisonKind {
	ibComparisonKind_Equal = 0,
	ibComparisonKind_NotEqual,
	ibComparisonKind_Greater,
	ibComparisonKind_Less,
	ibComparisonKind_GreaterEqual,
	ibComparisonKind_LessEqual,
	ibComparisonKind_Contains,   // → LIKE
};

// Map a comparison kind to the composer's language operator spelling.
inline wxString ComparisonKindToOp(ibComparisonKind kind) {
	switch (kind) {
	case ibComparisonKind_Equal:        return wxT("=");
	case ibComparisonKind_NotEqual:     return wxT("<>");
	case ibComparisonKind_Greater:      return wxT(">");
	case ibComparisonKind_Less:         return wxT("<");
	case ibComparisonKind_GreaterEqual: return wxT(">=");
	case ibComparisonKind_LessEqual:    return wxT("<=");
	case ibComparisonKind_Contains:     return wxT("LIKE");
	}
	return wxT("=");
}

// Inverse — the composer's stored operator spelling back to the comparison kind (for loading the dialog
// buffer FROM the composer: composer.GetFilterAt yields the op string, the FilterItem wants the kind).
inline ibComparisonKind OpToComparisonKind(const wxString& op) {
	if (op == wxT("<>"))   return ibComparisonKind_NotEqual;
	if (op == wxT(">"))    return ibComparisonKind_Greater;
	if (op == wxT("<"))    return ibComparisonKind_Less;
	if (op == wxT(">="))   return ibComparisonKind_GreaterEqual;
	if (op == wxT("<="))   return ibComparisonKind_LessEqual;
	if (op == wxT("LIKE")) return ibComparisonKind_Contains;
	return ibComparisonKind_Equal;
}

// The sort direction.
enum ibSortDirection {
	ibSortDirection_Ascending = 0,
	ibSortDirection_Descending,
};

// HOW A GROUP JOINS ITS CHILDREN. A filter is a TREE: the root is a group, and
// a group holds conditions and other groups. Without this every filter is an
// implicit AND of a flat list, which cannot say "this AND (that OR the other)"
// — the shape most real filters take the moment they stop being trivial.
enum ibFilterGroupKind {
	ibFilterGroupKind_And = 0,
	ibFilterGroupKind_Or,
	ibFilterGroupKind_Not,      // negates the AND of its children
};

// HOW A USER MEETS A CONDITION. A property of the SETTING, not of the data:
// the same condition is applied either way, this says whether the user is
// offered it, and where.
enum ibFilterDisplayMode {
	ibFilterDisplayMode_Normal = 0,     // in the settings form
	ibFilterDisplayMode_QuickAccess,    // also in the list header — the ones people change daily
	ibFilterDisplayMode_Inaccessible,   // applied, never shown
};

// Map a group kind to the language's operator spelling.
inline wxString FilterGroupKindToOp(ibFilterGroupKind kind) {
	return kind == ibFilterGroupKind_Or ? wxT("OR") : wxT("AND");
}

// Runtime enumeration "FilterGroupKind".
class BACKEND_API ibValueEnumFilterGroupKind : public ibValueEnumeration<ibFilterGroupKind> {
public:
	ibValueEnumFilterGroupKind() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		// Captions are the OPERATOR alone — the choice opens on a group's operator
		// cell, and it must offer what it sets (see ibValueFilterGroup::GetString).
		AddEnumeration(ibFilterGroupKind_And, wxT("And"), _("And"));
		AddEnumeration(ibFilterGroupKind_Or,  wxT("Or"),  _("Or"));
		AddEnumeration(ibFilterGroupKind_Not, wxT("Not"), _("Not"));
	}
};

// Runtime enumeration "FilterDisplayMode".
class BACKEND_API ibValueEnumFilterDisplayMode : public ibValueEnumeration<ibFilterDisplayMode> {
public:
	ibValueEnumFilterDisplayMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibFilterDisplayMode_Normal,       wxT("Normal"),       _("Normal"));
		AddEnumeration(ibFilterDisplayMode_QuickAccess,  wxT("QuickAccess"),  _("Quick access"));
		AddEnumeration(ibFilterDisplayMode_Inaccessible, wxT("Inaccessible"), _("Inaccessible"));
	}
};

// Runtime enumeration "GroupKind" — HOW a grouping unfolds. It was a bare C++ enum
// (ibQueryDimUnfold) that no form could offer, so a grouping's kind could not be
// changed at all; as a registered enumeration it is picked exactly like a
// comparison or a sort direction.
class BACKEND_API ibValueEnumGroupKind : public ibValueEnumeration<ibQueryDimUnfold> {
public:
	ibValueEnumGroupKind() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibQueryDimUnfold::Elements,       wxT("Elements"),       _("Elements"));
		AddEnumeration(ibQueryDimUnfold::Hierarchy,      wxT("Hierarchy"),      _("Elements and hierarchy"));
		AddEnumeration(ibQueryDimUnfold::HierarchyOnly,  wxT("HierarchyOnly"),  _("Hierarchy only"));
	}
};

// Runtime enumeration "ComparisonKind".
class BACKEND_API ibValueEnumComparisonKind : public ibValueEnumeration<ibComparisonKind> {
public:
	ibValueEnumComparisonKind() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibComparisonKind_Equal,        wxT("Equal"),        _("Equal"));
		AddEnumeration(ibComparisonKind_NotEqual,     wxT("NotEqual"),     _("Not equal"));
		AddEnumeration(ibComparisonKind_Greater,      wxT("Greater"),      _("Greater"));
		AddEnumeration(ibComparisonKind_Less,         wxT("Less"),         _("Less"));
		AddEnumeration(ibComparisonKind_GreaterEqual, wxT("GreaterEqual"), _("Greater or equal"));
		AddEnumeration(ibComparisonKind_LessEqual,    wxT("LessEqual"),    _("Less or equal"));
		AddEnumeration(ibComparisonKind_Contains,     wxT("Contains"),     _("Contains"));
	}
};

// Runtime enumeration "SortDirection".
class BACKEND_API ibValueEnumSortDirection : public ibValueEnumeration<ibSortDirection> {
public:
	ibValueEnumSortDirection() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibSortDirection_Ascending,  wxT("Ascending"),  _("Ascending"));
		AddEnumeration(ibSortDirection_Descending, wxT("Descending"), _("Descending"));
	}
};

// ============================ Filter =======================================

// One condition — { Use, Left, Comparison, Right, DisplayMode, Presentation }.
//   New FilterItem(left, comparison, right [, use])
//
// BOTH SIDES ARE VALUES, and either may be a CompositionField. That is the whole
// point of the shape: `Amount > 100` compares a field with a number, `Price >
// Cost` compares a field with a FIELD, and `True = True` compares two constants.
// One rule instead of three special cases — and the picker offers the same
// choice on both sides (a data type, or a field of the source).
class BACKEND_API ibValueFilterItem : public ibValueDynamicMembers {
public:
	enum Prop { enUse = 0, enLeft, enComparison, enRight, enDisplayMode, enPresentation };

	ibValueFilterItem();
	ibValueFilterItem(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use = true);
	ibValueFilterItem(const ibValue& left, ibComparisonKind comparison, const ibValue& right, bool use = true);
	virtual ~ibValueFilterItem() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool IsEmpty() const override { return false; }
	virtual wxString GetString() const override;

	bool GetUse() const { return m_use; }

	// THE TWO SIDES, as values.
	const ibValue& GetLeft() const { return m_left; }
	const ibValue& GetRight() const { return m_right; }
	void SetLeft(const ibValue& left) { m_left = left; }
	void SetRight(const ibValue& right) { m_right = right; }

	// THE FIELD ON EITHER SIDE, when there is one — null when that side holds a
	// plain value. This is what the callers who need a PATH ask (the composer
	// builds a query from paths), and what the dialog asks to know whether to
	// offer a field picker or a value editor.
	ibValueCompositionField* GetLeftField() const;
	ibValueCompositionField* GetRightField() const;

	// The comparison — a registered ENUMERATION (`ComparisonKind`), not a raw
	// number: it is what a script writes (`ComparisonKind.Equal`), what the
	// dialog lists, and what a saved setting round-trips.
	ibComparisonKind GetComparison() const { return m_comparison; }
	void SetComparison(ibComparisonKind comparison) { m_comparison = comparison; }

	// HOW THE USER MEETS THIS CONDITION — also an enumeration. A quick-access
	// condition is offered in the list header; an inaccessible one is applied but
	// not shown. It is a property of the SETTING, not of the data.
	ibFilterDisplayMode GetDisplayMode() const { return m_displayMode; }
	void SetDisplayMode(ibFilterDisplayMode mode) { m_displayMode = mode; }

	// The user's own label for the line, when the generated one will not do.
	const wxString& GetPresentation() const { return m_presentation; }
	void SetPresentation(const wxString& presentation) { m_presentation = presentation; }

	void SetUse(bool use) { m_use = use; }

protected:
	// A condition packs like any other value (compiler/valueSerialization.h): the
	// base writes the type, this writes the contents — and BOTH SIDES pack
	// themselves, so a field travels as a field and a value as a value.
	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;

public:
	// ---- the older, path-shaped surface -----------------------------------
	// Kept because the composer and the fetch path speak in paths, and because
	// most conditions really are "a field against a value". They answer for the
	// LEFT side; a left side that is not a field reads as an empty path.
	ibValueCompositionField* GetFieldObject() const;
	wxString GetField() const;
	void SetField(ibValueCompositionField* field);
	const ibValue& GetFilterValue() const { return m_right; }
	void SetFilterValue(const ibValue& value) { m_right = value; }

	// Runtime field typing — forwarded to the left field, which owns it now. The
	// form edits the RIGHT side through the runtime (AdjustValue / CreateObject /
	// QuickChoice) using this type, not a plain text box.
	ibMetaID GetLeafId() const;
	const ibTypeDescription& GetTypeDescription() const;
	void SetTypeInfo(const ibMetaID& leafId, const ibTypeDescription& typeDesc);

	// WHAT THE RIGHT-HAND EDITOR SHOULD OFFER.
	//
	// The left side decides: a FIELD lends its own type (a reference field gets
	// the quick choice, a boolean field the two-item drop-down, a number field a
	// number box); a plain value on the left lends the type it already is, which
	// is what makes `True = True` editable as two drop-downs rather than as two
	// text boxes.
	//
	// Empty when neither says anything — an unbound field on the left — and the
	// caller then offers the type picker instead of guessing.
	ibTypeDescription GetRightTypeDescription() const;

private:
	bool                m_use;
	ibValue             m_left;          // a field, or a plain value
	ibComparisonKind    m_comparison;
	ibValue             m_right;         // a field, or a plain value
	ibFilterDisplayMode m_displayMode = ibFilterDisplayMode_Normal;
	wxString            m_presentation;  // user override; empty = generate from the sides
};

// A GROUP of conditions — the branch node of the filter tree.
//
// It holds conditions and other groups, in order, and joins them with its own
// kind (AND / OR / NOT). The root of every filter is a group, which is why the
// settings form shows a top line called "Filter" that everything hangs under:
// that line IS the root group, and changing it from AND to OR changes the whole
// filter without touching a single condition.
class BACKEND_API ibValueFilterGroup : public ibValueDynamicMembers {
public:
	enum Prop { enUse = 0, enKind, enDisplayMode, enPresentation };
	enum Method { enAdd = 0, enAddGroup, enCount, enGet, enRemove, enClear };

	ibValueFilterGroup();
	explicit ibValueFilterGroup(ibFilterGroupKind kind, bool use = true);
	virtual ~ibValueFilterGroup() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool IsEmpty() const override { return m_children.empty(); }
	virtual wxString GetString() const override;

	bool GetUse() const { return m_use; }
	void SetUse(bool use) { m_use = use; }

	ibFilterGroupKind GetKind() const { return m_kind; }
	void SetKind(ibFilterGroupKind kind) { m_kind = kind; }

	ibFilterDisplayMode GetDisplayMode() const { return m_displayMode; }
	void SetDisplayMode(ibFilterDisplayMode mode) { m_displayMode = mode; }

	const wxString& GetPresentation() const { return m_presentation; }
	void SetPresentation(const wxString& presentation) { m_presentation = presentation; }

	// CHILDREN ARE VALUES, of two shapes: a condition or a nested group. The
	// caller asks which it got rather than being handed two parallel lists —
	// order matters and interleaves, so two lists could not express it.
	size_t Count() const { return m_children.size(); }
	ibValue GetChild(size_t idx) const;
	ibValueFilterItem* GetItem(size_t idx) const;
	ibValueFilterGroup* GetGroup(size_t idx) const;

	ibValueFilterItem* Add(const ibValue& left, ibComparisonKind comparison, const ibValue& right, bool use = true);
	ibValueFilterGroup* AddGroup(ibFilterGroupKind kind = ibFilterGroupKind_And);
	void Append(const ibValue& child);
	void Remove(size_t idx);
	void Clear() { m_children.clear(); }

	// GROUP THE CHOSEN LINES — the context-menu verb. The selected children move
	// into a new group in place of the FIRST of them, so their order relative to
	// everything else is kept; the inverse (Ungroup) splices a group's children
	// back where the group stood.
	// ONE STEP, WITHIN THIS GROUP. A line does not change parent by moving — that
	// would change what it means without the user asking for it. Returns the new
	// index, or the old one when the line is already at the end it is moving to.
	size_t MoveChild(size_t idx, int delta);
	// Where a child sits, or Count() when it is not ours.
	size_t IndexOf(const ibValue& child) const;

	ibValueFilterGroup* GroupChildren(const std::vector<size_t>& indexes, ibFilterGroupKind kind = ibFilterGroupKind_And);
	bool UngroupChild(size_t idx);

protected:
	// The tree packs itself, children and all — each child through its own
	// Serialize, so a nested group needs no special case here.
	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;

private:
	bool                m_use = true;
	ibFilterGroupKind   m_kind = ibFilterGroupKind_And;
	ibFilterDisplayMode m_displayMode = ibFilterDisplayMode_Normal;
	wxString            m_presentation;
	std::vector<ibValue> m_children;   // conditions and groups, in order
};

// ==================== the tree AS AN EXPRESSION =============================

// A filter TREE as a query AST — the SAME shape `Restrict` compiles to
// (query/queryAst.h). This is the real output of a filter: an expression the
// engine already knows how to lower, not text somebody has to parse back.
//
// Values it mentions are registered as &parameters of the composer, so the
// expression refers to them by name and no value is ever inlined.
// Null when the tree says nothing — every condition switched off, or none
// finished.
BACKEND_API ibQueryAstExprPtr ibBuildFilterAst(class ibDataComposer& composer, const class ibValueFilterGroup* root);

// The same thing as TEXT, for the composer, which speaks the query language.
// Rendered by the ordinary AST renderer (queryRender.h) — so parentheses and
// precedence are its business, not the filter's.
BACKEND_API wxString ibRenderFilterTree(class ibDataComposer& composer, const class ibValueFilterGroup* root);

// The Filter — an ordered collection of FilterItem.
//   list.Filter.Add(field, comparison, value) / .Count() / .Get(i) / .Clear()
class BACKEND_API ibValueFilterList : public ibValueDynamicMembers {
public:
	enum Method { enAdd = 0, enCount, enGet, enClear };

	// ONE STORE: the settings' ROOT GROUP. This list is the flat DOOR onto it —
	// what a script writes (`List.Settings.Filter.Add(…)`) and what the quick
	// column filter uses — while the tree is what the dialog edits and what gets
	// saved. They were two stores for one day, and everything that touched only
	// one of them (Clear, the quick filter) silently did nothing.
	explicit ibValueFilterList(ibValueFilterGroup* root = nullptr);            // BUFFER mode; no root = nothing to hold
	ibValueFilterList(ibValueFilterGroup* root, ibValueModel& model, std::function<void()> onChange);   // FACADE mode (live)
	virtual ~ibValueFilterList() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool IsEmpty() const override;
	virtual wxString GetString() const override;

	size_t Count() const;
	ibValueFilterItem* GetItem(size_t idx) const;
	// Follow the settings when their tree is replaced wholesale.
	void SetRoot(ibValueFilterGroup* root) { m_root = root; }
	// BY PATH — the ordinary call: a path names a field, and the field is built
	// here. BY FIELD — when the caller already holds one (the picker, a restored
	// setting), so its type and presentation survive instead of being re-derived.
	ibValueFilterItem* Add(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use = true);
	ibValueFilterItem* Add(ibValueCompositionField* field, ibComparisonKind comparison, const ibValue& value, bool use = true);
	void Clear();

private:
	// FACADE: resolve the model's composer LAZILY (it is polymorphic + lazily created, so it does not exist
	// when the model's ctor builds this facade — by first USE the model is fully constructed). null = BUFFER mode.
	ibDataComposer*   Composer() const;
	// FACADE: re-state the WHOLE tree on the composer after a mutation, so the
	// live list shows what the settings now say (and an emptied filter empties).
	void              ApplyToComposer();
	ibValuePtr<ibValueFilterGroup> m_ownRoot;     // only when standing alone (no settings behind it)
	ibValueFilterGroup*   m_root = nullptr;       // THE store — normally the settings' root group
	ibValueModel*         m_model = nullptr;      // FACADE target (the model); null = BUFFER mode
	std::function<void()> m_onChange;             // FACADE: fire the model's refresh after a mutation
};

// ========================= Sort ============================================

// One sort line — { Field, Direction }.
//   New SortItem(field [, direction])
class BACKEND_API ibValueSortItem : public ibValueDynamicMembers {
public:
	enum Prop { enField = 0, enDirection };

	ibValueSortItem();
	ibValueSortItem(const wxString& field, ibSortDirection direction = ibSortDirection_Ascending);
	virtual ~ibValueSortItem() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool IsEmpty() const override { return false; }
	virtual wxString GetString() const override;

	// THE SAME FIELD TYPE as a condition's left side. A sort, a grouping and a
	// filter all point at one field of one source, so they point at it the same
	// way: one picker, one presentation, one thing that gets saved.
	ibValueCompositionField* GetFieldObject() const { return m_field; }
	const wxString& GetField() const { return m_field->GetPath(); }
	void SetField(ibValueCompositionField* field);

	ibSortDirection GetDirection() const { return m_direction; }
	void SetDirection(ibSortDirection direction) { m_direction = direction; }   // inline edit from the settings dialog
	bool IsAscending() const { return m_direction == ibSortDirection_Ascending; }

private:
	ibValuePtr<ibValueCompositionField> m_field;   // never null; empty until chosen
	ibSortDirection m_direction;
};

// The Sort — an ordered collection of SortItem.
class BACKEND_API ibValueSortList : public ibValueDynamicMembers {
public:
	enum Method { enAdd = 0, enCount, enGet, enClear };

	ibValueSortList();                                                      // BUFFER mode (own storage)
	ibValueSortList(ibValueModel& model, std::function<void()> onChange);     // FACADE mode (live over the model's composer)
	virtual ~ibValueSortList() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool IsEmpty() const override;
	virtual wxString GetString() const override;

	size_t Count() const;
	ibValueSortItem* GetItem(size_t idx) const;
	ibValueSortItem* Add(const wxString& field, ibSortDirection direction = ibSortDirection_Ascending);
	void Clear();
	// ORDER IS THE POINT of a sort list — "by Date, then by Number" is a different
	// sort from "by Number, then by Date" — so removing and reordering are part of
	// the type, not something a form re-invents by clearing and re-adding.
	bool Remove(size_t idx);
	bool Move(size_t idx, int delta);

private:
	ibDataComposer*   Composer() const;        // FACADE: lazily resolve the model's composer; null = BUFFER mode
	ibValueModel*         m_model = nullptr;       // FACADE target (the model); null = BUFFER mode
	std::function<void()> m_onChange;             // FACADE: fire the model's refresh after a mutation
	std::vector<ibValuePtr<ibValueSortItem>> m_items;   // BUFFER storage (unused in facade mode)
};

// ======================== Group ============================================

// The Group — an ordered list of grouping items, each a { field path, KIND }. The KIND is the grouping
// VID: a plain DIMENSION group (group rows by the field's value) or a HIERARCHY group (the field is a
// reference whose own parent chain is unfolded into a tree — a hierarchical catalog). Hierarchy is just a grouping
// kind: add a hierarchy-group on a reference → the list becomes a tree; remove it → it falls back to flat.
//   list.Group.Add("Producer") / .Count() / .Get(i) / .Clear()
class BACKEND_API ibValueGroupList : public ibValueDynamicMembers {
public:
	enum Method { enAdd = 0, enCount, enGet, enClear };

	// One group line — { field path, KIND }. KIND = the grouping VID (Elements / Hierarchy / HierarchyOnly),
	// lifted from the L3/L4 TOTALS BY. A HIERARCHY kind on a reference field IS what makes the list a tree.
	// The field is the SAME type as everywhere else (compositionField.h); the KIND
	// is the grouping VID, which only a grouping has.
	struct Item {
		ibValuePtr<ibValueCompositionField> m_field;
		ibQueryDimUnfold m_kind = ibQueryDimUnfold::Elements;
	};

	ibValueGroupList();                                                     // BUFFER mode (own storage)
	ibValueGroupList(ibValueModel& model, std::function<void()> onChange);    // FACADE mode (live over the model's composer)
	virtual ~ibValueGroupList() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool IsEmpty() const override;
	virtual wxString GetString() const override;

	size_t Count() const;
	wxString GetField(size_t idx) const;
	ibValueCompositionField* GetFieldObject(size_t idx) const;
	ibQueryDimUnfold GetKind(size_t idx) const;
	bool IsHierarchy(size_t idx) const { return GetKind(idx) != ibQueryDimUnfold::Elements; }
	void Add(const wxString& field, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements);
	void Add(ibValueCompositionField* field, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements);
	void Clear();
	// ORDER IS THE NESTING — "by Warehouse, then by Item" is a different report
	// from the other way round — so a grouping list removes and reorders too.
	bool Remove(size_t idx);
	bool Move(size_t idx, int delta);

private:
	ibDataComposer*   Composer() const;        // FACADE: lazily resolve the model's composer; null = BUFFER mode
	ibValueModel*         m_model = nullptr;       // FACADE target (the model); null = BUFFER mode
	std::function<void()> m_onChange;             // FACADE: fire the model's refresh after a mutation
	std::vector<Item>     m_items;                // BUFFER storage (unused in facade mode)
};

// ====================== ListSettings (SettingsComposer) ====================

// The container that holds Filter / Order / Group — the runtime mirror of the
// reference SettingsComposer. Owned by every list; surfaced as the list's
// "Settings" property (and the convenience "Filter" / "Order" / "Group").
class BACKEND_API ibValueListSettings : public ibValueDynamicMembers {
public:
	enum Prop { enFilter = 0, enOrder, enGroup };

	// Filter / Order / Group container, in TWO MODES (see the file header):
	//   * FACADE  (composer, onChange) — a thin wrapper OVER the composer (the store). The MODEL owns ONE of
	//     these as its live runtime/script settings; a script's list.Settings.Filter.Add writes the composer
	//     immediately + fires the model refresh. (Max: "this is a system type that lives inside the model; the
	//     composer is handed to it; add a filter and it forwards it immediately.") This is the canonical owner.
	//   * BUFFER  (default ctor) — own storage, the settings DIALOG's transactional copy (load FROM the composer
	//     on open via ibLoadSettingsFromComposer, commit BACK on OK via ibCommitSettingsToComposer; Cancel = discard).
	ibValueListSettings();
	ibValueListSettings(ibValueModel& model, std::function<void()> onChange);
	virtual ~ibValueListSettings() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool IsEmpty() const override { return false; }
	virtual wxString GetString() const override { return wxT("ListSettings"); }

	ibValueFilterList* GetFilter() const { return m_filter; }
	ibValueSortList*   GetOrder()  const { return m_order; }
	ibValueGroupList*  GetGroup()  const { return m_group; }

	// THE FILTER AS A TREE — the root group, which is the line the settings form
	// shows at the top ("Filter") with everything hanging under it. Never null:
	// an empty filter is an empty ROOT GROUP, not the absence of one, so the form
	// always has a node to add to and the renderer always has somewhere to start.
	//
	// The flat GetFilter() above is the same filter seen as a list — what the
	// composer and the older callers speak. A tree that is only ever one level
	// deep reads identically through both.
	ibValueFilterGroup* GetFilterRoot() const { return m_filterRoot; }

	// THE TREE ARRIVES WHOLE sometimes — read back from a saved setting, or copied
	// from the live settings into the dialog's buffer. Replacing it has to take the
	// flat door along: they are one store, and a Filter list still pointing at the
	// tree that was replaced answers about a filter nobody is using any more.
	void SetFilterRoot(ibValueFilterGroup* root);

	// Object-level node save/load — Filter / Order / Group. Called by the dynamic list's
	// ReadData/WriteData so the settings persist on the form (the attached object's data).
	bool ReadData(const ibDataNode& node);
	bool WriteData(ibDataNode& node) const;

private:
	ibValuePtr<ibValueFilterGroup> m_filterRoot;   // the tree; the root is a group, always
	ibValuePtr<ibValueFilterList> m_filter;
	ibValuePtr<ibValueSortList>   m_order;
	ibValuePtr<ibValueGroupList>  m_group;
};

// ================ validation + the composer BRIDGE ==========================

// Transactional bridge between the composer (the committed settings STORE) and an ibValueListSettings (the
// settings DIALOG's edit BUFFER). The composer is never read+rebuilt every fetch any more — instead:
//   * the dialog OPENS  → ibLoadSettingsFromComposer (composer → buffer: show the current state for editing),
//   * the user EDITS the buffer (Filter / Order / Group items) transactionally,
//   * the dialog OKs    → ibCommitSettingsToComposer (buffer → composer: CLEAR then re-apply = the commit).
// Dot-walk fields ("Ref.Owner") resolve to auto-JOINs on the door. Cancel = do nothing (the composer, the
// truth, is untouched). Field is a PATH; the value travels as an auto-&parameter.
// `live` — the settings the model is actually running with, when the caller has
// them. A TREE filter reaches the composer as one expression, so it cannot be
// read back out of it; the live settings are where that tree still is, and the
// buffer takes a copy of it.
// SETTINGS THAT CANNOT BE APPLIED ARE REFUSED, not silently dropped. A condition
// with no field, or a value whose type the field cannot hold, would either narrow
// the list by nothing or make the query lie — so committing raises.
//
// ONE CHECK, TWO FACES: the runtime gets an ibBackendException it can catch; the
// settings form catches the same one and shows the message as a warning instead of
// closing. Nothing is validated twice, and the rules live where the data lives.
BACKEND_API void ibValidateSettings(const ibValueListSettings* settings);

BACKEND_API void ibLoadSettingsFromComposer (ibValueListSettings* settings, const ibDataComposer& composer,
	const ibValueListSettings* live = nullptr);
BACKEND_API void ibCommitSettingsToComposer (ibDataComposer& composer, const ibValueListSettings* settings);

#endif // __LIST_FILTER_H__
