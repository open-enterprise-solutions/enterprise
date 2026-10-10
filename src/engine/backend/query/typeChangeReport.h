#ifndef __TYPE_CHANGE_REPORT_H__
#define __TYPE_CHANGE_REPORT_H__

// What stored values become under a new attribute type, judged before any DDL.
//
// A non-empty value that becomes empty is lost. A parse that does not consume
// the whole text is lost. Any narrowing (scale, precision, length, date parts)
// is lost. An empty value that stays empty is not. The same function is what
// a later conversion writes, so the report and the write cannot disagree.

#include "backend/backend.h"
#include "backend/compiler/value.h"
#include "backend/typeDescription.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <wx/buffer.h>

class ibDatabaseLayer;
class ibMetaData;
class ibRestructureInfo;
class ibSchemaSnapshot;

enum class ibTypeChangeState {
	Absent,   // nothing was stored, or an empty that stays empty
	Kept,     // the new type holds the whole value
	Lost      // empty result, a partial parse, or a narrowing
};

struct ibTypeChangeFate {
	ibTypeChangeState state = ibTypeChangeState::Absent;
	// True when the value cannot be represented at all: the cell is cleared,
	// not stored as a coerced leftover (a partial number, False, an empty date).
	bool    wipe  = false;
	// True when the value is representable only by dropping a part (scale,
	// precision, length, a date part).
	bool    narrowed = false;
	ibValue value;
};

// `oldMeta` is the configuration the stored value belongs to. `newMeta` is the
// configuration the new type belongs to. Neither is optional and neither is
// replaced with the process's active configuration: a null means "no
// configuration", which is enough for a primitive and not enough for a reference.
BACKEND_API ibTypeChangeFate ibAssessTypeChange(const ibValue& stored,
                                                 const ibTypeDescription& next,
                                                 const ibMetaData* oldMeta,
                                                 const ibMetaData* newMeta);

struct ibTypeChangeSample {
	wxString ref;
	wxString oldValue;
};

// One attribute. Counters are 64-bit: a register can hold more than a 32-bit count.
struct ibAttributeTypeChange {
	wxString object;
	wxString attribute;
	std::int64_t rows     = 0;
	std::int64_t kept     = 0;
	std::int64_t lost     = 0;
	std::int64_t narrowed = 0;
	std::vector<ibTypeChangeSample> samples;
};

struct BACKEND_API ibTypeChangeReport {
	std::vector<ibAttributeTypeChange> lines;
	std::int64_t rowsRead = 0;

	bool HasDataLoss() const;
	// Per-attribute lines, the first samples, and one totals line. Wrapped for translation.
	wxString Text() const;
};

// A loss is a refusal. Nothing has been written yet.
BACKEND_API bool ibTypeChangeBlocksApply(const ibTypeChangeReport& report, wxString& refusal);

// Asked only when the report has a loss. Empty, or an answer of false, stops
// the apply. True converts: a value that fits is written as the new type, and
// a value that does not is left empty.
using ibTypeChangeAccept = std::function<bool(const ibTypeChangeReport&)>;

// How one physical field of a converted row is bound.
enum class ibTypeChangeBind {
	Null,
	Int,
	Number,
	String,
	Date,
	Bool,
	Blob
};

struct ibTypeChangeCell {
	wxString         field;
	ibTypeChangeBind bind = ibTypeChangeBind::Null;
	int              integer = 0;
	ibNumber         number;
	wxString         text;
	ibDateTime       date;
	bool             flag = false;
	wxMemoryBuffer   blob;
};

// One row of one attribute. `key` is every slot of every column in
// GetPrimaryKeyColumns, except the discriminator. `before` is written while
// the old column still exists, so a narrowing fits the ALTER that follows.
struct ibTypeChangeWrite {
	wxString table;
	wxString field;
	bool     before = false;
	std::vector<ibTypeChangeCell> key;
	std::vector<ibTypeChangeCell> set;
};

// The writes, and the tables that need one but have no complete key. An
// update that cannot name its row is not stored here.
struct BACKEND_API ibTypeChangePlan {
	std::vector<ibTypeChangeWrite> writes;
	std::vector<wxString>          incomplete;

	wxString KeyRefusal() const;
};

// Every stored value whose attribute type differs. One SELECT per table, of
// every changed column of that table. A derived table is not read: its rows
// are recomputed. The read runs only after exclusive mode is held.
//
// A cell that cannot be read becomes a refusal naming the table and the row,
// rather than an exception escaping the apply. `journal`, when set, receives
// one line per table with the number of rows read. `plan`, when set, receives
// the writes that make the stored values match the new type. Both are filled
// from this one read, inside the exclusive window.
BACKEND_API ibTypeChangeReport ibReadTypeChangeReport(
	const ibSchemaSnapshot* baseline,
	const ibSchemaSnapshot& target,
	const std::shared_ptr<ibDatabaseLayer>& layer,
	ibRestructureInfo* journal,
	ibTypeChangePlan* plan = nullptr);

// UPDATE each planned row. One prepared statement per column shape, rebound
// per row. A write that does not change exactly one row is a refusal: the key
// did not name that row.
BACKEND_API void ibApplyTypeChangeWrites(
	const std::shared_ptr<ibDatabaseLayer>& layer,
	const std::vector<ibTypeChangeWrite>& writes);

#endif // __TYPE_CHANGE_REPORT_H__
