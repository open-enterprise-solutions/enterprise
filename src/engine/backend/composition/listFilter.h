#ifndef __LIST_FILTER_H__
#define __LIST_FILTER_H__

#include "backend/compiler/enumUnit.h"
#include "backend/typeDescription.h"   // ibTypeDescription (filter field typing)

class ibValueMetaObjectGenericData;
class ibDataComposer;
class ibDataNode;

// ---------------------------------------------------------------------------
// Dynamic-list settings — runtime (script-visible) objects mirroring the
// reference "List settings": Filter, Order (Sort), Group, held by a
// ListSettings container (≈ SettingsComposer).
//
// Everything is a runtime value: the comparison kind / sort direction are
// runtime enumerations; the filter / sort / group lines and collections are
// runtime values. The fetch path reads them and feeds ibDataComposer
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
	const ibValue& GetFilterValue() const { return m_value; }

	// Runtime field typing — like ibFilterData. The form edits the value THROUGH
	// the runtime (AdjustValue / CreateObject / QuickChoice / ProcessChoice) using
	// this type, not a plain text box. Set from the queryable column when the
	// filter row is built off a source.
	ibMetaID GetModel() const { return m_model; }
	const ibTypeDescription& GetTypeDescription() const { return m_typeDescription; }
	void SetTypeInfo(const ibMetaID& model, const ibTypeDescription& typeDesc) { m_model = model; m_typeDescription = typeDesc; }
	void SetUse(bool use) { m_use = use; }
	void SetFilterValue(const ibValue& value) { m_value = value; }

private:
	bool              m_use;
	wxString          m_field;
	ibComparisonKind  m_comparison;
	ibValue           m_value;
	ibMetaID          m_model = wxNOT_FOUND;   // the field's id (queryable column id)
	ibTypeDescription m_typeDescription;       // the field's type — for AdjustValue / choice
};

// The Filter — an ordered collection of FilterItem.
//   list.Filter.Add(field, comparison, value) / .Count() / .Get(i) / .Clear()
class BACKEND_API ibValueFilterList : public ibValueDynamicMembers {
public:
	enum Method { enAdd = 0, enCount, enGet, enClear };

	ibValueFilterList();
	virtual ~ibValueFilterList() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool IsEmpty() const override { return m_items.empty(); }
	virtual wxString GetString() const override;

	size_t Count() const { return m_items.size(); }
	ibValueFilterItem* GetItem(size_t idx) const {
		return idx < m_items.size() ? static_cast<ibValueFilterItem*>(m_items[idx]) : nullptr;
	}
	ibValueFilterItem* Add(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use = true);
	void Clear() { m_items.clear(); }

private:
	std::vector<ibValuePtr<ibValueFilterItem>> m_items;
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
	bool IsAscending() const { return m_direction == ibSortDirection_Ascending; }

private:
	wxString        m_field;
	ibSortDirection m_direction;
};

// The Sort — an ordered collection of SortItem.
class BACKEND_API ibValueSortList : public ibValueDynamicMembers {
public:
	enum Method { enAdd = 0, enCount, enGet, enClear };

	ibValueSortList();
	virtual ~ibValueSortList() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool IsEmpty() const override { return m_items.empty(); }
	virtual wxString GetString() const override;

	size_t Count() const { return m_items.size(); }
	ibValueSortItem* GetItem(size_t idx) const {
		return idx < m_items.size() ? static_cast<ibValueSortItem*>(m_items[idx]) : nullptr;
	}
	ibValueSortItem* Add(const wxString& field, ibSortDirection direction = ibSortDirection_Ascending);
	void Clear() { m_items.clear(); }

private:
	std::vector<ibValuePtr<ibValueSortItem>> m_items;
};

// ======================== Group ============================================

// The Group — an ordered list of grouping field paths (a flat list of
// fields). Items are strings (the field path).
//   list.Group.Add("Producer") / .Count() / .Get(i) / .Clear()
class BACKEND_API ibValueGroupList : public ibValueDynamicMembers {
public:
	enum Method { enAdd = 0, enCount, enGet, enClear };

	ibValueGroupList();
	virtual ~ibValueGroupList() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool IsEmpty() const override { return m_fields.empty(); }
	virtual wxString GetString() const override;

	size_t Count() const { return m_fields.size(); }
	wxString GetField(size_t idx) const { return idx < m_fields.size() ? m_fields[idx] : wxString(); }
	void Add(const wxString& field) { m_fields.push_back(field); }
	void Clear() { m_fields.clear(); }

private:
	std::vector<wxString> m_fields;
};

// ====================== ListSettings (SettingsComposer) ====================

// The container that holds Filter / Order / Group — the runtime mirror of the
// reference SettingsComposer. Owned by every list; surfaced as the list's
// "Settings" property (and the convenience "Filter" / "Order" / "Group").
class BACKEND_API ibValueListSettings : public ibValueDynamicMembers {
public:
	enum Prop { enFilter = 0, enOrder, enGroup };

	ibValueListSettings();
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

	// (Source / query config and the ms_showDialog hook are GONE: the source is now a
	// property ON the dynamic list — m_propertySource — and the settings form is opened
	// from the frontend property action, not a backend function pointer.)

private:
	ibValuePtr<ibValueFilterList> m_filter;
	ibValuePtr<ibValueSortList>   m_order;
	ibValuePtr<ibValueGroupList>  m_group;
};

// Apply the dynamic-list settings to a composer: Filter, Sort, Group→TotalBy.
// Dot-walk fields ("Ref.Owner") resolve to auto-JOINs on the
// door. Shared by the legacy list fetch path (objectListQuery) AND the unified
// ibValueDynamicList — one source of truth.
//
// The per-aspect forms let a caller apply only part of the settings — e.g. a grouping
// drill applies Filters + Sorts onto a scoped composer but supplies its OWN per-level
// TotalBy instead of the full Group set. ibApplyDynamicSettings = all three in order.
BACKEND_API void ibApplyDynamicFilters(ibDataComposer& composer, const ibValueListSettings* settings);
BACKEND_API void ibApplyDynamicSorts  (ibDataComposer& composer, const ibValueListSettings* settings);
BACKEND_API void ibApplyDynamicGroups (ibDataComposer& composer, const ibValueListSettings* settings);
BACKEND_API void ibApplyDynamicSettings(ibDataComposer& composer, const ibValueListSettings* settings);

#endif // __LIST_FILTER_H__
