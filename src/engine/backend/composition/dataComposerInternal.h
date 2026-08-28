#ifndef __DATA_COMPOSER_INTERNAL_H__
#define __DATA_COMPOSER_INTERNAL_H__

////////////////////////////////////////////////////////////////////////////
//	The composer's OWN vocabulary — what its aspect files say to each other.
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ WHY THIS FILE EXISTS. `dataComposer.h` is included by the whole backend and by the frontend,
// so anything stated there is a promise to every tier and a rebuild for every tier. These functions
// are promises to NOBODY: they are how the composer's own files — the text, the walk, what is shown
// — speak to one another (Max, 2026-08-28: *"why is it declared at all, if it can live in the
// module?"*).
//
// The same shape the query constructor already has (queryConstructorInternal.h): a public header for
// other tiers, an internal one for the aspect files, and nothing of the second reaches the first.

#include "dataComposer.h"

// ⭐ DOES THIS COLUMN ANSWER TO THIS PATH — and the two are NOT spelled the same. A path is what a
// person picked (`Ref.Date`, `Sales.Qty`); the OUTPUT NAME the engine gives it drops a leading
// source qualifier and CONCATENATES the walk (`ibQueryProposedName`): `RefDate`, `Qty`.
bool ibComposerColumnAnswersTo(const ibQueryLowering::OutputColumn& oc, const wxString& path);

// ONE TABLE, RESOLVED AGAINST WHAT IS IN FORCE ABOVE IT — the whole of the inheritance rule: an
// empty table inherits, an `Auto` row is WHERE the inherited set lands, and a table without one
// states this node's composition whole.
void ibComposerResolveSelected(std::vector<wxString>& into,
                               const std::vector<ibSelectedFieldDescription>& rows,
                               const std::vector<wxString>& inherited);

// WHAT IS IN FORCE UNDER A NODE, given what was in force above it. The chain is CARRIED by whoever
// walks the tree, never worked out by climbing back up.
std::vector<wxString> ibComposerSelectedUnder(const std::vector<wxString>& above,
                                              const ibDataComposer::GroupNode& level);

// (IS THIS LEVEL THE RECORDS — asked of the LEVEL: `ibLevelDescription::IsDetailRecords()`. It was
//  a function here for an hour, which made it the composer's opinion about a level rather than a
//  fact about one, and the settings window — which cannot see this header — went on holding its own.)

// WHAT A REPORT READS WHEN NOBODY HAS CHOSEN A FIELD — the fields it groups BY, and nothing else.
std::vector<wxString> ibComposerGroupingFieldsOf(const ibDataComposer::Output& output);

#endif
