////////////////////////////////////////////////////////////////////////////
//	Description : ibSourceDataObject self-description + path walk (extracted from commonObject.cpp)
////////////////////////////////////////////////////////////////////////////

#include "srcDataObject.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/metaCollection/partial/commonObject.h"   // ibValueMetaObjectGenericData::GetMetaData / GetName

bool ibSourceDataObject::IsTableSource() const
{
	// Resolve the source's class ctor by its CLSID — metaobject registry first (catalog /
	// document lists, register record sets), else the global value registry (the dynamic
	// list and ibValueModelTable). ibMetaData::GetAvailableCtor already falls back to the
	// global registry, so one lookup covers both; the table answer is the ctor's IsTableValue.
	const ibClassID clsid = GetSourceClassType();
	const ibMetaData* metaData = GetSourceMetaData(); 
	const ibCtorAbstractType* ctor = metaData != nullptr
		? metaData->GetAvailableCtor(clsid)
		: ibValue::GetAvailableCtor(clsid);
	return ctor != nullptr && ctor->IsTableValue();
}

// THE shared deep-hop — the single code path the whole engine fetches dotted data through (this is
// what GetValueByPath and the tablebox column renderer both call). The value at each step IS a source
// object (a reference now inherits ibSourceDataObject), so it self-describes the next id. Static: the
// starting value need not be THIS source (the renderer feeds a row's reference cell). Metadata-free —
// no metaID -> name -> FindProp round-trip. Gateway boolean: false = a primitive mid-path or a missing
// id; `out` holds the final value only on true.
bool ibSourceDataObject::ContinueHops(const ibValue& start, const std::vector<ibSourceId>& path, size_t from, ibValue& out)
{
	ibValue current = start;
	for (size_t i = from; i < path.size(); ++i) {
		ibSourceDataObject* source = nullptr;
		current.ConvertToValue<ibSourceDataObject>(source);
		if (source == nullptr)
			return false;   // a non-source value (a primitive) ends the walk: you cannot dot into it
		ibValue next;
		if (!source->GetValueByMetaID(path[i], next))
			return false;
		current = next;
	}
	out = current;
	return true;
}

bool ibSourceDataObject::GetValueByPath(const std::vector<ibSourceId>& path, ibValue& pvarMetaVal) const
{
	if (path.empty())
		return false;

	// First hop: the source resolves its OWN column. The deeper hops are the shared ContinueHops walk.
	ibValue current;
	if (!GetValueByMetaID(path.front(), current))
		return false;

	return ContinueHops(current, path, 1, pvarMetaVal);
}

// THE structure-resolve hop — the design-time twin of ContinueHops. Walks the path through the source
// EXPLORER TREE: a node with CHILDREN (a tabular SECTION) descends into them in the SAME explorer (the
// section's columns live under the section node — a section is an ibTabularObject, NOT an ibSourceDataObject,
// so you can't hop into its VALUE); a leaf REFERENCE node hops into the referenced source's OWN explorer
// (its value is an empty-but-typed reference at design time). Collects the leaf column + dotted name.
// Gateway boolean: true iff every hop resolved. The whole `this` explorer is built ONCE and never refilled
// here, so the nested node refs stay valid across the walk; leaf = GetColumn() points into the owning
// metaobject (stable regardless).
bool ibSourceDataObject::WalkColumns(const std::vector<ibSourceId>& path, size_t from, const ibBackendSourceColumn*& leaf, wxString* outText) const
{
	leaf = nullptr;
	const ibSourceExplorer* explorer = GetSourceExplorer();
	// A reference hop's design-time value is an empty typed reference-as-source; park them so the
	// explorers they vend (which the next node aliases) outlive each step.
	std::vector<ibValue> refHolders;
	for (size_t i = from; i < path.size(); ++i) {
		if (explorer == nullptr)
			return false;   // ran out of columns mid-path (a primitive can't be dotted into)
		const ibSourceExplorer* node = explorer->FindById(path[i]);
		if (node == nullptr)
			return false;   // not a column at this level -> broken binding
		if (outText != nullptr) { *outText += wxT("."); *outText += node->GetSourceName(); }
		leaf = node->GetColumn();
		if (i + 1 >= path.size())
			break;
		if (node->GetHelperCount() > 0) {
			// CONTAINER node (a tabular section) — its columns are CHILDREN in the SAME explorer.
			explorer = node;
		}
		else {
			// REFERENCE field — descend into the referenced TYPE's columns. The node materialises an empty
			// reference-as-source per TARGET type (GetReferenceSources, type-derived via the owner's metaData
			// — the recursion fuel the runtime value-hop also uses). For a COMPOSITE reference (several
			// targets) pick the type whose columns carry the NEXT hop (path[i + 1] is in range — we broke
			// above at the leaf). Park the chosen one so its explorer outlives the step.
			std::vector<ibValue> candidates;
			node->GetReferenceSources(candidates);
			explorer = nullptr;
			for (ibValue& candidate : candidates) {
				ibSourceDataObject* next = nullptr;
				candidate.ConvertToValue<ibSourceDataObject>(next);
				const ibSourceExplorer* candExplorer = (next != nullptr) ? next->GetSourceExplorer() : nullptr;
				if (candExplorer != nullptr && candExplorer->FindById(path[i + 1]) != nullptr) {
					refHolders.push_back(candidate);
					explorer = candExplorer;
					break;
				}
			}
		}
	}
	return true;
}

// GetReferenceTargets — THE clsid -> target-metaobject-id resolution, shared by the node's
// reference-as-source below AND the picker's lazy expansion (advpropSource). A composite reference
// (several clsids) yields several targets.
std::vector<ibMetaID> ibSourceDataObject::GetReferenceTargets(const std::vector<ibClassID>& clsids, const ibMetaData* metaData)
{
	std::vector<ibMetaID> targets;
	if (metaData == nullptr)
		return targets;
	for (const ibClassID& clsid : clsids) {
		const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
		if (typeCtor == nullptr || typeCtor->GetMetaTypeCtor() != ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
			continue;
		const ibValueMetaObject* metaObj = typeCtor->GetMetaObject();
		if (metaObj != nullptr)
			targets.push_back(metaObj->GetMetaID());
	}
	return targets;
}

// ibSourceExplorer::GetReferenceSources — out-of-line: the OWNER ibSourceDataObject is incomplete inside
// the nested class body. The node's TYPED-EMPTY reference-as-source(s): the design-time twin of a live
// fetch. Built from the node's own TYPE through the owner source's metaData (the source is metadata-bound
// — the node only borrows it, it stores no metaobject), never a live row read, so it works for a
// collection source (list / section) with no current row. Several for a COMPOSITE reference; none for a
// non-reference / unresolved node.
void ibSourceDataObject::ibSourceExplorer::GetReferenceSources(std::vector<ibValue>& out) const
{
	const ibMetaData* metaData = m_sourceInfo.m_owner != nullptr ? m_sourceInfo.m_owner->GetSourceMetaData() : nullptr;
	if (metaData == nullptr)
		return;
	for (const ibMetaID& target : ibSourceDataObject::GetReferenceTargets(m_sourceInfo.m_typeDesc.GetClsidList(), metaData))
		out.push_back(ibValueReferenceDataObject::Create(metaData, target));
}
