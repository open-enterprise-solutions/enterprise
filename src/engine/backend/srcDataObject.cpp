////////////////////////////////////////////////////////////////////////////
//	Description : ibSourceDataObject self-description + path walk (extracted from commonObject.cpp)
////////////////////////////////////////////////////////////////////////////

#include "srcDataObject.h"

bool ibSourceDataObject::IsTableSource() const
{
	// The source's OWN explorer answers it — a table source builds its root as a tabular section; a scalar
	// object / reference does not. Self-described, no clsid -> ctor registry round-trip.
	const ibSourceExplorer* explorer = GetSourceExplorer();
	return explorer != nullptr && explorer->IsTableSection();
}

// (ResolvePath — the shared deep-hop — is now an inline static in srcDataObject.h, symmetric with
//  ibBackendCommandSender::ResolveCommandPath: both walks are tiny header-only loops over a self-describing
//  value. The virtual hop inside stays a virtual call either way, so this is symmetry, not a speed change.)

// Walk a whole hop path off THIS source to the leaf value: first hop off this, deeper hops via the shared
// ResolvePath. Steps through the hop array; each step self-describes the next.
bool ibSourceDataObject::GetValueByPath(const std::vector<ibSourceHop>& path, ibValue& pvarMetaVal) const
{
	if (path.empty())
		return false;
	
	ibValue current;
	if (!GetValueBySourceHop(path.front(), current))
		return false;

	return ResolvePath(current, path, 1, pvarMetaVal);
}


// THE structure-resolve hop — the design-time twin of ResolvePath. Walks the path through the source
// EXPLORER TREE: a node with CHILDREN (a tabular SECTION) descends into them in the SAME explorer (the
// section's columns live under the section node — a section is an ibTabularObject, NOT an ibSourceDataObject,
// so you can't hop into its VALUE); a leaf REFERENCE node hops into the referenced source's OWN explorer
// (its value is an empty-but-typed reference at design time). Collects the leaf column + dotted name.
// Gateway boolean: true iff every hop resolved. The whole `this` explorer is built ONCE and never refilled
// here, so the nested node refs stay valid across the walk; leaf = GetColumn() points into the owning
// metaobject (stable regardless).
bool ibSourceDataObject::WalkColumns(const std::vector<ibSourceHop>& path, size_t from, const ibBackendSourceColumn*& leaf,
	wxString* outText, bool* outLeafIsTable, bool* outContainerIsTable) const
{
	leaf = nullptr;
	const ibSourceExplorer* explorer = GetSourceExplorer();
	// A reference hop's design-time value is an empty typed reference-as-source; park them so the
	// explorers they vend (which the next node aliases) outlive each step.
	std::vector<ibValue> refHolders;
	for (size_t i = from; i < path.size(); ++i) {
		if (explorer == nullptr)
			return false;   // ran out of columns mid-path (a primitive can't be dotted into)
		const ibSourceExplorer* node = explorer->FindById(path[i].m_id);
		if (node == nullptr)
			return false;   // not a column at this level -> broken binding
		if (outText != nullptr) { *outText += wxT("."); *outText += node->GetSourceName(); }
		leaf = node->GetColumn();
		if (i + 1 >= path.size()) {
			// Leaf reached — the control-class facts: is the leaf itself a table (a tabular section drops as a
			// tablebox), and is its container a table (then the leaf is a per-row COLUMN, bindable only INTO
			// that table). The source root itself flags table (the value-table / list invariant), so one
			// IsTableSection() answers the container uniformly at root and mid-path.
			if (outLeafIsTable != nullptr) *outLeafIsTable = node->IsTableSection();
			if (outContainerIsTable != nullptr) *outContainerIsTable = explorer->IsTableSection();
			break;
		}
		if (node->GetHelperCount() > 0) {
			// CONTAINER node (a tabular section) — its columns are CHILDREN in the SAME explorer.
			explorer = node;
		}
		else {
			// REFERENCE field — hop into the target through THE gate (GetValueBySourceHop) off the node's owner
			// source. srcDataObject stays METADATA-FREE: the record / reference override resolves the id and, for
			// a composite / empty field, hands back the pinned target's empty typed twin (CoerceHopType lives on
			// the reference side, not here). A field nested in a tabular section works through the same gate — the
			// record's id-read just misses (returns false) and the twin takes over. Park it so its explorer lives.
			ibSourceDataObject* owner = node->GetOwner();
			ibValue next;
			if (owner == nullptr || !owner->GetValueBySourceHop(path[i], next))
				return false;
			ibSourceDataObject* nextSrc = nullptr;
			next.ConvertToValue<ibSourceDataObject>(nextSrc);
			if (nextSrc == nullptr)
				return false;
			refHolders.push_back(next);
			explorer = nextSrc->GetSourceExplorer();
		}
	}
	return true;
}


