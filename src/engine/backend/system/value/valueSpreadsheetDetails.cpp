////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : details of a composed cell — its value and what it stood under
////////////////////////////////////////////////////////////////////////////

#include "valueSpreadsheetDetails.h"

#include <algorithm>   // find_if — the context is a short list, searched by path

//////////////////////////////////////////////////////////////////////

void ibValueSpreadsheetDetails::LinkTo(const ibValue& parent)
{
	if (parent.IsEmpty())
		return;   // nothing above this one — a top level, and that is an ordinary case
	m_parents.push_back(parent);
}

const ibValue& ibValueSpreadsheetDetails::Unwrap(const ibValue& value)
{
	const ibValueSpreadsheetDetails* details = From(value);
	return details != nullptr ? details->m_value : value;
}

ibValue* ibValueSpreadsheetDetails::FindOpenable()
{
	// ⚠ A REFERENCE IS WHAT HAS A CARD, and it is the whole test: an OBJECT held in a value is a
	// reference too (ibValue takes ownership as TYPE_REFFER), while a number, a string and a date
	// are not and `ShowValue` on them is a no-op by construction (value.cpp).
	if (m_value.IsReference())
		return &m_value;

	// DEPTH FIRST, IN LINK ORDER — for a cross-table cell that is the row heading before the column
	// one, which is the same order its context reads in.
	for (ibValue& parent : m_parents) {
		if (ibValueSpreadsheetDetails* link = From(parent)) {
			if (ibValue* openable = link->FindOpenable())
				return openable;
		}
	}

	return nullptr;
}

void ibValueSpreadsheetDetails::ShowValue()
{
	if (ibValue* openable = FindOpenable())
		openable->ShowValue();
}

void ibValueSpreadsheetDetails::CollectContext(std::vector<ibSpreadsheetDetailsField>& into) const
{
	// THIS ONE FIRST, so the nearer link wins where the two branches of a cross-table cell name one
	// path — and only if it is a DIMENSION: a figure is what was measured, never what it was measured
	// under.
	if (m_role == ibQueryLowering::ibColumnRole::Dimension && !m_path.IsEmpty()) {
		const auto known = std::find_if(into.begin(), into.end(),
			[this](const ibSpreadsheetDetailsField& field) { return field.m_path == m_path; });
		if (known == into.end())
			into.push_back({ m_path, m_value });
	}

	// …AND THEN UP, branch by branch. Depth first: one axis is walked to the root before the other
	// starts, so a cross-table cell reads as "this row, in this column" rather than as the two
	// interleaved.
	for (const ibValue& parent : m_parents) {
		if (const ibValueSpreadsheetDetails* link = From(parent))
			link->CollectContext(into);
	}
}

//////////////////////////////////////////////////////////////////////

enum
{
	eField,
	eKind,
	eValue
};

void ibValueSpreadsheetDetails_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	// ⚠ THREE NAMES, AND NO MORE. What a script can usefully ask of a details parameter is which
	// field it stands for, what kind of field that is, and what value it holds; the LINKS are walked
	// by the detail itself, in C++, and publishing them would be a second surface to keep true.
	helper.AppendProp(wxT("Field"));
	helper.AppendProp(wxT("Kind"));
	helper.AppendProp(wxT("Value"));
}

bool ibValueSpreadsheetDetails::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eField:
		pvarPropVal = m_path;
		return true;
	case eKind:
		pvarPropVal = ibValue::CreateEnumObject<ibValueEnumSpreadsheetFieldKind>(m_role);
		return true;
	case eValue:
		pvarPropVal = m_value;
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// ⭐ A SYSTEM TYPE — the composer builds these while it writes the sheet, and nothing constructs one
// from a script (Max, 2026-08-28: "it is a full runtime object, a system one, because it is created
// by the system"). So it is registered to be RECOGNISED, not to be created.
SYSTEM_TYPE_REGISTER(ibValueSpreadsheetDetails, "SpreadsheetDetails");

// …and the face of the column role — a TYPE the runtime can name, so "what field is this" is
// answered with a value that can be compared, not with a number to look up in a table somewhere.
ENUM_TYPE_REGISTER(ibValueEnumSpreadsheetFieldKind, "SpreadsheetFieldKind");
