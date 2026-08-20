#ifndef __MODEL_VIEW_H__
#define __MODEL_VIEW_H__

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <wx/variant.h>
#include <wx/dnd.h>             // For wxDragResult declaration only.
#include <wx/dynarray.h>
#include <wx/itemid.h>
#include <wx/font.h>

#include "backend/backend.h"
#include "backend/system/value/valueType.h"   // ibValue, ibTypeDescription, ibGuid

// ----------------------------------------------------------------------------
// ibDataViewCtrl globals
// ----------------------------------------------------------------------------

class BACKEND_API ibDataViewModel;

// (ibComparisonType / ibFilterRow / ibSortOrder / ibSortData / ibSortModel + the in-memory RAM CompareRow are
//  ALL DELETED — the legacy view-layer filter/sort descriptors + per-model m_filterRow / m_sortOrder store.
//  Filter / sort / group is L5 now: the composer holds them and controls the order by the PORTIONS it issues
//  (add a sort → different page → different order). Any consumer that needs a column id resolves the composer's
//  name-sort inline (GetSortAt + GetColumnIDByName) at its point of use.)

// Page DIRECTION for the universal paged fetch — shared by ibValueModel::RunComposerPage (the model layer) AND
// ibReadPageRequest (the query layer, dataQueryBuilder.h). It lives HERE, the shared view/query-neutral header,
// so the query layer sees it without including tabularModel.h (which would re-introduce the include cycle).
enum class ibFetchDirection : int8_t {
	Reset    = 0,   // discard buffer, fetch initial window at anchor
	Forward  = 1,   // append after anchor
	Backward = -1   // prepend before anchor
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
// Refcount-aware: rows in OES (ibValueTreeNode, ibComposerNode, ...)
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

	// A GROUP node's caption — the dimension VALUE this group folds by, as a display string, for the
	// hierarchical render (the front paints it flowing across the columns that have no value of their own).  The
	// node SELF-describes it (the model is not consulted — the front asks the item directly, like IsContainer);
	// only a composer GROUP node overrides.  Default: not a group → false.
	virtual bool GetGroupCaption(wxString& WXUNUSED(caption)) const { return false; }

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
	// model.  Default: attached.  ibComposerNode / ibValueTreeNode
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
	// The GROUP row's caption — asked straight off the held node (like IsContainer), so the model stays out of it.
	// Empty for a detail / non-group. Guards RawId / fake pointers (never dereferenceable).
	bool GetGroupCaption(wxString& caption) const {
		return m_mode == Mode::Refcounted && m_id->GetGroupCaption(caption);
	}
	ibDataViewItem GetParentItem() const;

private:
	ibDataViewObject* m_id   = nullptr;
	Mode              m_mode = Mode::Empty;
};

// The rows a source command runs against, carried FRONT -> model (TableBox -> the model's CallAsCommand).
// m_selection = the actually-selected row (empty when nothing is selected). m_anchor = where the user stands
// in the tree (the current / top node), the fallback a CREATE command uses when there is no selection so a
// new element lands in the browsed folder. A flat model (value table / tabular) reads m_selection ONLY; the
// hierarchy model uses m_selection ?: m_anchor for the parent. Destructive commands never touch m_anchor.
struct ibDataViewCommandContext {
	ibDataViewItem m_selection;
	ibDataViewItem m_anchor;
};

// Out-of-class definitions: inline so the body lives in the header.
// Need ibDataViewItem complete before returning it.
inline ibDataViewItem ibDataViewObject::GetParentItem() const {
	return ibDataViewItem();
}

inline ibDataViewItem ibDataViewItem::GetParentItem() const {
	return (m_mode == Mode::Refcounted) ? m_id->GetParentItem() : ibDataViewItem();
}

// Sentinel passed by the control as `parent` when a FLAT List view of a hierarchical (folder-aware) source
// wants EVERY row in one ORDER BY (the cursor walks the whole table instead of recursing per folder). Distinct
// from `ibDataViewItem()` which means "top-level rows only" (the Tree view's root). Compare via
// `parent == s_constIgnoreParent` (operator== short-circuits on the shared marker pointer). (RESTORED — the
// hierarchy is an inherent queryable property now, so the FRONTEND view mode is what signals flat-vs-tree.)
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

	// The notifier is PURE PUSH now — the model tells the front WHAT CHANGED (Item* / ValueChanged / Before &
	// AfterReset / Cleared / Resort) and nothing else. Everything it used to pull or trigger (GetSelection /
	// GetSelections / GetDrillParent / GetCurrentModelColumn / StartEditing / ShowListSettings / ShowViewMode /
	// Select / GetCountPerPage) is GONE: the TableBox reads the control directly (Command_* / OnItemActivated) and
	// the control itself owns selection + page size. Commands receive the front's selection as an argument.

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
	// (A single GetDataViewInfo(item) -> {container, hasChildren, …} door would be better than one
	//  virtual per question — but this one has eight overriders across the designer, the dataview
	//  controls and the filter tree, so collapsing it is its own piece of work, not a side effect.)
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

	// some platforms, such as GTK+, may need a two step procedure for ::Reset()
	bool BeforeReset();
	bool AfterReset();

	// delegated action
	virtual void Resort();

	// (Header-click sort has NO model-side hook — an OES tablebox commits it entirely on the front
	//  (ibValueModelTableBox::OnColumnClick pokes the concrete model's composer + RefetchAll).)

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

	// Do this model's rows carry a stable primary KEY (restore selection by key) or only a positional index
	// (restore by row index)? Keyed by default; RAM-backed ibValueModels (value table / tabular section — their
	// source queryable has no primary key) override to false. Replaces the retired RamFetch / DbFetch flags.
	virtual bool HasKeyedRows() const                                   { return true; }

	// Does this model currently GROUP (produce a tree)? False by default (a flat list / native store). A grouped
	// model's row mutations must re-fetch to re-place the row into its group, so the control routes them through
	// the paged refresh + selection-restore path instead of the cheap in-place tree-insert.
	virtual bool IsGroupedModel() const                                 { return false; }

	// (No GetSortArrows / ibSortArrow — the column-header arrow is a pure FRONT concern now: the OES tablebox
	//  sets it on click and re-reads the composer's active sort per column on rebuild, matching by the column's
	//  own bound field name. The model never bridges a {col-id, asc} pair to the control.)

	// USER-FACING capabilities the settings dialog / GUI query to drive rendering without knowing the concrete
	// model class. `flags` is a bitset; defaults mean "feature absent". Wiring (today, all READ):
	// * Filters / Sorting / Grouping — the list-settings dialog shows/hides its tabs by these; Sorting also lets
	//   the column header click-sort.
	// * CustomQuery — capability marker; the query is edited in the DESIGNER, not the runtime dialog.
	// What this struct deliberately does NOT carry — all DERIVED now (Max: "a single fetch through RunComposerPage;
	// tree-ness is determined by grouping"):
	// * Tree — tree-ness is whether the composer GROUPS (GetModelComposer().GroupCount() > 0).
	// * RamFetch / DbFetch — fetch is uniform (IsPagedModel() true for every ibValueModel); the RAM-vs-keyed
	//   distinction (restore by index vs by key) is HasKeyedRows(), derived from the source's primary key.
	// * Folders / folderSortID — a List view already ignores the parent (it never groups, so RunComposerPage
	//   does not scope by it); folder-first ORDER is a composer-grouping concern. Both retired.
	struct Features {
		enum Flag : uint32_t {
			Filters   = 1u << 4,   // user filter is meaningful — show the Filter tab
			Sorting   = 1u << 5,   // user column-sort is meaningful — header click reorders + show the Sort tab
			Grouping  = 1u << 6,   // user grouping is meaningful — show the Group tab
			CustomQuery = 1u << 7, // runs a verbatim author query ("custom query"); edited in the DESIGNER
			// (bits 0-3 intentionally free — retired Tree / Folders / RamFetch / DbFetch; keep positions stable)
		};
		uint32_t flags = 0;

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

	// (GetSortModels DELETED — the header arrows read the model's composer (GetModelComposer().GetSortAt +
	//  GetColumnIDByName) directly now; sort is L5, by name. ibSortModel is gone.)

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

	// HOW THIS MODEL'S PORTION READ RUNS — asked of the MODEL, because only the
	// model knows what its read costs and what it is allowed to touch. The control
	// has ONE code point for every kind of table it can hold: it hands over a unit
	// of work and is told nothing about where it ran.
	//
	// This base answers WITH A THREAD OF ITS OWN — one per model, owned here (see
	// below). Not because a plain model's read is slow, but because
	// "the thread that asked" is not always a UI thread with time to spare: the same
	// forms are rendered on the web server, where it is a session's worker and a
	// blocking read holds up that session's whole answer. One shape for both hosts,
	// and the busy indicator becomes honest everywhere — a slow read shows a
	// spinner instead of a frozen window.
	//
	// `ibValueModel` overrides it: that is a RUNTIME table, so its portion goes to a
	// background job instead. The decision is per model, never per control, which is
	// why it is a virtual here and not a branch at the call site — and it is the
	// only place in the engine that knows a background job exists at all.
	// (Out-of-line in tabularModelView.cpp — it is the only thing here that needs <thread>.)
	virtual void SubmitFetchAsync(std::function<void()> work);

	// ONE PORTION AT A TIME — the only lock in the whole path, and it lives in the
	// door because that is where the operation is.
	//
	// A sort click is a RESET: the composer recomputes, a portion is re-fetched, and
	// the view rebuilds its tree from it. If a portion is still on its way back when
	// the next reset starts, the two are inside the same buffer. So every unit of
	// work handed to the door runs wrapped in this — whoever runs it, thread or
	// background job. Nothing else takes it: not the mutators, not the composer, not
	// the paint.
	std::function<void()> GuardFetch(std::function<void()> work) {
		return [this, work]() {
			std::lock_guard<std::mutex> lock(m_fetchMtx);
			if (work) work();
		};
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
	// DecRef() must be used instead. It JOINS the fetch thread — a read must never
	// outlive the model it is reading.
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

	// The thread from SubmitFetchAsync. Held as a pointer so this header does not
	// pull <thread> into every translation unit that sees a model; created on the
	// first read that leaves the thread and never afterwards recreated while it
	// runs (it is joined first).
	std::unique_ptr<class ibModelFetchThread> m_fetchThread;

	// See GuardFetch — one portion at a time.
	std::mutex m_fetchMtx;
};

#endif