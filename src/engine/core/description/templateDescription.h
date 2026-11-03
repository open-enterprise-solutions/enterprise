#ifndef __TEMPLATE_DESCRIPTION_H__
#define __TEMPLATE_DESCRIPTION_H__

// ⭐⭐ WHAT A TEMPLATE HOLDS — its kind, and the live schema of that kind (Max, 2026-11-03). The kind is the
// only thing this description has of its own; the schema is a spreadsheet's or a composition's, the very
// one their editors and their runtime use, and it is stored by their own *Memory. A third kind (binary
// data) is a third schema beside these two, not a third field on the template.
//
// ⚠ A SCHEMA IS ASKED FOR BY ITS KIND, and asking for the other kind's is an error, not an empty answer: a
// composer read as a sheet is a bug in the caller, and an empty sheet would hide it.
#include "core/core.h"
#include "core/description/spreadsheetDescription.h"
#include "core/description/compositionDescription.h"

#include <variant>

// Stored by number — a file written with these must read back the same.
enum class ibTemplateType {
	None        = 0,   // not chosen yet — a template the designer has just made asks for it
	Spreadsheet = 1,   // cells, named areas and parameters
	Composer    = 2,   // a composition
};

class CORE_API ibTemplateDescription {
public:

	ibTemplateDescription() = default;
	explicit ibTemplateDescription(ibTemplateType type);   // an empty schema of the kind
	ibTemplateDescription(const ibSpreadsheetDescription& spreadsheetDesc) : m_data(spreadsheetDesc) {}
	ibTemplateDescription(const ibCompositionDescription& compositionDesc) : m_data(compositionDesc) {}

	ibTemplateType GetType() const;

	// The schema of its kind, by reference — edited in place; another kind raises (ibCoreException).
	ibSpreadsheetDescription& GetSpreadsheetDesc();
	const ibSpreadsheetDescription& GetSpreadsheetDesc() const;
	ibCompositionDescription& GetCompositionDesc();
	const ibCompositionDescription& GetCompositionDesc() const;

	bool operator==(const ibTemplateDescription& rhs) const { return m_data == rhs.m_data; }

private:

	std::variant<std::monostate, ibSpreadsheetDescription, ibCompositionDescription> m_data;
};

// ⭐ ONE NODE: THE SCHEMA'S OWN, WITH THE KIND ADDED — the way a sheet adds a cell's place to the cell's node.
// The schema is written and read by its *Memory; this writes nothing else. A node with no kind is a template
// from before the kinds, a spreadsheet. No kind chosen — nothing is written.
class CORE_API ibTemplateDescriptionMemory {
public:
	static bool ReadNode(const class ibDataValue& value, ibTemplateDescription& templateDesc);
	static bool WriteNode(class ibDataValue& value, const ibTemplateDescription& templateDesc);
};

#endif
