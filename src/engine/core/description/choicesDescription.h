#ifndef __CHOICES_DESCRIPTION_H__
#define __CHOICES_DESCRIPTION_H__

#include "core/core.h"                    // CORE_API, ibString
#include "core/serialize/dataBuilder.h"   // ibDataNode — what an item's value is kept as

#include <vector>

// ⭐ WHAT A SWITCH OFFERS — the values set in advance in the studio, each with its presentation, as many as are
// listed (Max, 2026-11-23: "a property where you set the values and the presentations"). The type of the field's source
// does not make them: the author does, in a window of its own (docs/private/form-field.md § 4.6).
//
// ⭐ AN ITEM'S VALUE IS A NODE — the one the application's door writes it into (ibMetaData::Serialize), so an
// enumeration's member is kept as itself, and the core, which knows no application, holds an id and a node and never a
// value (core/description: the schemas both sides share). The PRESENTATION is the translated string in its stored form,
// which the side that shows it reads in the session's language.
struct ibChoiceDescription {

	ibDataNode m_value;
	ibString   m_presentation;

	bool operator==(const ibChoiceDescription& other) const {
		return m_value == other.m_value && m_presentation == other.m_presentation;
	}
};

// …in the order they are shown. No items — the switch shows nothing.
struct ibChoicesDescription {

	std::vector<ibChoiceDescription> m_rows;

	bool IsOk() const { return !m_rows.empty(); }
	void Clear() { m_rows.clear(); }

	bool operator==(const ibChoicesDescription& other) const { return m_rows == other.m_rows; }
};

// node form: a Child { rows: [{ value: <node>, presentation: <string> }] } — the shape of the choice parameters'
// (choiceLinkDescription.h).
class CORE_API ibChoicesDescriptionMemory {
public:
	static bool ReadNode(const ibDataValue& value, ibChoicesDescription& choicesDesc);
	static bool WriteNode(ibDataValue& value, const ibChoicesDescription& choicesDesc);
};

#endif
