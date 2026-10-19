#ifndef __QUERY_AGGREGATES_H__
#define __QUERY_AGGREGATES_H__

////////////////////////////////////////////////////////////////////////////
//	Description : WHICH AGGREGATES A TYPE ADMITS - the one answer every side reads
////////////////////////////////////////////////////////////////////////////
//
// A question of the TYPE and of the keyword table, nothing else: no source, no configuration, no
// lowering. So it lives in the core, and the lowering that refuses with it, the desktop's windows and
// the client's, which offer with it, read one answer — a window cannot offer what the query refuses.

#include "core/core.h"                          // CORE_API
#include "core/description/typeDescription.h"   // ibTypeDescription — what the field holds
#include "core/query/queryKeywords.h"           // ibQueryKeyword — the answer's words

#include <wx/arrstr.h>   // wxbase exports std::vector<wxString> (wxArrayString's base) — seen here, a unit imports it
#include <wx/string.h>

#include <vector>

// WHICH AGGREGATES CAN BE TAKEN OVER A VALUE OF THIS TYPE — the ONE door, read two ways.
//
//   SUM / AVG — a NUMBER. There is no sum of dates and no average of references.
//   MIN / MAX — anything ORDERED: number, date, string. A reference has no order of its own
//               (it keys by guid), so folding one would rank rows by an internal identity.
//   COUNT     — anything at all. Counting asks nothing of the type.
//
// ⚠ CheckNames reads it as a REFUSAL ("SUM cannot be taken over 'Description'"); a host reads it
// as WHAT TO OFFER — the constructor's function cell lists exactly these, so a person is never
// shown a choice their own engine would then reject. Written twice, the two would drift, and the
// drift is a dropdown that offers something the query cannot do.
//
// A COMPOSITE type (several clsids) yields the full set: which type a row holds is a fact about
// the ROW, and narrowing on "it might be a string" would be this answer inventing one.
CORE_API std::vector<ibQueryKeyword> ibQueryAggregatesFor(const ibTypeDescription& type);

// ⭐ THE READY CALLS OVER A FIELD, as TEXT — what a window offers in a cell.
//
// ibQueryAggregatesFor answers in KEYWORDS, and a window then wraps each one around the field itself.
// Two windows did that wrapping (the constructor's Totals tab and the composition's Resources),
// and the moment an offer stopped being one plain keyword the two had to be taught separately —
// which is how `COUNT(DISTINCT x)` ended up in the LANGUAGE (the parser reads it) and in neither
// list. DISTINCT is not an aggregate of its own; it is a modifier on one, so it cannot come back
// through the keyword list at all.
//
// So the ENGINE composes the offers: it knows what the type admits AND what forms a call may take.
CORE_API std::vector<wxString> ibQueryAggregateCallsFor(const ibTypeDescription& type, const wxString& field);

#endif // __QUERY_AGGREGATES_H__
