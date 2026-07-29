#ifndef __META_DIFF_H__
#define __META_DIFF_H__

#include "backend/metaCollection/metaObject.h"

#include <vector>

// Configuration comparison primitive.
//
// Pairs two ibValueMetaObject trees side by side and classifies each node
// pair against the existing ibValueMetaObject::CompareObject() identity
// check. Output is a flat std::vector preserving tree topology via the
// m_parentIndex / m_depth fields — the UI layer (ibDataViewCtrl model)
// reads this directly. No UI code lives here; the walker stays in the
// backend so it is reusable from CLI tooling and tests.
//
// Pairing strategy: at each level, a left child is paired with a right
// child if they share the same ibGuid. Children that don't match are
// classified as OnlyInLeft / OnlyInRight at the level where they appear.
// We deliberately do NOT search the whole right subtree for a moved
// object — a topology change (object moved from parent A to parent B)
// shows up as one OnlyInLeft + one OnlyInRight, which is what the merge
// UI needs to represent anyway.

// Sentinel CLSID for the synthetic "Common" umbrella node the walker
// inserts under the root config. Not a real metadata class — just a
// marker so the dataview model knows to render the umbrella with the
// generic folder art (wxART_COMMON_FOLDER) instead of looking up a
// per-class glyph. Mirrors the m_treeCOMMON umbrella in
// treeConfiguration_impl.cpp (line 1473).
inline constexpr ibClassID g_diffCommonUmbrellaClsid = make_clsid("UI_COMN", ibClassKind_None);   // header-defined: constexpr + ODR-safe

// Sentinel CLSID for the synthetic "Properties" group node the walker
// inserts under every paired metadata object. Contains one child row
// per property (m_propertyName / m_leftValue / m_rightValue on the
// child record); the comparison itself is property-by-property via
// CompareObject, so this group makes the property-level delta visible.
inline constexpr ibClassID g_diffPropertiesGroupClsid = make_clsid("UI_PROP", ibClassKind_None);   // header-defined: constexpr + ODR-safe

enum class ibMetaDiffStatus {
	// CompareObject returned true and recursive children match: identical.
	// The walker still emits a record so the UI can render the unchanged
	// node in its tree position.
	Same,

	// All children pair by GUID and every child is Same / Reordered, but
	// the children appear in a different order between the two sides.
	// Significant for sequence-sensitive collections (enum values,
	// predefined items, attribute order on a Catalog). Merge operation
	// for a Reordered parent is "rewrite child order on the target side".
	Reordered,

	// Paired by GUID, but CompareObject returned false at this node OR a
	// descendant differs. The NODE's status is object-level, but the synthetic
	// Properties group beneath it (g_diffPropertiesGroupClsid, above) carries a
	// per-property status row. Changed dominates Reordered and Same when
	// rolled up.
	Changed,

	// Present in left config, no GUID match in right.
	OnlyInLeft,

	// Present in right config, no GUID match in left.
	OnlyInRight,
};

struct BACKEND_API ibMetaDiffRecord {
	// One of the two pointers may be nullptr (OnlyInLeft / OnlyInRight).
	// Both are non-null when status is Same or Changed. BOTH are nullptr
	// when this is a synthetic group row — m_groupClsid is non-zero in
	// that case and supplies the icon CLSID + group label.
	ibValueMetaObject* m_left = nullptr;
	ibValueMetaObject* m_right = nullptr;

	ibMetaDiffStatus m_status = ibMetaDiffStatus::Same;

	// Index into the owning vector of this record's parent; -1 for the
	// root record. Lets the dataview model walk parent / child links
	// without rebuilding pointers across the flat array.
	int m_parentIndex = -1;

	// Depth from the root (root == 0). UI can use for indent if it
	// chooses not to traverse parent links.
	int m_depth = 0;

	// Synthetic group row. When non-zero, this row represents a CLSID
	// group (Catalogs, Documents, Attributes, ...) — m_left/m_right are
	// null, m_status is computed from descendants. The walker inserts
	// these between every pair node and its actual object children, so
	// the diff tree mirrors the standard metadata-tree shape (see
	// treeConfiguration_impl.cpp FillData).
	ibClassID m_groupClsid = 0;

	// Property-row mode. When non-empty, this record represents a
	// single ibProperty of the parent paired object — m_left/m_right
	// are null, m_groupClsid is 0, m_status is Same / Changed by
	// string equality of the two values. The model renders the name
	// in kColName and the stringified values in kColLeft / kColRight.
	wxString m_propertyName;
	wxString m_leftValue;
	wxString m_rightValue;

	bool IsGroup() const { return m_groupClsid != 0; }
	bool IsProperty() const { return !m_propertyName.IsEmpty(); }

	// Convenience: the non-null pointer (left if present, else right).
	// Returns nullptr for synthetic group rows.
	ibValueMetaObject* GetAnyObject() const {
		return m_left != nullptr ? m_left : m_right;
	}

	// Best CLSID for icon rendering — group's own class for synthetic
	// rows, the wrapped object's class otherwise.
	ibClassID GetIconClsid() const {
		if (m_groupClsid != 0) return m_groupClsid;
		const ibValueMetaObject* obj = GetAnyObject();
		return obj != nullptr ? obj->GetClassType() : 0;
	}

	// True when status is Same/Changed; both sides exist and were paired.
	bool IsPaired() const {
		return m_left != nullptr && m_right != nullptr;
	}

	// True when this row represents a difference the merge UI should let
	// the user act on. Same nodes, group rows, and property-detail rows
	// are display-only (properties don't merge individually — the
	// containing object is the merge unit).
	bool IsMergeCandidate() const {
		return !IsGroup() && !IsProperty()
			&& m_status != ibMetaDiffStatus::Same;
	}
};

class BACKEND_API ibMetaDiffWalker {
public:

	// Walk two metadata subtrees in parallel and produce the flat diff
	// vector. Either root may be nullptr; the result will contain only
	// OnlyInLeft / OnlyInRight records in that case. Records appear in
	// pre-order — parent before its children — so the dataview model
	// can render them top-to-bottom without sorting. The walker inserts
	// synthetic group rows under every paired object, partitioning its
	// children by CLSID, so the result mirrors the standard metadata-
	// tree shape (Catalogs / Documents / Attributes / ...).
	static std::vector<ibMetaDiffRecord> Walk(
		ibValueMetaObject* leftRoot,
		ibValueMetaObject* rightRoot
	);

	// Localized group label for a metadata CLSID (Catalogs, Documents,
	// Attributes, ...). Mirrors the constants in
	// treeConfiguration_impl.cpp. Returns a hex-formatted fallback for
	// unknown CLSIDs so the UI never shows an empty string.
	static wxString GroupLabelFor(ibClassID clsid);

	// Canonical sort rank for a group CLSID. Lower rank = appears first
	// in the standard metadata tree (see treeConfiguration_impl.cpp).
	// Used by the walker to emit groups in the same order the designer
	// shows them. Unknown CLSIDs sort after the known ones.
	static int GroupOrderRank(ibClassID clsid);

private:

	// Recursive worker for object pairs. Emits the pair record, then
	// emits synthetic group rows per CLSID present in either side's
	// children, then recurses into the children of each group.
	static void WalkPair(
		ibValueMetaObject* left,
		ibValueMetaObject* right,
		int depth,
		int parentIndex,
		std::vector<ibMetaDiffRecord>& out
	);

	// Recursive worker for one CLSID group inside a paired object. Emits
	// the group record and then a pair record per matched-by-GUID child,
	// plus OnlyInLeft / OnlyInRight for unmatched children.
	static void WalkGroup(
		ibClassID clsid,
		ibValueMetaObject* leftParent,
		ibValueMetaObject* rightParent,
		int depth,
		int parentIndex,
		std::vector<ibMetaDiffRecord>& out
	);

	// True iff the subtree rooted at the pair has any non-Same descendant.
	// CompareObject is per-node; we propagate "Changed" upward so the UI
	// can collapse Same subtrees and the user can find diffs by scanning
	// only the top of the tree.
	static bool SubtreeHasDifference(
		const std::vector<ibMetaDiffRecord>& out,
		size_t recordIndex
	);

	// True iff the same set of GUIDs appears under both parents but in a
	// different order. Returns false when child counts differ or any
	// child has no GUID match on the other side (those are handled as
	// OnlyInLeft / OnlyInRight, not Reordered).
	static bool ChildOrderDiffers(
		const ibValueMetaObject* left,
		const ibValueMetaObject* right
	);
};

#endif // __META_DIFF_H__
