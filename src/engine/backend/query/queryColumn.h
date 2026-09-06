#ifndef __QUERY_COLUMN_H__
#define __QUERY_COLUMN_H__

// ibBackendQueryColumn — the column counterpart of ibBackendQueryable: ONE logical,
// typed field of a queryable. It is a PURELY L3 descriptor —
//   name      : the logical column name (the requisite the script names),
//   physical  : the base physical column (db field name) it maps to,
//   type      : its ibTypeDescription (CLSIDs + number / string / date qualifiers).
// That is all L3 needs to reason about a column (projection, validation, virtual
// tables). The physical multi-column split (TYPE / _N / _S / _RRRef) and the value
// binding are LOWERING — derived from (physical, type) below the L3 surface, never
// on this interface. No statement, no cursor, no positions here.
//
// Lives in its OWN light header (only typeDescription.h) so the fundamental
// attribute metaobject can name it without dragging in the full queryable.h (and
// model.h) weight.
//
// ⚠ AN ATTRIBUTE IS NOT ONE OF THESE — IT HOLDS ONE. It stays an ibBackendSourceColumn (the
// descriptive face the form binding walks to) and carries its QUERY face as a member,
// ibValueMetaObjectAttributeBase::ibMetaAttributeColumn, held by shared_ptr. The reason is
// ownership: the attribute lives under the runtime's own reference count and this interface is
// held by std::shared_ptr, and two counts over one object each believe they may destroy it.
// See docs/ownership-authority.md. Nothing on THIS side changed — every tier below the L3 door
// still meets a plain ibBackendQueryColumn and knows nothing about where it came from.

#include "backend/typeDescription.h"    // ibTypeDescription (the column's L3 type)
#include "backend/databaseLayer/columnType.h"   // ibColumnType — the canonical type a column's field declares

// ⚠ NOT <wx/icon.h> — FORWARD-DECLARED. backend.dll is GUI-free (CLAUDE.md), and this header is
// included by every attribute metaobject, so one GUI include here reaches ~25 direct includers and
// everything behind them. A virtual returning wxIcon compiles against an incomplete type; only the
// two files that DEFINE such a body need the real header, and they already include it.
class wxIcon;

#include <memory>   // enable_shared_from_this — a column carries its own control block (see below)
#include <vector>

// The role a physical field plays in a logical column's spread (an L3 layout concept — L2 never sees
// it). Declared HERE, with the column, because it is the column that answers with these
// (ibBackendQueryColumn::DescribeLayout below); the layout tier reads them from this header.
enum class ibColumnRole : uint8_t {
	Raw,             // a raw physical column — its own single field, type carried by the column
	Discriminator,   // _TYPE  — the variant type tag
	Boolean,         // _B
	Number,          // _N
	Date,            // _D
	String,          // _S
	Enum,            // _E
	ReferenceType,   // _RTRef — the reference target's clsid (BIGINT)
	ReferenceId,     // _RRRef — the reference value (pure guid blob; type is _RTRef)
	Schedule,        // _SCH   — a JobSchedule, serialised whole (blob); see ibFieldTypes_Schedule
	TypeDescription  // _TD    — a type description, serialised whole (blob). Same reasoning as the
	                 //          schedule: what it carries is a SET OF ADMISSIBLE TYPES with their
	                 //          qualifiers, and nothing filters on it in SQL — so spreading it into
	                 //          columns would buy a predicate nobody writes and cost an ALTER every
	                 //          time the type system grows. It is read to NARROW a value, in memory.
};

// One physical field of a logical column (the layout decomposition unit).
struct ibColumnSlot {
	wxString     m_name;                       // physical column name (base + suffix)
	ibColumnRole m_role    = ibColumnRole::Raw;
	ibColumnType m_type;                       // canonical type for DDL (MapType -> dialect SQL)
	wxString     m_default;                    // DEFAULT clause (e.g. "0"), empty = none
	bool         m_notNull = false;
};

// ibFieldTypes — the persisted VARIANT TAG stored in a composite column's _TYPE field: which of
// the column's allowed types the row actually holds. The L3 column vocabulary, shared by the
// layout tier, the value codec and the (forwarding) attribute. Moved here out of the heavy
// attribute header so the tier can speak it without dragging the metadata class in.
enum ibFieldTypes {
	ibFieldTypes_Empty = 0,
	ibFieldTypes_Boolean,
	ibFieldTypes_Number,
	ibFieldTypes_Date,
	ibFieldTypes_String,
	ibFieldTypes_Null,
	ibFieldTypes_Enum,
	ibFieldTypes_Reference,
	// A SCHEDULE (JobSchedule) — a value object stored whole, in its own blob field. It is here
	// rather than folded into one of the tags above because it is neither a primitive nor a
	// reference: nothing points at it, it has no row of its own, and it is read back by
	// deserialising the blob. The tag is APPENDED, never inserted — it is persisted in every
	// composite column's _TYPE field, so a renumbering would re-read old rows as another type.
	ibFieldTypes_Schedule,
	// A TYPE DESCRIPTION — the same shape as the schedule: a value object stored whole in its own
	// blob field, neither primitive nor reference. It carries a SET OF ADMISSIBLE TYPES with their
	// qualifiers (a characteristic's own Type) and is read back by deserialising the blob.
	//
	// APPENDED AT THE END for the reason stated above — these values are persisted in every
	// composite column's _TYPE field, so inserting one renumbers every tag after it and old rows
	// start reading as a different type.
	ibFieldTypes_TypeDescription,
};

// (ibSQLField — the structured "_TYPE + per-type field" projection — is REMOVED. Its analog is the
//  column-layout tier: DescribeColumnLayout (slots) / ColumnFieldNames / ColumnFieldList. The enum
//  ibFieldTypes above stays — it is the persisted _TYPE discriminator value, used by the codec.)

// ibBackendSourceColumn — the SOURCE/UI face of a column: name, synonym, type. The minimal
// "column, like a DB column". It is the BASE of ibBackendQueryColumn (every query/DB column IS a
// source column) and, through it, of the attribute metaobject — so the source-binding dot-walk
// (ibBackendTypeSourceFactory) returns THIS, blind to the concrete class: a metaobject attribute
// OR a dynamic list's queryable column, both already ARE an ibBackendSourceColumn, no adapter.
// ibBackendAbstractColumn — the NAME / SYNONYM / COMMENT face shared by a metadata source column
// AND a form attribute (the "linking element"): a control reads the caption / comment from it without
// knowing which it is. Both ibBackendSourceColumn (metadata / query column) and the form attribute
// (ibBackendFormAttributeValue) derive it, so ONE resolver returns either, uniformly.
class BACKEND_API ibBackendAbstractColumn
{
public:
	virtual ~ibBackendAbstractColumn() = default;

	// Logical identifier — what a script / the L4 parser refers to.
	virtual wxString GetName() const = 0;

	// Display caption. Defaults to the logical name; a metaobject column / a form attribute override
	// it with their synonym.
	virtual wxString GetSynonym() const { return GetName(); }

	// Tooltip / comment. Empty by default; a metaobject column / a form attribute override it.
	virtual wxString GetComment() const { return wxEmptyString; }
};

class BACKEND_API ibBackendSourceColumn : public ibBackendAbstractColumn
{
public:
	virtual ~ibBackendSourceColumn() = default;

	// GetName / GetSynonym / GetComment come from ibBackendAbstractColumn.

	// The column's L3 type — CLSIDs + number / string / date qualifiers. The single source of
	// "what this column holds". The SAME accessor the attribute already exposes — free.
	virtual ibTypeDescription& GetTypeDesc() const = 0;

	// WHAT A VALUE HERE MAY BE — the same object for every ordinary column. Declared on BOTH bases (the
	// type factory names it too, backend_type.h) for the same reason GetTypeDesc is: an attribute is a
	// column AND a type factory, and one overrider then answers for both. A column that stands behind
	// no declaration of its own — temp, computed — inherits this default and never differs.
	virtual ibTypeDescription& GetTypeValueDesc() const { return GetTypeDesc(); }

	// Is the column usable / shown? A plain (queryable) column always is; a metaobject attribute
	// overrides — a deleted or access-denied field is not. The metadata-agnostic source explorer
	// gates on THIS instead of poking the metaobject.
	virtual bool IsAllowed() const { return true; }

	// THE COLUMN'S OWN PICTURE — asked of the column, never deduced by the reader. The default is
	// the plain ATTRIBUTE picture from the icon library, so every column is dressed even when it
	// stands behind no metaobject (a view's column, a temp table's); a column that IS a metaobject
	// answers with whatever picture its own metatype registered. That is what keeps a dimension
	// from looking like a resource without anybody, anywhere, keeping a list of kinds and a switch
	// over it — a metatype added tomorrow is dressed the day it registers an icon.
	//
	// Body in metaAttributeObject_res.cpp, next to the icon it returns: this header must not pull
	// the metadata tree in (the attribute metaobject includes THIS file).
	virtual wxIcon GetColumnIcon() const;
};

// ⭐⭐ A COLUMN CARRIES ITS OWN CONTROL BLOCK, so nobody ever has to invent a second one.
//
// A `shared_ptr` built from a raw pointer CANNOT tell that the object is already owned: it makes
// its OWN control block, with its own count, and both of them delete at zero. There is no
// diagnostic for that — not in the compiler, not at runtime. The only defence is to never need the
// wrapping, and `enable_shared_from_this` is exactly that: the weak reference it keeps INSIDE the
// object points at the control block that already exists.
//
//     col->weak_from_this().lock()   ->  the real holder, or empty
//
// Empty is a fact, not a failure: a column the configuration owns, or one that lives as a member,
// has no shared owner and nothing to hand out. This is the same discriminator ibRunContext uses to
// tell a heap-promoted frame from a stack one (compiler/procContext.h) — no second flag, no map of
// who owns what.
//
// It also retires the question `ibBackendQueryable::ShareColumn` was invented to answer. That asks
// every SOURCE in turn "did you mint this column, and if so give me its storage"; asking the column
// itself needs no loop and no knowledge of where it came from. (docs/ownership-authority.md)
class BACKEND_API ibBackendQueryColumn : public ibBackendSourceColumn,
                                         public std::enable_shared_from_this<ibBackendQueryColumn>
{
public:
	virtual ~ibBackendQueryColumn() = default;

	// Base physical column (db field name). The lowering derives the actual
	// per-type physical columns from this plus the type description.
	virtual wxString GetPhysicalName() const = 0;

	// The column's id WITHIN its source/model — the key its source reads a value by.
	// For a DB / attribute column this IS the metaID; for a computed / temp column it
	// is the source's own internal unique id ("source id") — whatever the queryable
	// keys its rows by. The point: each column self-describes its read key, so a model
	// (RAM) read is GetValueByMetaID(col->GetColumnId()) with NO attribute resolution —
	// and a non-metaobject temp column fits the same shape. UNIQUE PER COLUMN (a
	// resource's Balance vs Turnover have distinct model ids, though one metaID).
	virtual ibMetaID GetColumnId() const = 0;

	// ⭐⭐ …AND AN ID NOTHING DECLARED IS A NEGATIVE ONE — the sign carries the classification.
	//
	// The id above is a CONFIGURATION number wherever a metaobject stands behind the column: the
	// metadata tree hands those out, and they are positive and small. A query also needs ids for
	// columns nothing declared — an aggregate's figure, a computed projection, a second reading of one
	// table — and those are minted NEGATIVE, so no query column can ever be mistaken for a declared
	// one. (They are handed out by the DOOR that assembles the query — ibDataQueryBuilder — because
	// minting is a property of building a query, not of being a column. This is the reading side.)
	//
	// 🛑 They used to be carved out of the POSITIVE space in bands (0x4000'0000 aggregates,
	// 0x5000'0000 minted columns, 0x6000'0000 subquery folds, 0x7000'0000 stitch outputs), and that
	// bookkeeping had already failed twice by 2026-09-06: one band held TWO tenants, `ibMetaID` is a
	// signed int so 0x7000'0000 was the last band there is, and I walked into an occupied one adding a
	// fifth. A map of bands has to be READ before every addition, and nothing makes anybody read it.
	// The sign does what the map was trying to do, structurally (Max, 2026-09-06).
	//
	// ⭐⭐ A COLUMN NOBODY DECLARED SAYS SO BY ITS SIGN — and the KIND is stamped on BY THE COLUMN.
	//
	// Whoever makes such a column hands in a plain ordinary number — the id of the column it stands for,
	// a position it is already walking, a running count of its own. The synthetic column then STORES it with
	// its own kind composed onto it, because the class is the thing that knows which kind it is
	// (Max, 2026-09-06: *"the synthetic takes an id in its argument, and when it stores it, it adds
	// its own kind"*). The result is negative, so no minted id can ever be a configuration number.
	//
	// 🛑 This replaced five hand-carved BANDS in the positive space — one of which already held two
	// tenants, against a ceiling made by the sign bit. A band map has to be READ before every
	// addition and nothing makes anybody read it; I walked into an occupied one adding a sixth.
	//
	// ⚠ Nothing is decoded back out. Where such an id is read, what it stands for is known from where
	// the reader is standing.
	enum class SyntheticKind : unsigned {
		Output = 1,   // an output of the query with no declared column behind it
		Alias,        // a twin — a second reading of one table (ibAliasColumn)
		Stitch,       // made while a result is assembled: a repeated select entry, a computed projection
		Aggregate,    // the slot a fold's figure lands in when it cannot roll into its input's column
		GroupKey,     // a computed GROUP BY key — grouped by an expression, so it has no column
		Subquery,     // what a nested query publishes for its own folds and walks
	};
	// ⭐⭐ THE VALUE GOES IN AS IT IS — ordinary or already synthetic — AND A NEW ONE COMES OUT.
	//
	// The kind is the LOW digit and the value the high ones, so composing is a mixed radix rather
	// than a bit field. That is what lets a synthetic id be fed back in: a field layout would mask the
	// inner kind away and hand back the same number, while here the value simply grows
	// (Max, 2026-09-06: *"you take the value, feed it into the kind as it is — synthetic or ordinary —
	// and get a new one"*).
	//
	// Which is what a THIRD reading of one table needs: it wraps the second, the second wraps the
	// first, and each link is a new number without anybody counting anything.
	//
	// ⚠ So `id + 1` is NOT "the next value of this kind" — it is another KIND. Whoever numbers a run
	// of these composes each one (`SyntheticId(kind, i++)`), never increments a composed id.
	static constexpr unsigned kSyntheticKinds = 8;
	static constexpr ibMetaID SyntheticId(SyntheticKind kind, ibMetaID value) {
		return -((value < 0 ? -value : value) * static_cast<ibMetaID>(kSyntheticKinds)
		         + static_cast<ibMetaID>(kind));
	}
	static bool IsSyntheticId(ibMetaID id) { return id < 0; }

	// (The column's value-field split — a composite / variant / reference column expands to several
	// physical fields — is NOT a column method: it is the tier free function ColumnValueFields(col)
	// over DescribeColumnLayout (columnLayout.h), metadata-free, asked only by the DB provider. The
	// column stays a pure descriptor; value materialization / binding stays on the queryable.
	// docs/query-language-arc.md §22.4b)

	// (No per-column primary-key flag: a source's uniqueness key is owned by the QUERYABLE —
	// ibBackendQueryable::GetPrimaryKeyColumns is the ONE authority for both the write UPSERT
	// match AND the auto-join self-reference key (a record's data-reference); the uuid read keyset
	// is GetPrimaryKeyColumns. The column stays a pure typed descriptor. docs/query-language-arc.md §22.1)

	// Is this a RAW (direct) physical column — addressed by its concrete field name with NO
	// metadata translation (no TYPE/_N/_S/_RRRef expansion, no SetValueAttribute decomposition)?
	// A metadata attribute returns false (it IS translated); ibBackendColumnRawDB returns true. The
	// provider uses this to decide: bind the value straight (raw) vs decompose it (attribute).
	// (docs/query-language-arc.md §22)
	// ⭐⭐ WHAT KIND OF COLUMN THIS IS — one question, one answer.
	//
	// It used to be a bit (`IsRawColumn`), and a second bit was about to be added beside it. Two bits
	// admit a combination that means nothing (raw AND assembled), and every reader would have had to
	// know which to ask first. A column is one of these and cannot be two:
	//
	//   Composite — a metadata column: `_TYPE` plus a field per admissible type, spread and reassembled
	//               by the codec. The ordinary case, and the default;
	//   Raw       — ONE physical field carrying its own declared type, no tag (a row key, a tabular
	//               section's owner reference); it reads and binds itself;
	//   Computed  — an output that exists only in a result: an aggregate, an expression, a dot-walk
	//               leaf minted under an alias. Nothing stores it and nothing declares it — it is read
	//               back BY NAME from the cursor;
	//   Synthetic — NO field of its own: it is MADE of other columns, and THEY write its fields. A
	//               document's MOMENT is the date plus the reference. It is never projected and never
	//               published — writing those fields again puts one alias in a select list twice
	//               (`-104 … specified multiple times`), and declaring it names a field nothing wrote
	//               (`-206 Column unknown`); we hit both in one day. It still HAS a layout, because a
	//               sort and a comparison are built from it, and it reads itself out of those fields.
	enum class Kind { Composite, Raw, Computed, Synthetic };
	virtual Kind GetColumnKind() const { return Kind::Composite; }

	bool IsRawColumn()       const { return GetColumnKind() == Kind::Raw; }
	bool IsSyntheticColumn() const { return GetColumnKind() == Kind::Synthetic; }

	// ⭐⭐ HOW THIS COLUMN LIES IN THE DATABASE — the physical fields it occupies, in bind order.
	//
	// The default derives them from (physical name, type): `<name>_TYPE`, then a slot per primitive
	// the type admits, then the reference pair. That is where every field spelling comes from — the
	// DDL, the projection, the sort keys, the keyset anchor and the codec all read this one answer.
	//
	// A column whose data lies in OTHER columns overrides it: a document's MOMENT is the date
	// followed by the reference, so it answers with their layouts, one after the other, and sorting
	// by it is sorting by the date and then by the identifier — through the very machinery that
	// already sorts a reference by its own two fields. Nothing new is taught to anybody.
	virtual std::vector<ibColumnSlot> DescribeLayout() const;

	// ⭐⭐ HOW THIS COLUMN IS READ OUT OF A RESULT — asked OF THE COLUMN, answered by the codec.
	//
	// The default is what every ordinary column has always done: the value codec reads the `_TYPE`
	// tag and takes the field for it (ibColumnCodec::ReadValue, body in columnLayout.cpp). Nothing
	// about that changes, and there is one such branch, not one per caller.
	//
	// What changes is WHO IS ASKED. A caller no longer names the codec; it asks the column, and a
	// column that is not stored the ordinary way overrides this and reads itself — a synthetic column
	// has no tag of its own and assembles its value from the columns it is made of. The default stays
	// the default, so nothing that reads a stored column pays for the possibility.
	virtual bool ReadValue(const wxString& fieldName, const class ibMetaData* metaData,
	                       class ibValue& retValue, class ibQueryResult& result, bool createData = false) const;

	// …AND THE WRITE, THE SAME WAY. The pair belongs together: a value bound by the default lands in
	// exactly the fields the default read takes it back out of, so a column that changes one and not
	// the other would be storing what it cannot read. The default is the codec (BindWriteValue); a
	// column with nothing of its own to store overrides it and binds nothing — a moment is READ out of
	// the date and the reference and WRITTEN by writing those, which is what already happens.
	virtual void BindValue(class ibQueryStatement& statement, const class ibMetaData* metaData,
	                       const class ibValue& value, int& position) const;

	// ⭐⭐ THE RAW DB COLUMN THIS ONE *IS*, or null — asked instead of casting on IsRawColumn().
	//
	// `GetColumnKind() == Raw` says what a column is LIKE; five places read it as "so it is an
	// ibBackendColumnRawDB" and static_cast on the strength of that. The two are not the same
	// statement, and the day they parted was the day a column began STANDING FOR another one: an
	// aliased reading of a table forwards every question about the data to the column it aliases —
	// including this one — while being a different class entirely. The cast would then read a
	// RawType out of an object that has none, silently.
	//
	// So the question is asked OF THE COLUMN and answered by whoever can: the raw column with itself,
	// a forwarding one with what it stands for, everybody else with null. A cast cannot be wrong when
	// there is no cast.
	//
	// ⚠ LAST IN THE CLASS ON PURPOSE — a virtual inserted among the others renumbers every slot after
	// it. New optional virtuals go here.
	virtual const class ibBackendColumnRawDB* AsRawColumn() const { return nullptr; }
};

// ==========================================================================
// ibBackendColumnRawDB — a DIRECT physical column: a concrete db field named AS-IS, no metadata
// behind it and no translation. Lets the door address a real column straight (the row-key
// uuid; a balance's computed qty_balance) through the SAME ibBackendQueryColumn interface as
// a metadata attribute. The PHYSICAL TYPE is carried by the column itself (its RawType), so
// the provider binds it with no value-type guessing — use the typed factories below.
// Slicing-safe — all state is on this base, so the door takes one by ref and owns a copy.
// The uniqueness key is the queryable's concern, not the column's. (docs §22)
// ==========================================================================
class BACKEND_API ibBackendColumnRawDB : public ibBackendQueryColumn
{
public:
	// How the provider binds the raw value — fixed by the concrete factory, not the value.
	// Reference is the FIXED reference-key binary (_RRRef = pure [guid 16], indexable; type is the _RTRef column); Blob is a
	// VARIABLE-length blob (a register's rowData). Guid / Blob / Reference are the schema-scaffold types
	// so the structure builder can create those columns through this same column interface, no ibDdlColumn.
	enum class RawType { String, Number, Reference, Date, Boolean, Guid, Blob };

	// `modelId` gives the column an IDENTITY. Zero (the default) means "scaffold": a field created
	// with its table and never migrated — a row key, a blob of packed data. A non-zero id makes it
	// a column the schema differ can track, so it can be ADDED to or DROPPED from an existing
	// table. A derived table's accumulating columns need that: they appear and disappear as the
	// metaobject gains and loses resources, and without an identity the differ cannot see either.
	ibBackendColumnRawDB(const wxString& field, RawType type, ibMetaID modelId = 0)
		: m_field(field), m_type(type), m_modelId(modelId) {}

	// ⭐ TWO NAMES WHERE THEY DIFFER, one where they do not. A scaffold column is its own field and
	// says so under one name. A column PUBLISHED to a query is a different case: `Ref` is what an
	// author writes, `uuid` is what the table keeps, and handing the storage spelling out as the
	// name puts the physical schema into the field tree — the exact mistake the register's view
	// columns were fixed for.
	wxString              GetName()         const override { return m_name.IsEmpty() ? m_field : m_name; }
	wxString              GetPhysicalName() const override { return m_field; }
	ibTypeDescription&    GetTypeDesc()     const override { return m_typeDesc; }   // interface returns a non-const ref
	ibMetaID              GetColumnId()      const override { return m_modelId; }   // 0 = scaffold, never diffed
	Kind                  GetColumnKind()   const override { return Kind::Raw; }
	// …and it IS one, which is the whole of the answer nobody else can give.
	const ibBackendColumnRawDB* AsRawColumn() const override { return this; }

	RawType               GetRawType()      const { return m_type; }   // the provider's bind selector
	// The declared width: a string's length, a number's PRECISION. 0 = "no reason to say", and the
	// layout tier then answers with its default.
	unsigned int          GetRawLength()    const { return m_length; }
	unsigned int          GetRawScale()     const { return m_scale; }

	// Convenience makers — read cleaner than naming the enum at the call site, with no extra
	// type to maintain (these are static factories, not subclasses). One per RawType.
	// ⭐ A WIDTH, WHERE THE COLUMN HAS A REASON TO NAME ONE. Zero = the default (255), which is right
	// for a scaffold column nobody indexes. It is NOT right for a column an INDEX stands on: Firebird
	// sizes an index key from the DECLARED length times the charset's bytes-per-character, so a
	// VARCHAR(255) in UTF8 is 1020 bytes and passes the key-size ceiling (≈ page_size / 4) on its own —
	// "key size exceeds implementation restriction", with nothing wrong but the declaration.
	static ibBackendColumnRawDB String   (const wxString& field, ibMetaID id = 0, unsigned int length = 0) {
		ibBackendColumnRawDB col(field, RawType::String, id);
		col.m_length = length;
		return col;
	}
	// The same for a number: a totals column has to carry the RESOURCE's own precision and scale, or a
	// figure with kopecks is stored in a column that has none and the fraction is lost on the way in.
	static ibBackendColumnRawDB Number   (const wxString& field, ibMetaID id = 0,
	                               unsigned int precision = 0, unsigned int scale = 0) {
		ibBackendColumnRawDB col(field, RawType::Number, id);
		col.m_length = precision;
		col.m_scale  = scale;
		return col;
	}
	// ⭐ A REFERENCE STORED AS ONE FIELD, WITH A CONSTANT TARGET.
	//
	// The ordinary reference column is a PAIR — `_RTRef` (which type) beside `_RRRef` (which row) —
	// because the value may point at several kinds. This one cannot: a tabular section belongs to
	// exactly one owner, so the type is known from the metadata and storing it per row would be a
	// column repeating one constant a million times. The target rides on the column instead, and the
	// codec reads a real reference out of the sixteen bytes it finds.
	static ibBackendColumnRawDB Reference(const wxString& field, const ibClassID& target = 0,
	                               const wxString& name = wxEmptyString, ibMetaID modelId = 0)
	{
		ibBackendColumnRawDB col(field, RawType::Reference, modelId);
		col.m_name = name;
		if (target != 0)
			col.m_typeDesc.SetDefaultMetaType(target);
		return col;
	}

	// What this column points at — 0 when it is not a single-target reference.
	ibClassID GetRawTarget() const { const auto& list = m_typeDesc.GetClsidList(); return list.empty() ? 0 : list.front(); }

	// ⭐⭐ AND IT READS AND WRITES ITSELF — one field, its own declared type, no `_TYPE` tag to consult.
	//
	// This is what the column always did; what changes is where it is written down. The rule used to
	// live as `if (col->IsRawColumn())` at the head of the codec's bind and again at every reader that
	// projects one — the same switch over RawType in three places, each of which had to remember that
	// a raw guid is sixteen bytes and not thirty-six characters. Now the column answers, and a caller
	// asks nobody what kind of column it holds. (Bodies in dbTableProvider.cpp, beside the reference
	// assembly a single-target raw reference needs.)
	bool ReadValue(const wxString& fieldName, const class ibMetaData* metaData,
	               class ibValue& retValue, class ibQueryResult& result, bool createData = false) const override;
	void BindValue(class ibQueryStatement& statement, const class ibMetaData* metaData,
	               const class ibValue& value, int& position) const override;
	static ibBackendColumnRawDB Date     (const wxString& field, ibMetaID id = 0) { return ibBackendColumnRawDB(field, RawType::Date, id);      }
	static ibBackendColumnRawDB Boolean  (const wxString& field) { return ibBackendColumnRawDB(field, RawType::Boolean);   }
	static ibBackendColumnRawDB Guid     (const wxString& field) { return ibBackendColumnRawDB(field, RawType::Guid);      }
	static ibBackendColumnRawDB Blob     (const wxString& field) { return ibBackendColumnRawDB(field, RawType::Blob);      }

private:
	wxString                  m_field;
	wxString                  m_name;    // what a QUERY writes; empty = the field is its own name
	RawType                   m_type;
	unsigned int              m_length = 0;   // string length / number precision; 0 = the tier's default
	unsigned int              m_scale  = 0;   // number scale — the fraction a figure is stored with
	ibMetaID                  m_modelId;   // 0 = scaffold: created with its table, never migrated
	mutable ibTypeDescription m_typeDesc;   // mutable: GetTypeDesc() is const but returns a non-const ref
};

#endif
