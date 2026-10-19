#ifndef __META_DIFF_H__
#define __META_DIFF_H__

#include "frmclient/backend/metaCollection/metaObject.h"

#include <vector>

#include <wx/translation.h>

// THE ENGINE'S COMPARISON (backend/metaCollection/metaDiff.h) AS THE THIN DESIGNER'S COMPARE TAB NAMES IT
// (designer-thin/mainFrame/configCompare): the record and its status whole. Records arrive from schema ConfigCompare
// Build (the server's Walk); m_left/m_right stay null — Name, Picture, LeftId, RightId and the side strings name each row.
//
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

struct ibMetaDiffRecord {
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

	// Filled from schema ConfigCompare Records — the server's Walk has no live pointers on the client.
	wxString m_name;
	ibClassID m_iconClsid = 0;
	ibMetaID m_leftId = 0;
	ibMetaID m_rightId = 0;

	bool IsGroup() const { return m_groupClsid != 0; }
	bool IsProperty() const { return !m_propertyName.IsEmpty(); }

	// Convenience: the non-null pointer (left if present, else right).
	// Returns nullptr for synthetic group rows — and always on the thin client (records are wire-filled).
	ibValueMetaObject* GetAnyObject() const {
		return m_left != nullptr ? m_left : m_right;
	}

	ibClassID GetIconClsid() const {
		if (m_groupClsid != 0) return m_groupClsid;
		return m_iconClsid;
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

class ibMetaDiffWalker {
public:

	// Walk two metadata subtrees in parallel and produce the flat diff
	// vector. Either root may be nullptr; the result will contain only
	// OnlyInLeft / OnlyInRight records in that case. Records appear in
	// pre-order — parent before its children — so the dataview model
	// can render them top-to-bottom without sorting. The walker inserts
	// synthetic group rows under every paired object, partitioning its
	// children by CLSID, so the result mirrors the standard metadata-
	// tree shape (Catalogs / Documents / Attributes / ...).
	// The walk is the server's (schema ConfigCompare Build). The thin designer never walks local trees — the client's
	// metaobjects hold no children. Kept so the header matches the engine's; callers use wire Records instead.
	static std::vector<ibMetaDiffRecord> Walk(
		ibValueMetaObject* WXUNUSED(leftRoot),
		ibValueMetaObject* WXUNUSED(rightRoot)
	) {
		return {};
	}

	// Localized group label for a metadata CLSID (Catalogs, Documents,
	// Attributes, ...). Mirrors the constants in
	// treeConfiguration_impl.cpp. Returns a hex-formatted fallback for
	// unknown CLSIDs so the UI never shows an empty string.
	// The two rows the comparison adds itself; a metatype's group caption (the engine's ibMetaGroupCaption) is the
	// server's — its row would carry it, the raw symbol until then.
	static wxString GroupLabelFor(ibClassID clsid) {
		if (clsid == g_diffCommonUmbrellaClsid)             return _("Common");
		if (clsid == g_diffPropertiesGroupClsid)            return _("Properties");

		// Unknown CLSID — show its raw symbol so the UI never blanks.
		return wxString::Format(wxT("[%llu]"),
			static_cast<unsigned long long>(clsid));
	}

	// Canonical sort rank for a group CLSID. Lower rank = appears first
	// in the standard metadata tree (see treeConfiguration_impl.cpp).
	// Used by the walker to emit groups in the same order the designer
	// shows them. Unknown CLSIDs sort after the known ones.
	// The two rows the comparison adds itself ahead; a metatype's declared place (the engine's ibMetaGroupOrder) is the
	// server's walk's.
	static int GroupOrderRank(ibClassID clsid) {
		if (clsid == g_diffCommonUmbrellaClsid)             return -2;
		if (clsid == g_diffPropertiesGroupClsid)            return -1;
		return 0;
	}

	// (The engine's private workers — WalkPair, WalkGroup, SubtreeHasDifference, ChildOrderDiffers — walk the trees;
	// they are the server's with the walk.)
};

#endif // __META_DIFF_H__
