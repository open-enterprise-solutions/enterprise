#ifndef __TABLE_VIEW_H__
#define __TABLE_VIEW_H__

#include <algorithm>
#include <vector>

#include <wx/variant.h>
#include <wx/dnd.h>             // For wxDragResult declaration only.
#include <wx/dynarray.h>
#include <wx/itemid.h>
#include <wx/font.h>

#include "backend/backend.h"
#include "backend/system/value/valueType.h"   // ibValue, ibTypeDescription, ibGuid for ibFilterRow

// ----------------------------------------------------------------------------
// ibDataViewCtrl globals
// ----------------------------------------------------------------------------

class BACKEND_API ibDataViewModel;

// Sort + filter descriptors live at the view layer — used by
// ibDataViewModel's header-arrow / folder-sort virtuals and by paged
// Fetch SQL builders.  Storage (the per-model m_sortOrder /
// m_filterRow members) still lives on ibValueModel in tableInfo.h.

enum ibComparisonType {
	ibComparisonType_Equal, // ==
	ibComparisonType_NotEqual, // !=
};

struct ibFilterRow {

	struct ibFilterData {
		unsigned int m_filterModel;
		ibGuid m_filterGuid;
		wxString m_filterName;
		wxString m_filterPresentation;
		ibComparisonType m_filterComparison;
		ibTypeDescription m_filterTypeDescription;
		ibValue m_filterValue;
		bool m_filterUse;
	public:
		ibFilterData(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
			ibComparisonType comparisonType, const ibTypeDescription& filterTypeDescription, const ibValue& filterValue,
			bool filterUse = false) :
			m_filterModel(filterModel),
			m_filterGuid(ibGuid::newGuid()),
			m_filterName(filterName),
			m_filterPresentation(filterPresentation),
			m_filterComparison(comparisonType),
			m_filterTypeDescription(filterTypeDescription),
			m_filterValue(filterValue),
			m_filterUse(filterUse) {
		}
	};

	std::vector< ibFilterData> m_filters;

public:

	void AppendFilter(unsigned int filterModel, const wxString& filterName,
		const ibTypeDescription& filterTypeDescription, const ibValue& filterValue) {
		m_filters.emplace_back(filterModel, filterName, filterName,
			ibComparisonType::ibComparisonType_Equal, filterTypeDescription, filterValue, false
		);
	}

	void AppendFilter(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
		const ibTypeDescription& filterTypeDescription, const ibValue& filterValue) {
		m_filters.emplace_back(filterModel, filterName, filterPresentation,
			ibComparisonType::ibComparisonType_Equal, filterTypeDescription, filterValue, false
		);
	}

	void AppendFilter(unsigned int filterModel, const wxString& filterName, const wxString& filterPresentation,
		ibComparisonType comparisonType, const ibTypeDescription& filterTypeDescription, const ibValue& filterValue,
		bool filterUse = false) {
		m_filters.emplace_back(filterModel, filterName, filterPresentation,
			comparisonType, filterTypeDescription, filterValue, filterUse
		);
	}

	ibFilterData* GetFilterByID(unsigned int filterModel) {
		auto iterator = std::find_if(m_filters.begin(), m_filters.end(), [filterModel](const ibFilterData& data) {
			return filterModel == data.m_filterModel; });
		if (iterator != m_filters.end())
			return &*iterator;
		return nullptr;
	}

	ibFilterData* GetFilterByName(const wxString& filterName) {
		auto iterator = std::find_if(m_filters.begin(), m_filters.end(), [filterName](const ibFilterData& data) {
			return filterName == data.m_filterName; });
		if (iterator != m_filters.end())
			return &*iterator;
		return nullptr;
	}

	void SetFilterByID(unsigned int filterModel, const ibValue& filterValue) {
		ibFilterData* data = GetFilterByID(filterModel);
		if (data != nullptr) {
			data->m_filterValue = filterValue;
			data->m_filterUse = true;
		}
	}

	void SetFilterByName(const wxString& filterName, const ibValue& filterValue) {
		ibFilterData* data = GetFilterByName(filterName);
		if (data != nullptr) {
			data->m_filterValue = filterValue;
			data->m_filterUse = true;
		}
	}

	bool UseFilter() const {
		return m_filters.size() > 0;
	}

	void ResetFilter() {
		for (auto& filter : m_filters) {
			filter.m_filterUse = false;
		}
	}
};

struct ibSortOrder {

	struct ibSortData {
		unsigned int m_sortModel;
		wxString m_sortName;
		wxString m_sortPresentation;
		bool m_sortAscending;
		bool m_sortEnable;
		bool m_sortSystem;
	public:
		ibSortData(unsigned int sortModel, const wxString& sortName, const wxString& sortPresentation = wxEmptyString, bool sortAscending = true, bool sortEnable = true, bool sortSystem = false) :
			m_sortModel(sortModel),
			m_sortName(sortName),
			m_sortPresentation(sortPresentation),
			m_sortAscending(sortAscending),
			m_sortEnable(sortEnable),
			m_sortSystem(sortSystem)
		{
		}
	};
	std::vector< ibSortData> m_sorts;
public:

	void AppendSort(unsigned int col_id, const wxString& name, bool ascending = true, bool use = true, bool system = false) {
		AppendSort(col_id, name, wxEmptyString, ascending, use, system);
	}

	void AppendSort(unsigned int col_id, const wxString& name, const wxString& presentation, bool ascending = true, bool use = true, bool system = false) {
		if (GetSortByID(col_id) == nullptr) m_sorts.emplace_back(col_id, name, presentation, ascending, use, system);
	}

	ibSortData* GetSortByID(unsigned int col_id) const {
		auto iterator = std::find_if(m_sorts.begin(), m_sorts.end(),
			[col_id](const ibSortData& data) {return col_id == data.m_sortModel; });
		if (iterator != m_sorts.end()) return const_cast<ibSortData*>(&*iterator);
		return nullptr;
	}

	unsigned int GetSortCount() const {
		return m_sorts.size();
	}

	// Insert (or re-enable) a system-sort entry at the front of
	// m_sorts so BuildOrderBy emits this column ahead of any user
	// column sort.  Idempotent — repeated calls just toggle the
	// existing entry's enable + ascending flags.  Used by the GUI
	// to opt a model into folder-first ordering on Tree /
	// Hierarchical view modes (sortID = ibValueModel::GetIsFolderSortID).
	void EnableSystemSort(unsigned int sortID, bool ascending) {
		auto it = std::find_if(m_sorts.begin(), m_sorts.end(),
			[sortID](const ibSortData& s) {
				return s.m_sortSystem && s.m_sortModel == sortID;
			});
		if (it == m_sorts.end()) {
			m_sorts.insert(m_sorts.begin(),
				ibSortData(sortID, wxEmptyString, wxEmptyString,
				           ascending, /*enable=*/true, /*system=*/true));
		} else {
			it->m_sortEnable    = true;
			it->m_sortAscending = ascending;
		}
	}

	// Disable (but keep) the system-sort entry for sortID.  Disabled
	// entries are skipped by BuildOrderBy.  Removal is intentional —
	// re-enabling later just flips the flag without re-inserting.
	void DisableSystemSort(unsigned int sortID) {
		auto it = std::find_if(m_sorts.begin(), m_sorts.end(),
			[sortID](const ibSortData& s) {
				return s.m_sortSystem && s.m_sortModel == sortID;
			});
		if (it != m_sorts.end())
			it->m_sortEnable = false;
	}
};

struct ibSortModel {
	unsigned int m_sortModel;
	bool m_sortAscending;
};

// ----------------------------------------------------------------------------
// ibDataViewCtrl flags
// ----------------------------------------------------------------------------

// size of a ibDataViewRenderer without contents:
#define wxDVC_DEFAULT_RENDERER_SIZE     20

// the default width of new (text) columns:
#define wxDVC_DEFAULT_WIDTH             80

// the default width of new toggle columns:
#define wxDVC_TOGGLE_DEFAULT_WIDTH      30

// the default minimal width of the columns:
#define wxDVC_DEFAULT_MINWIDTH          30

// The default alignment of ibDataViewRenderers is to take
// the alignment from the column it owns.
#define wxDVR_DEFAULT_ALIGNMENT         -1

// ---------------------------------------------------------
// ibDataViewItem
// ---------------------------------------------------------

// Make it a class and not a typedef to allow forward declaring it.
//
// Refcount-aware: rows in OES (ibValueTreeNode, ibValueTableRow, ...)
// inherit from wxRefCounter.  Storing the underlying pointer with
// auto-IncRef/DecRef means a row stays alive as long as any
// ibDataViewItem references it — solves selection-holds-evicted-row,
// frozen breadcrumb survival across model rebuilds, and dangling
// pointers after Clear().
//
// Constructor still accepts void* for ABI back-compat with existing
// callsites — they pass the row pointer (which IS a wxRefCounter
// subclass in all real usages), we cast internally.  GetID() returns
// void* for legacy code.
class BACKEND_API ibDataViewItem;
class BACKEND_API ibDataViewObject;

// Common base for any object held inside an ibDataViewItem.  Concrete
// row / node classes override the virtual hooks below; the data-view
// control then queries items directly (item.IsContainer / GetParent /
// operator==) instead of routing every check through the model.  The
// model's job shrinks to: produce items via Get*Fetch, accept writes,
// trigger UI refresh.
class BACKEND_API ibDataViewObject : public wxRefCounter
{
public:
	// Logical equality.  Default: pointer match.  Row classes compare
	// by data so the same business row matches across re-fetches
	// behind a fresh pointer.
	virtual bool IsEqualTo(const ibDataViewObject& other) const { return this == &other; }

	// True if the item can have children (a folder, a tree node with
	// expander).  Default: leaf.
	virtual bool IsContainer() const { return false; }

	// Item's logical parent inside the data source.  Default: no parent.
	// Tree-node subclasses override to walk up the live tree; row
	// subclasses can return the parent row recovered from a reference
	// attribute.  Named GetParentItem to avoid colliding with the
	// existing tree-node GetParent() that returns a typed pointer.
	virtual ibDataViewItem GetParentItem() const;

	// True iff the row/node is still attached to its owner model.
	// Refcount-aware ibDataViewItem may keep an object alive past
	// the model's Clear/Remove (the model nulls the back-pointer),
	// so script paths that try to read or mutate through a pinned-
	// but-detached row (e.g. ReturnLine cached on the form) need
	// this check to fail safely instead of dereferencing a dead
	// model.  Default: attached.  ibValueTableRow / ibValueTreeNode
	// override to report based on m_valueTable / m_valueTree.
	virtual bool IsAttached() const { return true; }
};

class BACKEND_API ibDataViewItem
{
public:
	// Storage mode discriminator.  ibDataViewItem holds a single
	// pointer-sized field but the SEMANTICS of that field depends on
	// which ctor populated it:
	//   Empty       — no item (m_id == nullptr).
	//   Refcounted  — m_id is a real ibDataViewObject* (wxRefCounter
	//                 subclass); copy/dtor IncRef/DecRef.  This is
	//                 OES paged-fetch rows, tree nodes, etc.
	//   RawId       — m_id is opaque: either a non-refcounted node
	//                 pointer (legacy ibDataViewTreeStoreNode,
	//                 delete-owned) or a virtual-list row tag
	//                 (wxUIntToPtr(row+1), not a real pointer).
	//                 Copy/dtor MUST skip refcount ops — calling
	//                 IncRef on a fake pointer is an AV.
	enum class Mode : uint8_t { Empty, Refcounted, RawId };

	ibDataViewItem() = default;
	// nullptr-literal disambiguator: keeps `ibDataViewItem(nullptr)`
	// callsites compiling without ambiguity between the typed and
	// void* overloads below.
	ibDataViewItem(std::nullptr_t) {}

	// Refcounted ctor — `pItem` IS a real ibDataViewObject; we IncRef
	// on construction and DecRef on destruction so copies pin the row
	// alive across model resets and selection survival.
	explicit ibDataViewItem(ibDataViewObject* pItem)
		: m_id(pItem), m_mode(pItem ? Mode::Refcounted : Mode::Empty)
	{
		if (m_id) m_id->IncRef();
	}

	// Raw-id ctor — escape hatch for two non-refcounted holder shapes:
	//   * ibDataViewTreeStoreNode (delete-owned by parent, NOT a
	//     wxRefCounter).
	//   * Virtual-list row encoding — `wxUIntToPtr(rowIndex + 1)`
	//     is a fake pointer (e.g. 0x00000001), never dereferenceable.
	// Stored as Mode::RawId so dtor / copy-helpers skip refcount.
	// Prefer the typed constructor for new code that holds real
	// ibDataViewObject rows.
	explicit ibDataViewItem(void* pItem)
		: m_id(static_cast<ibDataViewObject*>(pItem))
		, m_mode(pItem ? Mode::RawId : Mode::Empty)
	{}

	ibDataViewItem(const ibDataViewItem& o)
		: m_id(o.m_id), m_mode(o.m_mode)
	{
		if (m_mode == Mode::Refcounted) m_id->IncRef();
	}

	ibDataViewItem(ibDataViewItem&& o) noexcept
		: m_id(o.m_id), m_mode(o.m_mode)
	{
		o.m_id = nullptr;
		o.m_mode = Mode::Empty;
	}

	~ibDataViewItem()
	{
		if (m_mode == Mode::Refcounted) m_id->DecRef();
	}

	ibDataViewItem& operator=(const ibDataViewItem& o)
	{
		if (this != &o) {
			ibDataViewObject* old      = m_id;
			const Mode        oldMode  = m_mode;
			m_id   = o.m_id;
			m_mode = o.m_mode;
			if (m_mode == Mode::Refcounted) m_id->IncRef();
			if (oldMode == Mode::Refcounted) old->DecRef();
		}
		return *this;
	}

	ibDataViewItem& operator=(ibDataViewItem&& o) noexcept
	{
		if (this != &o) {
			if (m_mode == Mode::Refcounted) m_id->DecRef();
			m_id   = o.m_id;
			m_mode = o.m_mode;
			o.m_id   = nullptr;
			o.m_mode = Mode::Empty;
		}
		return *this;
	}

	bool              IsOk()  const { return m_id != nullptr; }
	ibDataViewObject* GetID() const { return m_id; }
	Mode              GetMode() const { return m_mode; }

	// Boolean shortcuts so legacy `if (item)` / `if (!item)` keeps
	// compiling against the previous wxItemId<void*> base.
	explicit operator bool() const { return m_id != nullptr; }
	bool     operator!()      const { return m_id == nullptr; }

	// Logical equality: pointer match short-circuits, otherwise
	// dispatch to the held ibDataViewObject's IsEqualTo virtual so
	// each concrete row class decides what "same row" means for its
	// data.  Skip the virtual call if either side isn't Refcounted
	// (raw / fake-id) — those aren't valid objects to dereference,
	// pointer match is the only meaningful test.
	bool operator==(const ibDataViewItem& o) const {
		if (m_id == o.m_id) return true;
		if (!m_id || !o.m_id) return false;
		if (m_mode != Mode::Refcounted || o.m_mode != Mode::Refcounted) return false;
		return m_id->IsEqualTo(*o.m_id);
	}
	bool operator!=(const ibDataViewItem& o) const { return !(*this == o); }

	// Row-side queries — delegate to the held ibDataViewObject so the
	// caller (data-view control / business code) doesn't have to go
	// through the model.  Empty / RawId → leaf with no parent (would
	// dereference an invalid pointer).
	bool IsContainer() const {
		return m_mode == Mode::Refcounted && m_id->IsContainer();
	}
	ibDataViewItem GetParentItem() const;

private:
	ibDataViewObject* m_id   = nullptr;
	Mode              m_mode = Mode::Empty;
};

// Out-of-class definitions: inline so the body lives in the header.
// Need ibDataViewItem complete before returning it.
inline ibDataViewItem ibDataViewObject::GetParentItem() const {
	return ibDataViewItem();
}

inline ibDataViewItem ibDataViewItem::GetParentItem() const {
	return (m_mode == Mode::Refcounted) ? m_id->GetParentItem() : ibDataViewItem();
}

// Sentinel passed by the control as `parent` when it wants the model
// to ignore parent semantics altogether — every row in the dataset,
// regardless of hierarchy depth.  Used by the flat List view of a
// hierarchical catalog (FolderRef): the cursor walks the whole table
// in one ORDER BY instead of recursing per folder.  Distinct from
// `ibDataViewItem()` which means "top-level rows only".
//
// Compare via `parent == s_constIgnoreParent` (operator== short-
// circuits on pointer match — both sides hold the same marker
// instance).
BACKEND_API extern const ibDataViewItem s_constIgnoreParent;

WX_DEFINE_USER_EXPORTED_ARRAY(ibDataViewItem, ibDataViewItemArray, BACKEND_API);


// ---------------------------------------------------------
// ibDataViewModelNotifier
// ---------------------------------------------------------

class BACKEND_API ibDataViewModelNotifier
{
public:
	ibDataViewModelNotifier() { m_owner = NULL; }
	virtual ~ibDataViewModelNotifier() { m_owner = NULL; }

	virtual bool ItemInserted(const ibDataViewItem& parent, const ibDataViewItem& item) = 0;
	// "Append" semantics — distinct from ItemInserted (insert at position) so
	// the control can fire wxEVT_DATAVIEW_ITEM_START_ADDING separately from
	// _START_INSERTING. Default forwards to ItemInserted so notifiers that
	// don't care about the distinction stay correct.
	virtual bool ItemAppended(const ibDataViewItem& parent, const ibDataViewItem& item) { return ItemInserted(parent, item); }
	virtual bool ItemDeleted(const ibDataViewItem& parent, const ibDataViewItem& item) = 0;
	virtual bool ItemChanged(const ibDataViewItem& item) = 0;
	virtual bool ItemsAdded(const ibDataViewItem& parent, const ibDataViewItemArray& items);
	virtual bool ItemsDeleted(const ibDataViewItem& parent, const ibDataViewItemArray& items);
	virtual bool ItemsChanged(const ibDataViewItemArray& items);
	virtual bool ValueChanged(const ibDataViewItem& item, unsigned int col) = 0;
	virtual bool Cleared() = 0;

#pragma region __table_notifier__h__

	virtual unsigned int GetCurrentModelColumn() const = 0;
	virtual void StartEditing(const ibDataViewItem& item, unsigned int col) const = 0;

	virtual bool ShowFilter(struct ibFilterRow& filter) = 0;
	virtual bool ShowViewMode() = 0;

	virtual void Select(const ibDataViewItem& item) const = 0;
	virtual int GetCountPerPage() const = 0;

	virtual ibDataViewItem GetSelection() const = 0;
	virtual int GetSelections(ibDataViewItemArray& sel) const = 0;

	// Drill context: in Hierarchical view-mode the control keeps a
	// breadcrumb chain (m_topParentChain) of the folders the user drilled
	// into.  Returns the deepest crumb (== the currently shown folder)
	// or an empty item when not drilled (top-level view, non-hierarchical
	// mode).  Used by AddValue paths to inherit parent for new items even
	// when nothing is selected inside the folder.
	virtual ibDataViewItem GetDrillParent() const { return ibDataViewItem(); }

#pragma endregion

	// some platforms, such as GTK+, may need a two step procedure for ::Reset()
	virtual bool BeforeReset() { return true; }
	virtual bool AfterReset() { return Cleared(); }

	virtual void Resort() = 0;

	void SetOwner(ibDataViewModel* owner) { m_owner = owner; }
	ibDataViewModel* GetOwner() const { return m_owner; }

private:
	ibDataViewModel* m_owner;
};

// ----------------------------------------------------------------------------
// ibDataViewItemAttr: a structure containing the visual attributes of an item
// ----------------------------------------------------------------------------

// TODO: Merge with wxItemAttr somehow.

class BACKEND_API ibDataViewItemAttr
{
public:
	// ctors
	ibDataViewItemAttr()
	{
		m_bold = false;
		m_italic = false;
		m_strikethrough = false;
	}

	// setters
	void SetColour(const wxColour& colour) { m_colour = colour; }
	void SetBold(bool set) { m_bold = set; }
	void SetItalic(bool set) { m_italic = set; }
	void SetStrikethrough(bool set) { m_strikethrough = set; }
	void SetBackgroundColour(const wxColour& colour) { m_bgColour = colour; }

	// accessors
	bool HasColour() const { return m_colour.IsOk(); }
	const wxColour& GetColour() const { return m_colour; }

	bool HasFont() const { return m_bold || m_italic || m_strikethrough; }
	bool GetBold() const { return m_bold; }
	bool GetItalic() const { return m_italic; }
	bool GetStrikethrough() const { return m_strikethrough; }

	bool HasBackgroundColour() const { return m_bgColour.IsOk(); }
	const wxColour& GetBackgroundColour() const { return m_bgColour; }

	bool IsDefault() const { return !(HasColour() || HasFont() || HasBackgroundColour()); }

	// Return the font based on the given one with this attribute applied to it.
	wxFont GetEffectiveFont(const wxFont& font) const;

private:
	wxColour m_colour;
	bool     m_bold;
	bool     m_italic;
	bool     m_strikethrough;
	wxColour m_bgColour;
};

// ---------------------------------------------------------
// ibDataViewModel
// ---------------------------------------------------------

typedef wxVector<ibDataViewModelNotifier*> ibDataViewModelNotifiers;

class BACKEND_API ibDataViewModel : public wxRefCounter
{
public:
	ibDataViewModel();

	// get value into a wxVariant
	virtual void GetValue(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const = 0;

	// return true if the given item has a value to display in the given
	// column: this is always true except for container items which by default
	// only show their label in the first column (but see HasContainerColumns())
	virtual bool HasValue(const ibDataViewItem& item, unsigned col) const
	{
		return col == 0 || !IsContainer(item) || HasContainerColumns(item);
	}

	// usually ValueChanged() should be called after changing the value in the
	// model to update the control, ChangeValue() does it on its own while
	// SetValue() does not -- so while you will override SetValue(), you should
	// be usually calling ChangeValue()
	virtual bool SetValue(const wxVariant& variant,
		const ibDataViewItem& item,
		unsigned int col) = 0;

	bool ChangeValue(const wxVariant& variant,
		const ibDataViewItem& item,
		unsigned int col)
	{
		return SetValue(variant, item, col) && ValueChanged(item, col);
	}

	// Get text attribute, return false of default attributes should be used
	virtual bool GetAttr(const ibDataViewItem& WXUNUSED(item),
		unsigned int WXUNUSED(col),
		ibDataViewItemAttr& WXUNUSED(attr)) const
	{
		return false;
	}

	// Override this if you want to disable specific items
	virtual bool IsEnabled(const ibDataViewItem& WXUNUSED(item),
		unsigned int WXUNUSED(col)) const
	{
		return true;
	}

	// define hierarchy
	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const = 0;

	// Default container check delegates to the refcounted item's
	// own IsContainer() — that virtual lives on the row object the
	// model handed out (ibDataViewObject subclass), which is the
	// natural source of truth: the model knows about rows; the row
	// knows whether it has children. Subclasses only override when
	// they store hierarchy outside the row object (e.g. row holds a
	// flat record + parent lookup uses an external index).
	//
	// The empty (invisible) root is always treated as a container so
	// BuildTree's initial top-level fetch isn't skipped.
	virtual bool IsContainer(const ibDataViewItem& item) const
	{
		if (!item.IsOk()) return true;
		return item.IsContainer();
	}

	// Is the container just a header or an item with all columns
	virtual bool HasContainerColumns(const ibDataViewItem& WXUNUSED(item)) const
	{
		return false;
	}

	// GetChildren has been removed in favour of the unified fetch
	// contract — every "give me this parent's children" call now goes
	// through GetFirstFetch (with paged variants GetNextFetch /
	// GetPrevFetch for sources that need to stream).

	// delegated notifiers
	bool ItemInserted(const ibDataViewItem& parent, const ibDataViewItem& item);
	bool ItemAppended(const ibDataViewItem& parent, const ibDataViewItem& item);
	bool ItemsAdded(const ibDataViewItem& parent, const ibDataViewItemArray& items);
	bool ItemDeleted(const ibDataViewItem& parent, const ibDataViewItem& item);
	bool ItemsDeleted(const ibDataViewItem& parent, const ibDataViewItemArray& items);
	bool ItemChanged(const ibDataViewItem& item);
	bool ItemsChanged(const ibDataViewItemArray& items);
	bool ValueChanged(const ibDataViewItem& item, unsigned int col);
	bool Cleared();

#pragma region __table_notifier__h__

	unsigned int GetCurrentModelColumn(int view_id = 0) const;
	void StartEditing(const ibDataViewItem& item, unsigned int col, int view_id = 0) const;

	bool ShowFilter(struct ibFilterRow& filterm, int view_id = 0);
	bool ShowViewMode(int view_id = 0);

	void Select(const ibDataViewItem& item, int view_id = 0) const;
	int GetCountPerPage(int view_id = 0) const;

	ibDataViewItem GetSelection(int view_id = 0) const;
	int GetSelections(ibDataViewItemArray& sel, int view_id = 0) const;
	ibDataViewItem GetDrillParent(int view_id = 0) const;

#pragma endregion

	// some platforms, such as GTK+, may need a two step procedure for ::Reset()
	bool BeforeReset();
	bool AfterReset();

	// delegated action
	virtual void Resort();

	// Column-header click on a paged model means "rebuild the result
	// set with new ORDER BY", not "re-sort the loaded window".
	// Default implementation forwards to Resort() (legacy non-paged
	// behaviour); paged concrete models override to update their
	// sort state and trigger ResetForFilterOrSort.
	virtual void OnSortColumnChanged(unsigned int col, bool ascending);

	// Universal paged-fetch API — wxTreeCtrl iteration cookie pattern.
	// `parent` is the scope (invalid item == top-level / root).
	// `anchor`:
	//   - GetFirstFetch — optional restore point; invalid = from
	//     the top, valid = backend positions the window so the
	//     anchor row is the first returned (or the closest match).
	//   - GetNextFetch  — last loaded row (cursor going forward).
	//   - GetPrevFetch  — first loaded row (cursor going backward).
	// `count` = batch size. Returns the number of rows appended to
	// `out`; return value < requested `count` (typically 0) means
	// the side is exhausted in this direction.
	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		(void)parent; (void)anchor; (void)count; (void)out;
		return 0;
	}
	virtual unsigned int GetNextFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		(void)parent; (void)anchor; (void)count; (void)out;
		return 0;
	}
	virtual unsigned int GetPrevFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count, ibDataViewItemArray& out) const {
		(void)parent; (void)anchor; (void)count; (void)out;
		return 0;
	}

	// Is this model paged (has a `Get*Fetch` cursor source backing it)?
	// Control uses this to decide whether to drive its own ahead/behind
	// deque via Get*Fetch or treat the row store as fully loaded.
	virtual bool IsPagedModel() const                                   { return false; }

	// Capabilities the GUI may query to drive its rendering choices
	// (sort prefix, breadcrumb support, batch ops…) without having to
	// know the concrete model class.  `flags` is a bitset of boolean
	// capabilities; the rest are auxiliary values the matching
	// capability depends on (e.g. Folders → folderSortID).  Defaults
	// mean "feature absent".
	//
	// Wiring status (today):
	// * Folders + folderSortID — read by ibDataViewCtrl
	//   (GetEffectiveFetchParent + ApplyFolderSortForViewMode) to
	//   route List view of a hierarchical model through the
	//   s_constIgnoreParent sentinel and to manage the folders-on-top
	//   system sort.
	// * Tree / RamFetch / DbFetch / Filters / Sorting — written by
	//   concrete models, not yet read by GUI.  Reserved for future
	//   gating of: tree-expand arrows, fetch progress UX, filter /
	//   sort UI visibility.  Today the equivalent legacy checks
	//   (UseFilter / IsListModel / column->IsSortable) live alongside
	//   them; consolidate when migrating those callsites.
	struct Features {
		enum Flag : uint32_t {
			Tree      = 1u << 0,   // tree-shaped (parent/child rows); flat list otherwise
			Folders   = 1u << 1,   // has isFolder column → folderSortID is valid
			RamFetch  = 1u << 2,   // Get*Fetch slices an in-memory vector/map
			DbFetch   = 1u << 3,   // Get*Fetch hits the DB cursor
			Filters   = 1u << 4,   // user filter row is meaningful — show filter UI
			Sorting   = 1u << 5,   // user column-sort is meaningful — header click reorders
			// add new flags above; keep bit positions stable
		};
		uint32_t flags         = 0;
		int      folderSortID  = -1;   // valid iff (flags & Folders)

		bool Has(Flag f) const { return (flags & f) != 0; }
	};

	// Capability + state accessors lifted from ibValueModel so the
	// data-view fork (datavgen.cpp) can query them via GetTableModel()
	// directly, without cross-casting through ibDataViewModelProvider.
	// Default: empty Features / null pointers — non-paged native models
	// (tree-store, list-store, predefined editor) have no sort or
	// filter state.  ibValueModel's provider impl forwards each to
	// the owning model.
	virtual Features           GetFeatures()  const { return Features{}; }
	virtual ibSortOrder*       GetSortOrder()       { return nullptr; }
	virtual const ibSortOrder* GetSortOrder() const { return nullptr; }
	virtual ibFilterRow*       GetFilterRow()       { return nullptr; }
	virtual const ibFilterRow* GetFilterRow() const { return nullptr; }

	// Build a breadcrumb chain of ancestor items above `fromRow`,
	// ordered [direct parent, …, topmost ancestor].  Empty out when
	// `fromRow` is at the root, the model is non-hierarchical, or the
	// row's parent metadata isn't reachable.  Used by the control to
	// populate m_topParentChain on view-mode switch into Hierarchical
	// from List/Tree (selection's ancestors weren't loaded as nodes
	// during the flat fetch — the model has to materialise them so the
	// crumb labels render correctly).  Items in `out` carry refcount
	// transferred to the caller.
	virtual void BuildAncestorBreadcrumb(const ibDataViewItem& fromRow,
	                                     ibDataViewItemArray& out) const {
		(void)fromRow; (void)out;
	}

	// Dispatch a unit of fetch work asynchronously (worker pool when
	// available, inline as fallback).  Default impl runs `work`
	// synchronously; ibValueModel routes through ibSession::Submit so
	// the DB roundtrip never blocks the UI thread.
	virtual void SubmitFetchAsync(std::function<void()> work) {
		if (work) work();
	}

	void AddNotifier(ibDataViewModelNotifier* notifier);
	void RemoveNotifier(ibDataViewModelNotifier* notifier);

	int GetViewCount() const { return m_notifiers.size(); }

	// default compare function
	virtual int Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
		unsigned int column, bool ascending) const;
	virtual bool HasDefaultCompare() const { return false; }

	// internal
	virtual bool IsListModel() const { return false; }
	virtual bool IsVirtualListModel() const { return false; }

	// True iff the user may toggle ORDER BY on the given column via
	// header-click.  Default — every column is user-toggleable; the
	// fork's OnClick already gates on column-level IsSortable() so a
	// non-paged native model can rely on that.  Paged ibValueModel-
	// derived models override this to also filter out system sorts
	// (folders-on-top etc.) which the user must not flip.
	virtual bool IsSortable(unsigned int col) const { (void)col; return true; }

protected:
	// Dtor is protected because the objects of this class must not be deleted,
	// DecRef() must be used instead.
	virtual ~ibDataViewModel();

	// Helper function used by the default Compare() implementation to compare
	// values of types it is not aware about. Can be overridden in the derived
	// classes that use columns of custom types.
	virtual int DoCompareValues(const wxVariant& WXUNUSED(value1),
		const wxVariant& WXUNUSED(value2)) const
	{
		return 0;
	}

private:
	ibDataViewModelNotifiers  m_notifiers;
};

#endif