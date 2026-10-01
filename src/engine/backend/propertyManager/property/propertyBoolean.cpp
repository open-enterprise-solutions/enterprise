#include "propertyBoolean.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — readable Bool node value


//base property for "bool"
bool ibPropertyBoolean::SetDataValue(const ibValue& varPropVal)
{
	SetValue(varPropVal.GetBoolean());
	return true;
}

bool ibPropertyBoolean::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibPropertyBoolean::GetValueAsBoolean();
	return true;
}

bool ibPropertyBoolean::ReadNodeValue(const ibDataValue& value)
{
	// An EMPTY value never arrives here — ibBackendProperty::SetNodeValue gates it, so a boolean
	// missing from the file keeps the default it was declared with instead of being assigned
	// false. (That conflation is what unset `Correspondence` / `SplitTotals` on load; the reason
	// is written once, at the gate.)

	// ⭐ A BOOLEAN SPELLED OUT IS STILL A BOOLEAN. Everything that reaches a property from outside
	// the binary format arrives as TEXT — a tool argument, a JSON view — and `true` is the word a
	// caller writes for it.
	//
	// 🛑 AND A WORD THAT IS NOT ONE IS A REFUSAL, NOT AN EXCEPTION. `AsBool()` on a String RAISES,
	// so `metadata_set QuickChoice = "true"` came back as `[platform] ibDataValue: wrong value kind
	// (expected 1, got 4)` — an internal shape number thrown through a door whose caller had a
	// sentence ready to say instead ("it holds a Boolean — send one of that shape"). The refusal
	// path existed; the exception jumped over it (found 2026-09-01, sweeping every tool).
	if (value.Kind() == ibDataKind::String) {

		const wxString word = value.AsString();

		if (word.IsSameAs(wxT("true"), false) || word.IsSameAs(wxT("1"))) {
			ibPropertyBoolean::SetValue(true);
			return true;
		}

		if (word.IsSameAs(wxT("false"), false) || word.IsSameAs(wxT("0"))) {
			ibPropertyBoolean::SetValue(false);
			return true;
		}

		return false;
	}

	if (value.Kind() != ibDataKind::Bool)
		return false;

	ibPropertyBoolean::SetValue(value.AsBool());
	return true;
}

bool ibPropertyBoolean::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::Bool(ibPropertyBoolean::GetValueAsBoolean());
	return true;
}