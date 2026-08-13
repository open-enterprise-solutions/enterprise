#ifndef __SRC_DATA_OBJECT_H__
#define __SRC_DATA_OBJECT_H__

#include <vector>

#include "backend/srcObject.h"          // ibSourceObject (base) + ibValueMetaObjectCompositeData fwd
#include "backend/query/queryColumn.h"  // ibBackendQueryColumn / ibBackendSourceColumn / ibTypeDescription (nested ibSourceExplorer)
#include "backend/uniqueKey.h"          // ibUniqueKey (GetGuid)
#include "backend/lock/lockHandle.h"    // ibLockHandle (m_formLockHandle)
#include "backend/lock/lockTypes.h"     // ibLockMode (TryAcquireFormLock)
#include "backend/compiler/value.h"     // ibValue (inline GetValueByPath default-constructs one)
#include "backend/sourceDescription.h"  // ibSourceHop — a binding path is {id, expected type} per hop

#include "backend/metaCollection/genericData.h"   // ibValueMetaObjectGenericData COMPLETE -> the covariant GetSourceMetaObject() -> GenericData* compiles (no casts). genericData.h fwd-declares ibSourceDataObject, so no cycle.

// ibSourceDataObject: a concrete SOURCE instance, mostly metadata-independent. The self-describing
// data node the binding hops over (GetSourceExplorer -> node by id -> GetValue -> next source).
// Extracted from commonObject.h so the reference value can inherit it without a cycle (commonObject.h
// includes reference.h before this class). m_sourceExplorer is empty (no alloc) until first used.
class BACKEND_API ibSourceDataObject : public ibSourceObject {
public:

	// ibSourceExplorer: a source's column/field TEMPLATE (one-time form generation + the picker),
	// NESTED in ibSourceDataObject -- a node is only ever produced by / bound to a source, so the
	// type lives inside the source's type. METADATA-FREE: a node holds plain values (name / synonym /
	// id / type) + UI flags; a column is appended from the neutral ibBackendQueryColumn (a metaobject
	// attribute IS one, a queryable column IS one) -- never a metaobject pointer.
	class ibSourceExplorer {
	public:

		// Node flags — packed into ONE int instead of a byte-per-bool (the node struct is copied per
		// column; separate bools + padding cost ~8 bytes; room left for more flags incl. a future
		// "how to fetch the value" mode). eSrcDefault marks a node whose value is a DEFAULT: when the
		// constructor generates a NEW form it reads the explorer and substitutes this value at init.
		// Which values are default is decided PER OBJECT (each source sets the flag on its own nodes).
		enum ibSourceFlag {
			eSrcEnabled = 1 << 0,
			eSrcVisible = 1 << 1,
			eSrcTableSection = 1 << 2,
			eSrcSelect = 1 << 3,
			eSrcDefault = 1 << 4,
			eSrcChoiceMode = 1 << 5,   // a TABLE node is a value PICKER (choice mode): the tablebox shows Select first
		};

	private:

		struct ibSourceInfo {
			wxString          m_srcName;
			wxString          m_srcSynonym;
			ibMetaID          m_mid = wxNOT_FOUND;
			ibTypeDescription m_typeDesc;
			int               m_flags = eSrcEnabled | eSrcVisible | eSrcSelect;   // packed; default: enabled+visible+select
			ibSourceDataObject* m_owner = nullptr;   // source this node fetches its live value FROM (set by GetSourceExplorer)
			const ibBackendSourceColumn* m_col = nullptr;   // descriptor this node was built FROM (the leaf WalkSource/structure-hop returns); null for a plain-value node
		};

	public:

		ibSourceExplorer() {}

		// The one value ctor — every node is built from plain values + flags, no metaobject.
		ibSourceExplorer(const wxString& name, const wxString& synonym, const ibMetaID& id,
			const ibTypeDescription& typeDesc, bool tableSection = false, bool select = true,
			bool enabled = true, bool visible = true) {
			m_sourceInfo.m_srcName = name;
			m_sourceInfo.m_srcSynonym = synonym;
			m_sourceInfo.m_mid = id;
			m_sourceInfo.m_typeDesc = typeDesc;
			m_sourceInfo.m_flags = (enabled ? eSrcEnabled : 0) | (visible ? eSrcVisible : 0)
				| (tableSection ? eSrcTableSection : 0) | (select ? eSrcSelect : 0);
		}

		// Queryable-style node: the name doubles as the synonym.
		ibSourceExplorer(const wxString& name, const ibMetaID& id, const ibTypeDescription& typeDesc)
			: ibSourceExplorer(name, name, id, typeDesc) {
		}

		wxString GetSourceName()    const { return m_sourceInfo.m_srcName; }
		wxString GetSourceSynonym() const { return m_sourceInfo.m_srcSynonym; }
		ibMetaID GetSourceId()      const { return m_sourceInfo.m_mid; }
		const ibTypeDescription& GetTypeDesc() const { return m_sourceInfo.m_typeDesc; }   // node's Type (structure-hop reads the reference target)
		const ibBackendSourceColumn* GetColumn() const { return m_sourceInfo.m_col; }      // descriptor (the leaf WalkSource returns); null for a plain-value node

		// THE NODE'S PICTURE, asked of the column it was built from — so a dimension and a resource
		// are drawn apart without the drawer knowing either word. Null when the node stands behind
		// no column (a plain-value node); the reader then draws its own default.
		wxIcon GetSourceIcon() const {
			return m_sourceInfo.m_col != nullptr ? m_sourceInfo.m_col->GetColumnIcon() : wxNullIcon;
		}


		bool IsEnabled()      const { return (m_sourceInfo.m_flags & eSrcEnabled) != 0; }
		bool IsVisible()      const { return (m_sourceInfo.m_flags & eSrcVisible) != 0; }
		bool IsTableSection() const { return (m_sourceInfo.m_flags & eSrcTableSection) != 0; }
		bool IsSelect()       const { return (m_sourceInfo.m_flags & eSrcSelect) != 0; }
		bool IsDefault()      const { return (m_sourceInfo.m_flags & eSrcDefault) != 0; }
		void SetDefault(bool on = true) { if (on) m_sourceInfo.m_flags |= eSrcDefault; else m_sourceInfo.m_flags &= ~eSrcDefault; }

		// Choice mode — this table node is a value PICKER (opened to return a selection). Set by the open-as-choice
		// path; the form builder / tablebox reads it to show the Select command first.
		bool IsChoiceMode()   const { return (m_sourceInfo.m_flags & eSrcChoiceMode) != 0; }
		void SetChoiceMode(bool on = true) { if (on) m_sourceInfo.m_flags |= eSrcChoiceMode; else m_sourceInfo.m_flags &= ~eSrcChoiceMode; }

		const std::vector<ibClassID>& GetClsidList() const { return m_sourceInfo.m_typeDesc.GetClsidList(); }
		bool ContainType(const ibValueTypes& valType) const { return m_sourceInfo.m_typeDesc.ContainType(valType); }
		bool ContainType(const ibClassID& cid) const { return m_sourceInfo.m_typeDesc.ContainType(cid); }

		// Append a COLUMN from its neutral descriptor (an attribute / a queryable column — both
		// ibBackendQueryColumn). Skipped when the column is not allowed (a deleted / disabled field):
		// the metadata judgement lives on the column, not here.
		void AppendColumn(const ibBackendQueryColumn* col, bool enabled = true, bool visible = true) {
			if (col == nullptr || !col->IsAllowed()) return;
			m_arraySource.emplace_back(ibSourceExplorer{ col->GetName(), col->GetSynonym(), col->GetColumnId(),
				col->GetTypeDesc(), /*tableSection*/false, /*select*/true, enabled, visible });
			m_arraySource.back().m_sourceInfo.m_owner = m_sourceInfo.m_owner;   // child fetches through the same source
			m_arraySource.back().m_sourceInfo.m_col = col;                      // keep the descriptor (a query column IS-A source column)
		}

		// Append a COLUMN by plain values (a queryable column with no descriptor object).
		void AppendColumn(const wxString& name, const ibMetaID& id, const ibTypeDescription& typeDesc) {
			m_arraySource.emplace_back(ibSourceExplorer{ name, id, typeDesc });
			m_arraySource.back().m_sourceInfo.m_owner = m_sourceInfo.m_owner;
		}

		// Append a COLUMN from an ABSTRACT source-column descriptor + its id. Unlike the ibBackendQueryColumn
		// overload (a DB / queryable column that carries its own GetColumnId), a plain source column (e.g. a
		// RAM value-table column) has no query identity, so the id is passed explicitly. The descriptor is
		// KEPT (m_col) so WalkColumns returns it as the leaf — the header / type resolve through it.
		void AppendColumn(const ibBackendSourceColumn* col, const ibMetaID& id, bool enabled = true, bool visible = true) {
			if (col == nullptr || !col->IsAllowed()) return;
			m_arraySource.emplace_back(ibSourceExplorer{ col->GetName(), col->GetSynonym(), id,
				col->GetTypeDesc(), /*tableSection*/false, /*select*/true, enabled, visible });
			m_arraySource.back().m_sourceInfo.m_owner = m_sourceInfo.m_owner;
			m_arraySource.back().m_sourceInfo.m_col = col;
		}

		// Append a TABLE-SECTION node (tableSection = true). Returns a reference so the caller adds the
		// section's own columns to it via AppendColumn (the form builder reads them as a sub-tablebox).
		ibSourceExplorer& AppendTable(const wxString& name, const wxString& synonym, const ibMetaID& id,
			const ibTypeDescription& typeDesc) {
			m_arraySource.emplace_back(ibSourceExplorer{ name, synonym, id, typeDesc, /*tableSection*/true });
			m_arraySource.back().m_sourceInfo.m_owner = m_sourceInfo.m_owner;
			return m_arraySource.back();
		}

		// By POINTER — no copy of the node + its child vector (the no-allocation discipline). An out-of-
		// range idx returns nullptr.
		const ibSourceExplorer* GetHelper(unsigned int idx) const {
			return idx < m_arraySource.size() ? &m_arraySource[idx] : nullptr;
		}

		unsigned int GetHelperCount() const { return static_cast<unsigned int>(m_arraySource.size()); }

		// Find a direct child node by its SourceId — the per-hop lookup the structure walk uses (the
		// design-time twin of GetValueByMetaID). A miss returns nullptr, so the caller tells "found"
		// from "broken binding" by null.
		const ibSourceExplorer* FindById(const ibSourceId& id) const {
			for (size_t i = 0; i < m_arraySource.size(); ++i)
				if (m_arraySource[i].m_sourceInfo.m_mid == id) return &m_arraySource[i];
			return nullptr;
		}

		// The NAME form of the same lookup — for the walks that arrive with a written path segment rather
		// than an id (a RAM filter / sort key, a dotted field typed into a list's settings). CASE-INSENSITIVE,
		// like every other name resolution in the model layer (GetColumnByName). It exists because three
		// call sites wrote this loop by hand and only two of them agreed on the case rule: the third
		// compared exactly, so a filter written `Supplier.region` against a field named `Region` silently
		// stopped filtering instead of matching. One rule, in one place.
		const ibSourceExplorer* FindByName(const wxString& name) const {
			for (size_t i = 0; i < m_arraySource.size(); ++i)
				if (m_arraySource[i].GetSourceName().IsSameAs(name, false)) return &m_arraySource[i];
			return nullptr;
		}

		// In-place refill: drop the children but KEEP capacity (no realloc) and KEEP the owner (bound once
		// at construction). Only a subclass mutates m_sourceExplorer; external code holds a const ref.
		void Clear() {
			m_arraySource.clear();
			ibSourceDataObject* owner = m_sourceInfo.m_owner;
			m_sourceInfo = ibSourceInfo{};
			m_sourceInfo.m_owner = owner;
		}

		// Reset to a fresh ROOT with this info for an in-place rebuild — drops children (KEEPS capacity) and
		// KEEPS the owner (bound at construction). A subclass calls this at the top of GetSourceExplorer,
		// Appends its columns into *this, then returns *this (now a const& to the owner-bound member).
		ibSourceExplorer& Reset(const wxString& name, const wxString& synonym, const ibMetaID& id,
			const ibTypeDescription& typeDesc, bool tableSection = false, bool select = true) {
			ibSourceDataObject* owner = m_sourceInfo.m_owner;
			m_arraySource.clear();
			m_sourceInfo = ibSourceInfo{};
			m_sourceInfo.m_srcName = name;
			m_sourceInfo.m_srcSynonym = synonym;
			m_sourceInfo.m_mid = id;
			m_sourceInfo.m_typeDesc = typeDesc;
			m_sourceInfo.m_flags = eSrcEnabled | eSrcVisible | (tableSection ? eSrcTableSection : 0) | (select ? eSrcSelect : 0);
			m_sourceInfo.m_owner = owner;
			return *this;
		}

	private:

		// The owner is set ONLY through this PRIVATE ctor — friend ibSourceDataObject builds its member
		// explorer with itself; external code can neither construct an owned explorer nor re-point it. Child
		// nodes inherit the owner at Append (all of a source's columns fetch through that same source).
		friend class ibSourceDataObject;
		explicit ibSourceExplorer(ibSourceDataObject* owner) { m_sourceInfo.m_owner = owner; }
		ibSourceDataObject* GetOwner() const { return m_sourceInfo.m_owner; }

	protected:

		ibSourceInfo m_sourceInfo;
		std::vector<ibSourceExplorer> m_arraySource;
	};


	// Bind the per-instance explorer to THIS source at construction — owner passed to the explorer's
	// PRIVATE ctor (friend access). Subclasses fill m_sourceExplorer's nodes; external code only READS
	// it via GetSourceExplorer.
	ibSourceDataObject() : m_sourceExplorer(this) {}
	virtual ~ibSourceDataObject() {}

	//counter
	virtual void SourceIncrRef() = 0;
	virtual void SourceDecrRef() = 0;

	// Long-held sys_lock acquire/release for form-open hold pattern.
	// Default = no-op (sources without a meaningful lock identity
	// — DataProcessor, Report — just silently succeed). Concrete
	// data-bound sources override:
	//   ibValueRecordDataObjectRef → keys by ref guid
	//   (future) ibValueRecordSetObject → keys by m_keyValues
	//   (future) ibValueConstantDataObject → keys by const name
	//
	// Storage lives on the base (m_formLockHandle below) — overrides
	// reuse the same field for RAII release on source dtor.
	//
	// See docs/record-locks.md "Planned upgrade path" / Phase B.3.
	virtual bool TryAcquireFormLock(ibLockMode /*mode*/ = ibLockMode::Exclusive) { return true; }
	virtual void ReleaseFormLock() { m_formLockHandle.Release(); }

	//override default type object
	virtual bool IsNewObject() const { return true; }

	//standart override 
	virtual bool IsEmpty() const = 0;

	//standart override 
	virtual bool IsModified() const { return false; }
	virtual void Modify(bool mod) {}

	//save source modify  
	virtual bool SaveModify() { return true; }

	//check is changes data in db
	virtual bool ModifiesData() { return false; }

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const = 0;

	//get metaData from object. Covariant ibValueMetaObjectGenericData* — the PRECISE type, no caller-side
	// cast: GenericData is COMPLETE here via genericData.h (extracted from commonObject.h so srcDataObject.h
	// can include it without the reference.h cycle). Concrete data sources narrow further (RecordData* etc.)
	// in their own headers.
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const = 0;

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const = 0;

	// THE hop gate — read/write a value addressed by a HOP (id + the type the path pinned) instead of a bare
	// id. The dot-walk fetches THROUGH these; every other caller (runtime, object-to-object value copy) still
	// uses the id-only primitives above, which these sit on. A hop is really {metaID, clsid}: the id-only call
	// never knew the target type, so it could not tell a COMPOSITE reference apart — this can. Gateway bool =
	// did the hop RESOLVE, independent of whether `out` ends up undefined: true + undefined `out` means
	// "resolved, legitimately empty, contract met" (a composite fork with no live value and no usable pin);
	// false means the id is not a field here. Where the field yields a value of a DIFFERENT branch than the
	// pin (or empty), the gate hands back an EMPTY TYPED twin of the pin so the hop steps into the right
	// branch — generated INSIDE the value (this virtual), from the source's own metaData, so the walk stays
	// metadata-free. A live value already of the pinned type is returned AS IS (never fabricated over).
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const { return false; }
	virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) { return false; }

	// Is this source a TABLE (renders as a tablebox) or a scalar object? A pure TYPE fact,
	// answered by the class factory through the source's own CLSID — the metaobject ctor
	// registry first (a metatype List / TabularSection / RecordSet), else the global value
	// registry (any ibValueModel, incl. the queryable-based dynamic list, which carries no
	// metaobject). No dynamic_cast, and no dependence on the source's data / queryable being
	// populated — a dynamic list with no source picked yet still reports a table.
	bool IsTableSource() const;

	// Path access: the same source fed an ordered metaId path instead of a single id.
	// The first id is read off the source itself (GetValueByMetaID); each further id
	// steps into the previous reference value. A one-id path is just GetValueByMetaID.
	// The path is a plain lightweight vector — no property-side description type leaks
	// into the source. The walker lives on the source so every kind (RAM / list /
	// object / record set / manager) inherits one traversal — read-only navigation.
	bool GetValueByPath(const std::vector<ibSourceHop>& path, ibValue& pvarMetaVal) const;

	// THE shared deep-hop — ONE code path for fetching data along a dotted source path. Each step's
	// value IS a source object (a reference now inherits ibSourceDataObject), so it self-describes the
	// next id: value -> ConvertToValue<ibSourceDataObject> -> GetValueByMetaID(id) -> next value. Static
	// because the starting value need not be THIS source — the tablebox renderer feeds the row's
	// reference cell (resolved through its dumb model), GetValueByPath feeds its own first hop. Gateway
	// boolean: false = a hop hit a non-source (a primitive mid-path) or a missing id; `out` is the final
	// value only on true. Metadata-free — no metaID -> name -> FindProp round-trip.
	static bool ResolvePath(const ibValue& start, const std::vector<ibSourceHop>& path, size_t from, ibValue& out)
	{
		ibValue current = start;
		for (size_t i = from; i < path.size(); ++i) {
			ibSourceDataObject* source = nullptr;
			current.ConvertToValue<ibSourceDataObject>(source);
			if (source == nullptr)
				return false;   // a non-source value (a primitive) ends the walk: you cannot dot into it
			ibValue next;
			if (!source->GetValueBySourceHop(path[i], next))   // THE hop gate — the live value, or an empty typed twin of the pin
				return false;
			current = next;
		}
		out = current;
		return true;
	}


	// THE structure-resolve hop — the design-time twin of ResolvePath, the ONE gate WalkSource and the
	// picker resolve a dotted path through. Walks path[from..] through the source EXPLORERS: this source's
	// explorer -> node by id -> the node's value (an empty-but-TYPED reference at design time) is the next
	// source -> repeat. The SAME mechanism as the runtime value-hop, only it collects the leaf column +
	// dotted name instead of a value. Gateway boolean: true iff every hop resolved (a plain-value leaf with
	// no column is still a VALID binding); `leaf` is the last node's descriptor (may be null), `outText`
	// (optional) accumulates the dotted display name. `outLeafIsTable` / `outContainerIsTable` (optional)
	// report the leaf's own table-ness and whether the container it sits in is a table (then the leaf is
	// that table's per-row COLUMN) — the facts a control-class choice needs but `leaf` (null for a section)
	// cannot carry.
	bool WalkColumns(const std::vector<ibSourceHop>& path, size_t from, const ibBackendSourceColumn*& leaf,
		wxString* outText = nullptr, bool* outLeafIsTable = nullptr, bool* outContainerIsTable = nullptr) const;

protected:
	
	// Storage for TryAcquireFormLock / ReleaseFormLock. RAII-released
	// on source destruction (form holds source via refcount; source
	// dies when form closes → handle dtor DELETEs sys_lock row).
	// Empty by default; populated by concrete-source overrides of
	// TryAcquireFormLock.
	ibLockHandle m_formLockHandle;

	// Per-instance self-description — created WITH the source, owner-bound at construction. ONLY
	// subclasses fill it (each source builds its own nodes); external code reads it via GetSourceExplorer.
	mutable ibSourceExplorer m_sourceExplorer;
};

// Global alias so every existing 'ibSourceExplorer' spelling keeps resolving now the type is nested.
using ibSourceExplorer = ibSourceDataObject::ibSourceExplorer;

#endif
