#ifndef __TYPE_SELECTOR_DIALOG_H__
#define __TYPE_SELECTOR_DIALOG_H__

#include "backend/typeDescription.h"
#include "backend/backend_type.h"   // ibSelectorDataType — which shape the picker renders

// THE TYPE PICKER — one dialog, several callers.
//
// It reads a type and returns the edited one. That is the whole of it: it does not know who asked,
// what a characteristic is, or which chart declared anything. Two inputs, one output.
//
//   allowed  — the types this caller permits. NOT a rule the dialog evaluates: a plain list, already
//              computed by whoever knows the meaning. The metadata editor passes everything the
//              configuration allows; a characteristic passes its chart's composition. A third caller
//              tomorrow passes its own, and this file does not change.
//              A METATYPE in that list is not a leaf — the picker expands it into a category holding
//              this configuration's objects of that kind ("CatalogRef" → every catalog). Which of
//              the two a given id is, the picker works out from the id itself; the caller does not
//              sort them, it just says what is permitted.
//   inOut    — the description as it stands on the way in, the edited one on the way out. Passing
//              the current value is what makes this an EDITOR rather than a chooser: opening it on a
//              characteristic that already names "Contractor" must show it ticked, and Cancel must
//              mean "leave as it was", which is inexpressible without it.
//   single   — whether one type may be chosen or several. A type is legitimately composite, so the
//              general answer is several; a BINDING names exactly one. The caller knows which it is.
//
// Returns true when the user accepted (inOut then holds the new description), false on cancel — and
// on cancel inOut is untouched.
// `kind` — WHICH SHAPE the picker renders: the ordinary value vocabulary, references only, a table
// slot, and so on. It decides what the dialog OFFERS in general; `filter`, when non-empty, narrows
// that to a specific set (a characteristic's chart composition). Empty filter = no narrowing.
bool ibShowTypeSelector(class wxWindow* parent, ibSelectorDataType kind,
	const std::vector<ibClassID>& filter, ibTypeDescription& inOut, const class ibMetaData* metaData,
	bool allowEdit = true, bool single = false);

#endif // !__TYPE_SELECTOR_DIALOG_H__
