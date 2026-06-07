#ifndef __QUERYABLE_H__
#define __QUERYABLE_H__

// ibBackendQueryable — the data-navigation interface L3 (ibDataQueryBuilder)
// reads a metaobject through. ONE contract, implemented by the two queryable
// metaobject families:
//   ibValueMetaObjectRecordDataRef  (catalog / document / charts / enums)
//   ibValueMetaObjectRegisterData   (information / accumulation / accounting)
//
// L3 stays family-blind: the physical table, the row-identity (keyset) tail,
// reference/attribute materialisation, and virtual-table availability all come
// through here, so the per-family fork in the query builder dies. This is the
// "internals of the catalog / register" door — and it is where L3 learns
// whether a metaobject has virtual tables (balances / turnovers / slices).
//
// The interface carries NO data and does NOT derive ibValue — it is a pure
// mixin. Implementers list it as a SECOND base (after the ibValue-deriving
// metaobject base) so ibValue stays at offset 0 (see the first-base PMF rule).
//
// See docs/query-language-arc.md §18 (name substitution) / §20 (this interface).

#include "backend/tableInfo.h"          // ibComparisonType, ibMetaID
#include "backend/compiler/value.h"     // ibValue
#include "queryColumn.h"                // ibBackendQueryColumn (the column counterpart)

#include <vector>

class ibValueMetaObjectAttributeBase;
class ibDatabaseResultSet;

// ==========================================================================
// query-native vocabulary — the language between L3 and a queryable. NOT the
// dynamic-list UI types (ibFilterRow / ibSortOrder): the list layer translates
// those into the conditions / sorts below. Field identification is BY
// ATTRIBUTE (already resolved; the L4 text parser converts a name string into
// the attribute / metaID).
// ==========================================================================

// A query-native WHERE condition: a resolved attribute, a comparison, a value.
// L3-native filter operators beyond the list layer's Equal / NotEqual. Kept
// here (not L2's ibQueryBinOp) so the metadata side carries no L2 dependency —
// the query builder translates these to physical IR operators. (L3 doesn't pull
// L2 includes; see docs/query-language-arc.md §20.)
enum class ibQueryFilterOp { Like, Less, LessEqual, Greater, GreaterEqual };

struct ibQueryCondition
{
	const ibValueMetaObjectAttributeBase* m_attr       = nullptr;
	ibComparisonType                      m_comparison = ibComparisonType::ibComparisonType_Equal;
	ibValue                               m_value;

	// When m_explicitOp is set, m_op is the operator (LIKE / <= / < / > / >=) and
	// m_comparison is ignored. Filled by WhereLike / WhereCompare; the plain
	// Where(attr, comparison, value) path leaves it false (Eq / Ne).
	bool            m_explicitOp = false;
	ibQueryFilterOp m_op         = ibQueryFilterOp::Like;
};

// A query-native ORDER BY item: an attribute (null = the row-identity /
// reference sort, i.e. the queryable's row-key column) + direction.
struct ibQuerySortItem
{
	const ibValueMetaObjectAttributeBase* m_attr      = nullptr;   // null = row-key (reference/PK) sort
	bool                                  m_ascending = true;
};

// ==========================================================================

class BACKEND_API ibBackendQueryable
{
public:
	virtual ~ibBackendQueryable() = default;

	// --- name resolution (digest by string / id) ------------------------
	// The queryable digests an attribute reference and yields everything the
	// query builder needs. The L4 text parser feeds a NAME ("Номенклатура");
	// L3 feeds an already-resolved metaID. Null if no such attribute — L3 uses
	// this to validate a requisite against the metadata tree before building.
	// "Any attribute" = predefined (reference / recorder / period / ...) +
	// dimensions / resources / plain attributes, so EVERY queryable column is
	// reachable by one call.
	virtual const ibValueMetaObjectAttributeBase* ResolveAttribute(const wxString& name) const = 0;
	virtual const ibValueMetaObjectAttributeBase* ResolveAttribute(const ibMetaID& id) const = 0;

	// --- physical layout -------------------------------------------------
	// The real backing table for the main row scan.
	virtual wxString GetQueryTableName() const = 0;
	// This queryable's metaID — the parent-reference blob (tree filter) needs it.
	virtual ibMetaID GetQueryMetaID() const = 0;
	// The single row-key column, when the queryable has one (catalog: guidName;
	// register: empty — register identity is composite, see GetIdentitySort).
	virtual wxString GetRowKeyColumn() const = 0;

	// --- row identity / keyset tail --------------------------------------
	// Identity columns that give a cursor a TOTAL order; L3 appends them after
	// the user sort. The LAST one is unique. Returned as query-native sort
	// items so L3 builds ONE uniform keyset with no family fork:
	//   catalog/document : { { attr=nullptr } }      -> the row-key column
	//   register         : recorder+line  OR  period?+dimensions  (real attrs)
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const = 0;

	// Is this attribute the row-reference column? (catalog reference attribute;
	// registers have none -> always false.)
	virtual bool IsReferenceAttribute(const ibMetaID& id) const = 0;

	// --- dot-walk join key -----------------------------------------------
	// The physical column holding THIS row's own reference (the self-reference
	// _RRRef blob, ibReference[guid][metaID]) — the JOIN key a dot-walk binds to
	// when this queryable is the TARGET of a reference: source.<ref>_RRRef =
	// target.<this>_RRRef, byte-identical (the row stores its own reference, see
	// the data-reference column). Empty when the queryable is not a reference
	// target (register / constant today). (docs/query-language-arc.md §22 dot-walk)
	virtual wxString GetReferenceKeyColumn() const { return wxEmptyString; }

	// Resolve a single-target reference COLUMN of this queryable to the queryable of
	// the object it points at — the dot-walk join target. Works off the column's type
	// (ibBackendQueryColumn::GetTypeDesc) + this queryable's metadata. Null if the
	// column is not a reference, is polymorphic (more than one target type), or the
	// target vends no queryable. Navigation lives on the queryable (it owns the
	// metadata context); the door just chains the result. (docs §22 dot-walk)
	virtual const ibBackendQueryable* ResolveReferenceTarget(const ibBackendQueryColumn* refColumn) const { return nullptr; }

	// --- row materialisation ---------------------------------------------
	// The queryable knows its own row shape.
	// Row-key string of the current result row (catalog: guidName; register:
	// empty — the register consumer assembles its composite identity itself).
	virtual wxString MaterializeRowKey(ibDatabaseResultSet* rs) const = 0;
	// One attribute of the current row as a ready ibValue (the reference
	// attribute yields the row's reference object, so callers loop uniformly).
	virtual ibValue MaterializeAttribute(const ibValueMetaObjectAttributeBase* attr,
	                                     ibDatabaseResultSet* rs) const = 0;

	// --- computed (RAM) queryables ---------------------------------------
	// The main queryable (and the record families) is read by a physical DB scan.
	// A COMPUTED virtual table — a register slice / balance / turnover — is a
	// SELF-CONTAINED relation: its own filters (period, dimensions) are baked into
	// the instance's constructor, and it produces its rows in RAM. You construct
	// one, hand it to From(), and L3 reads it like any source — it checks
	// IsComputedInRam() and, when true, builds the selection from ComputeRows()
	// instead of the DB provider. So the same door reads physical and computed
	// sources alike, and the computed result is a RAM relation L3 can join. The
	// instance is call-scoped (it carries one query's filters). `extra` is any
	// further door conditions to compose on top (post-filter); empty for now.
	// Default: physical. (docs/query-language-arc.md §22.4d, §22.6 — RAM-set.)
	virtual bool IsComputedInRam() const { return false; }
	virtual ibValue ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const { return ibValue(); }
};

// ==========================================================================
// ibBackendQueryableHolder — the common interface for anything that VENDS an
// ibBackendQueryable. Catalogs, documents, charts, enumerations, registers,
// constants AND tabular sections all implement it (a metaobject HAS-A queryable;
// it no longer IS one). L4 and the door reach a metaobject's data uniformly through
// holder->GetQueryable(), without knowing the concrete metaclass — null when the
// metaobject has no queryable. (docs/query-language-arc.md §22.4e — decouple)
// ==========================================================================
class BACKEND_API ibBackendQueryableHolder
{
public:
	virtual ~ibBackendQueryableHolder() = default;
	virtual const ibBackendQueryable* GetQueryable() const = 0;
};

// ibBackendQueryColumn — the column counterpart, lives in queryColumn.h (included
// above) so the fundamental attribute metaobject can derive from it lightly.

#endif
