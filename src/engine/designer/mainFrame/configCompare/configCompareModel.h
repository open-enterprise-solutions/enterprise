#ifndef __CONFIG_COMPARE_MODEL_H__
#define __CONFIG_COMPARE_MODEL_H__

#include "backend/metaCollection/metaDiff.h"
#include "frontend/win/ctrls/dataview/dataview.h"

#include <vector>

class ibDataViewMetaDiffModel;

// ibDataViewDiffRowObject
//
// One per ibMetaDiffRecord — wraps an int index back into the model's
// flat record vector. We need a refcounted ibDataViewObject (not the
// raw void* encoding) because ibDataViewItem::IsContainer() returns
// false unconditionally for RawId items (modelView.h:400), and the
// dataview's expander-visibility check goes through that method. So
// every row that may be expanded HAS to be Refcounted-mode.
//
// IsEqualTo compares by record index so logically-equal items still
// match after the dataview re-fetches and gets a fresh object.
class ibDataViewDiffRowObject : public ibDataViewObject {
public:
	ibDataViewDiffRowObject(ibDataViewMetaDiffModel* model, int recordIndex)
		: m_model(model), m_recordIndex(recordIndex) {}

	int GetRecordIndex() const { return m_recordIndex; }

	virtual bool IsContainer() const override;
	virtual ibDataViewItem GetParentItem() const override;
	virtual bool IsEqualTo(const ibDataViewObject& other) const override;

private:
	ibDataViewMetaDiffModel* m_model;
	int m_recordIndex;
};

// ibDataViewMetaDiffModel
//
// Tree model for the configuration-compare ibDataViewCtrl. Wraps the
// flat std::vector<ibMetaDiffRecord> produced by ibMetaDiffWalker plus
// a parallel "selected for merge" bitmap that the toggle column reads
// and writes.
//
// Item encoding: every record gets a cached ibDataViewDiffRowObject
// (Refcounted). The invisible root is the empty ibDataViewItem().
// We deliberately use Refcounted mode rather than the void*/RawId
// trick because the dataview's expander visibility goes through
// ibDataViewItem::IsContainer(), which routes to the held object's
// virtual IsContainer — and that path is only taken in Refcounted
// mode.

class ibDataViewMetaDiffModel : public ibDataViewModel {
public:

	// What the dataview shows on top of the full diff result.
	//   All          — every record (default)
	//   Differences  — only records whose status != Same; their
	//                  ancestor containers are auto-promoted so the
	//                  tree shape stays navigable.
	//   SameOnly     — only records with status == Same; ancestor
	//                  containers also auto-promoted.
	enum class FilterMode {
		All,
		Differences,
		SameOnly,
	};

	enum Column {
		kColSelect = 0,   // bool checkbox, only meaningful when row is a merge candidate
		kColName,         // string — object name (left preferred, right fallback)
		kColStatus,       // string — localized status ("Same" / "Changed" / "Reordered" / ...)
		kColLeft,         // string — left summary (class + name, or "—" when OnlyInRight)
		kColRight,        // string — right summary (or "—" when OnlyInLeft)
		kColCount,
	};

	// Constructor moves the walker output into the model and pre-builds
	// the per-record row objects + parent → children index.
	explicit ibDataViewMetaDiffModel(std::vector<ibMetaDiffRecord> records);
	virtual ~ibDataViewMetaDiffModel() = default;

	// --- ibDataViewModel overrides ---

	virtual void GetValue(wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) const override;

	virtual bool SetValue(const wxVariant& variant,
		const ibDataViewItem& item, unsigned int col) override;

	virtual bool HasValue(const ibDataViewItem& item,
		unsigned col) const override;

	// IsContainer + GetChildren intentionally not overridden — the
	// ibDataViewModel base defaults route IsContainer through the
	// row object's virtual (ibDataViewDiffRowObject::IsContainer →
	// RecordHasChildren) and GetChildren through GetFirstFetch.
	// Single source of truth for hierarchy lives in the row object.

	virtual ibDataViewItem GetParent(const ibDataViewItem& item) const override;

	virtual unsigned int GetFirstFetch(const ibDataViewItem& parent,
		const ibDataViewItem& anchor, int count,
		ibDataViewItemArray& out) const override;

	virtual bool GetAttr(const ibDataViewItem& item, unsigned int col,
		ibDataViewItemAttr& attr) const override;

	virtual bool IsEnabled(const ibDataViewItem& item,
		unsigned int col) const override;

	virtual bool HasDefaultCompare() const override { return false; }

	virtual bool IsListModel() const override { return false; }
	virtual bool IsVirtualListModel() const override { return false; }

	// --- helpers exposed to ibDataViewDiffRowObject ---

	// True iff the record at this index has any child records (queried
	// from the parent → children index built in the constructor).
	bool RecordHasChildren(int recordIndex) const;

	// ibDataViewItem for the record's parent in the diff tree. Returns
	// the empty item when the record is at top level.
	ibDataViewItem GetParentItemForRecord(int recordIndex) const;

	// --- merge-selection accessors used by the dialog ---

	bool IsSelected(int recordIndex) const;
	void SetSelected(int recordIndex, bool selected);

	// Cascade selection to every merge-candidate descendant of the
	// given record. Used when the user toggles a group / container
	// checkbox — propagates the new state down the subtree.
	void CascadeSelection(int recordIndex, bool selected);

	// Aggregate selection state for a container: true if every
	// merge-candidate descendant is selected; false otherwise
	// (including the "mixed" case where some but not all are
	// selected). Used by GetValue to render group checkboxes.
	bool IsAllDescendantsSelected(int recordIndex) const;

	// True iff the subtree has at least one merge candidate. Used by
	// HasValue to hide the checkbox on container rows that have
	// nothing actionable inside (e.g. an all-Same group).
	bool HasMergeCandidateDescendant(int recordIndex) const;

	// Bulk operations exposed for "Select all differences" / "Clear" buttons.
	void SelectAllDifferences();
	void ClearAllSelection();

	// Read-only access to the underlying records — used by the merge
	// apply path to walk paired objects and call CopyObject.
	const std::vector<ibMetaDiffRecord>& GetRecords() const { return m_records; }

	// Item wrapping the root record (record 0) — the config pair the
	// walker emits first. Returns the empty item when the walker
	// produced no records (one-sided diff with empty input).
	ibDataViewItem GetRootItem() const;

	// True when at least one record is selected for merge — drives the
	// "Apply" button enabled state.
	bool HasSelection() const;

	// --- filter ---

	void SetFilterMode(FilterMode mode);
	FilterMode GetFilterMode() const { return m_filterMode; }

	// True if the record passes the current filter directly OR is a
	// promoted container of a visible descendant. Drives both
	// GetFirstFetch (skip hidden children) and RecordHasChildren
	// (hide expander when all children are filtered out).
	bool IsRecordVisible(int recordIndex) const;

private:

	// Look up the row object for a record (cached, never reallocated).
	// Returns nullptr for out-of-range indexes.
	ibDataViewDiffRowObject* RowObjectAt(int recordIndex) const;

	// Recover the record index from an ibDataViewItem; returns -1 for
	// the empty item (invisible root) and for items that don't carry
	// one of our row objects.
	int IndexFromItem(const ibDataViewItem& item) const;

	// Build the ibDataViewItem that wraps the row object for `index`.
	// Returns the empty item when index < 0.
	ibDataViewItem ItemFromIndex(int index) const;

	// Localized labels for the status column.
	static wxString StatusLabel(ibMetaDiffStatus status);

	// Short summary of one side, or "—" if null.
	static wxString SideSummary(const ibValueMetaObject* obj);

	std::vector<ibMetaDiffRecord> m_records;

	// One row object per record, owned by the model. wxObjectDataPtr
	// keeps refcount >= 1 for the model's lifetime; ibDataViewItem
	// users IncRef/DecRef on top so the rows survive transient model
	// states like a re-fetch.
	std::vector<wxObjectDataPtr<ibDataViewDiffRowObject>> m_rowObjects;

	// Per-record "selected for merge" flag. Stored separately from the
	// records so the walker output stays a pure data structure.
	std::vector<bool> m_selected;

	// Parent index → list of child record indices. Built once in the
	// constructor; children of the invisible root live in
	// m_childrenByParent[0]; record i's children live in
	// m_childrenByParent[i + 1].
	std::vector<std::vector<int>> m_childrenByParent;

	// Recompute m_visibleByFilter after a filter change. Bottom-up:
	// first mark every record that matches the filter directly, then
	// walk reverse-pre-order so ancestor containers light up when any
	// of their descendants pass.
	void RecomputeVisibility();

	FilterMode m_filterMode = FilterMode::All;

	// Per-record visibility under the current filter. Size matches
	// m_records.size().
	std::vector<bool> m_visibleByFilter;
};

#endif // __CONFIG_COMPARE_MODEL_H__
