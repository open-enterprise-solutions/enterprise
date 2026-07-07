#ifndef __LIST_FILTER_H__
#define __LIST_FILTER_H__

#include "backend/compiler/enumUnit.h"
#include "backend/typeDescription.h"   // ibTypeDescription (filter field typing)
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

// One filter line — { Use, Field, Comparison, Value }.
//   New FilterItem(field, comparison, value [, use])
class BACKEND_API ibValueFilterItem : public ibValueDynamicMembers {
public:
	enum Prop { enUse = 0, enField, enComparison, enValue };

	ibValueFilterItem();
	ibValueFilterItem(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use = true);
	virtual ~ibValueFilterItem() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool IsEmpty() const override { return false; }
	virtual wxString GetString() const override;

	bool GetUse() const { return m_use; }
	const wxString& GetField() const { return m_field; }
	ibComparisonKind GetComparison() const { return m_comparison; }
	void SetComparison(ibComparisonKind comparison) { m_comparison = comparison; }
	const ibValue& GetFilterValue() const { return m_value; }

	// Runtime field typing. The form edits the value THROUGH the runtime
	// (AdjustValue / CreateObject / QuickChoice / ProcessChoice) using this type,
	// not a plain text box. Set from the queryable column when the filter row is
	// built off a source.
	ibMetaID GetLeafId() const { return m_leafId; }
	const ibTypeDescription& GetTypeDescription() const { return m_typeDescription; }
	void SetTypeInfo(const ibMetaID& leafId, const ibTypeDescription& typeDesc) { m_leafId = leafId; m_typeDescription = typeDesc; }
	void SetUse(bool use) { m_use = use; }
	void SetFilterValue(const ibValue& value) { m_value = value; }

private:
	bool              m_use;
	wxString          m_field;
	ibComparisonKind  m_comparison;
	ibValue           m_value;
	ibMetaID          m_leafId = wxNOT_FOUND;   // the field's id (queryable column id)
	ibTypeDescription m_typeDescription;       // the field's type — for AdjustValue / choice
};

// The Filter — an ordered collection of FilterItem.
//   list.Filter.Add(field, comparison, value) / .Count() / .Get(i) / .Clear()
class BACKEND_API ibValueFilterList : public ibValueDynamicMembers {
public:
	enum Method { enAdd = 0, enCount, enGet, enClear };

	ibValueFilterList();                                                    // BUFFER mode (own storage)
	ibValueFilterList(ibValueModel& model, std::function<void()> onChange);   // FACADE mode (live over the model's composer)
	virtual ~ibValueFilterList() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool IsEmpty() const override;
	virtual wxString GetString() const override;

	size_t Count() const;
	ibValueFilterItem* GetItem(size_t idx) const;
	ibValueFilterItem* Add(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use = true);
	void Clear();

private:
	// FACADE: resolve the model's composer LAZILY (it is polymorphic + lazily created, so it does not exist
	// when the model's ctor builds this facade — by first USE the model is fully constructed). null = BUFFER mode.
	ibDataComposer*   Composer() const;
	ibValueModel*         m_model = nullptr;       // FACADE target (the model); null = BUFFER mode
	std::function<void()> m_onChange;             // FACADE: fire the model's refresh after a mutation
	std::vector<ibValuePtr<ibValueFilterItem>> m_items;   // BUFFER storage (unused in facade mode)
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

	const wxString& GetField() const { return m_field; }
	ibSortDirection GetDirection() const { return m_direction; }
	void SetDirection(ibSortDirection direction) { m_direction = direction; }   // inline edit from the settings dialog
	bool IsAscending() const { return m_direction == ibSortDirection_Ascending; }

private:
	wxString        m_field;
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
	struct Item { wxString m_field; ibQueryDimUnfold m_kind = ibQueryDimUnfold::Elements; };

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
	ibQueryDimUnfold GetKind(size_t idx) const;
	bool IsHierarchy(size_t idx) const { return GetKind(idx) != ibQueryDimUnfold::Elements; }
	void Add(const wxString& field, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements);
	void Clear();

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

	// Object-level node save/load — Filter / Order / Group. Called by the dynamic list's
	// ReadData/WriteData so the settings persist on the form (the attached object's data).
	bool ReadData(const ibDataNode& node);
	bool WriteData(ibDataNode& node) const;

private:
	ibValuePtr<ibValueFilterList> m_filter;
	ibValuePtr<ibValueSortList>   m_order;
	ibValuePtr<ibValueGroupList>  m_group;
};

// Transactional bridge between the composer (the committed settings STORE) and an ibValueListSettings (the
// settings DIALOG's edit BUFFER). The composer is never read+rebuilt every fetch any more — instead:
//   * the dialog OPENS  → ibLoadSettingsFromComposer (composer → buffer: show the current state for editing),
//   * the user EDITS the buffer (Filter / Order / Group items) transactionally,
//   * the dialog OKs    → ibCommitSettingsToComposer (buffer → composer: CLEAR then re-apply = the commit).
// Dot-walk fields ("Ref.Owner") resolve to auto-JOINs on the door. Cancel = do nothing (the composer, the
// truth, is untouched). Field is a PATH; the value travels as an auto-&parameter.
BACKEND_API void ibLoadSettingsFromComposer (ibValueListSettings* settings, const ibDataComposer& composer);
BACKEND_API void ibCommitSettingsToComposer (ibDataComposer& composer, const ibValueListSettings* settings);

#endif // __LIST_FILTER_H__
