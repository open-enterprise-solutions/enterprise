////////////////////////////////////////////////////////////////////////////
//	Description : ibSourceDataObject self-description + path walk (extracted from commonObject.cpp)
////////////////////////////////////////////////////////////////////////////

#include "srcDataObject.h"

// The walk needs ONE thing from a table — that it can answer for its own column without a row. That is
// ibTabularDataObject, a light interface; ibValueModel (and the whole model layer with it) has no business
// being named here, and pulling it in was leaking the model into a walk that stays metadata-free.
#include "backend/tabularDataObject.h"

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


const ibSourceDataObject::ibSourceExplorer* ibSourceDataObject::StepInto(const ibSourceExplorer* node,
	const ibSourceHop& hop, const ibTabularDataObject*& table, std::vector<ibValue>& held)
{
	if (node->GetHelperCount() > 0) {
		// CONTAINER node (a tabular section) — its columns are CHILDREN in the SAME explorer, and the
		// section itself is a field of its owner, so STEP INTO IT: from here its columns are ITS
		// business, and the object it yields is what the next turns ask.
		ibValue stepped;
		ibSourceDataObject* owner = node->GetOwner();
		if (owner != nullptr && owner->GetValueBySourceHop(hop, stepped)) {
			held.push_back(stepped);            // park it so the object outlives the walk
			stepped.ConvertToValue(table);
		}
		return node;
	}

	// REFERENCE field — hop into the target. Whoever the walk is STANDING ON answers: the table it
	// stepped into (its column, no row needed), else the node's owner. srcDataObject stays
	// METADATA-FREE either way — the override resolves the id and, for a composite / empty field,
	// hands back the pinned type's empty twin (CoerceHopType lives on the value side, not here).
	ibValue next;
	const bool stepped = (table != nullptr)
		? table->GetValueBySourceHop(hop, next)
		: (node->GetOwner() != nullptr && node->GetOwner()->GetValueBySourceHop(hop, next));
	if (!stepped)
		return nullptr;

	ibSourceDataObject* nextSrc = nullptr;
	next.ConvertToValue<ibSourceDataObject>(nextSrc);
	if (nextSrc == nullptr)
		return node;   // a PLAIN value — nothing under it, and its own node (no children) says so
	held.push_back(next);
	table = nullptr;   // left the table, standing on what its column referenced
	return nextSrc->GetSourceExplorer();
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
	ibString* outText, bool* outLeafIsTable, bool* outContainerIsTable, bool* outAvailable) const
{
	leaf = nullptr;
	if (outAvailable != nullptr) *outAvailable = true;
	const ibSourceExplorer* explorer = GetSourceExplorer();
	// A reference hop's design-time value is an empty typed reference-as-source; park them so the
	// explorers they vend (which the next node aliases) outlive each step.
	std::vector<ibValue> refHolders;
	// THE TABLE WE ARE STANDING IN, once the walk has stepped into one. Its columns are asked of IT from
	// there on — the step is a real one, through the object the previous hop yielded, instead of keeping
	// the root owner and asking it for a field that belongs to somebody else's rows.
	const ibTabularDataObject* table = nullptr;
	for (size_t i = from; i < path.size(); ++i) {
		if (explorer == nullptr)
			return false;   // the previous hop no longer resolves
		const ibSourceExplorer* node = explorer->FindById(path[i].m_id);
		if (node == nullptr)
			return false;   // not a column at this level (a plain value has none) -> broken binding
		if (outText != nullptr) { *outText += ibT("."); *outText += node->GetSourceName(); }
		leaf = node->GetColumn();
		if (outAvailable != nullptr && leaf != nullptr && !leaf->IsAvailable()) *outAvailable = false;
		if (i + 1 >= path.size()) {
			// Leaf reached — the control-class facts: is the leaf itself a table (a tabular section drops as a
			// tablebox), and is its container a table (then the leaf is a per-row COLUMN, bindable only INTO
			// that table). The source root itself flags table (the value-table / list invariant), so one
			// IsTableSection() answers the container uniformly at root and mid-path.
			if (outLeafIsTable != nullptr) *outLeafIsTable = node->IsTableSection();
			if (outContainerIsTable != nullptr) *outContainerIsTable = explorer->IsTableSection();
			break;
		}
		explorer = StepInto(node, path[i], table, refHolders);
	}
	return true;
}

#include "backend/backend_exception.h"   // a name that names nothing raises
#include "core/stringTokenizer.h"

const ibSourceDataObject::ibSourceExplorer* ibSourceDataObject::WalkColumns(const ibString& text, const ibString& walked,
	ibSourceDescription& path, std::vector<ibValue>& held) const
{
	const ibSourceExplorer* explorer = GetSourceExplorer();
	const ibTabularDataObject* table = nullptr;
	ibString written = walked;

	ibStringTokenizer segments(text, ibT("."));
	while (segments.HasMoreTokens()) {

		const ibString segment = segments.GetNextToken();
		const ibSourceExplorer* node = explorer != nullptr ? explorer->FindByName(segment) : nullptr;

		if (node == nullptr) {
			// A MISS IS A REFUSAL WITH THE ALTERNATIVES — the caller's next move is to pick one of them, and a
			// plain value has none to pick.
			ibString offered;
			for (unsigned int index = 0; explorer != nullptr && index < explorer->GetHelperCount(); ++index) {
				if (const ibSourceExplorer* field = explorer->GetHelper(index))
					offered << (offered.IsEmpty() ? ibT("") : ibT(", ")) << field->GetSourceName();
			}
			const ibString at = !written.IsEmpty() ? written
				: explorer != nullptr ? explorer->GetSourceName() : ibString();
			if (offered.IsEmpty())
				ibBackendCoreException::Error(_("'%s' holds a value - nothing unfolds from it, so '%s' cannot follow"),
					at, segment);
			ibBackendCoreException::Error(_("'%s' has no field called '%s'. It offers: %s"), at, segment, offered);
		}

		// A REFERENCE IS STEPPED INTO BY ITS TYPE: a table has no row to answer with, and steps into its column by that
		// type alone (CoerceHopType). The path keeps the type only where it goes on through, as the picker pins the
		// branch it opened; a reference the path ends on is written as the picker leaves it.
		const ibClassID type = node->GetTypeDesc().GetFirstClsid();
		const ibSourceHop hop = { node->GetSourceId(), ::IsReference(type) ? type : g_valueUndefinedCLSID };
		path.AppendSource(hop.m_id, segments.HasMoreTokens() ? hop.m_type : g_valueUndefinedCLSID);
		written = written.IsEmpty() ? segment : written + ibT(".") + segment;
		explorer = StepInto(node, hop, table, held);
	}
	return explorer;
}


