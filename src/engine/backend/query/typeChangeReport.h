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
#include <memory>
#include <vector>

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

// Every stored value whose attribute type differs. One SELECT per table, of
// every changed column of that table. A derived table is not read: its rows
// are recomputed. The read runs only after exclusive mode is held.
//
// A cell that cannot be read becomes a refusal naming the table and the row,
// rather than an exception escaping the apply. `journal`, when set, receives
// one line per table with the number of rows read.
BACKEND_API ibTypeChangeReport ibReadTypeChangeReport(
	const ibSchemaSnapshot* baseline,
	const ibSchemaSnapshot& target,
	const std::shared_ptr<ibDatabaseLayer>& layer,
	ibRestructureInfo* journal);

// A loss is a refusal. Nothing has been written yet.
BACKEND_API bool ibTypeChangeBlocksApply(const ibTypeChangeReport& report, wxString& refusal);

#endif // __TYPE_CHANGE_REPORT_H__
