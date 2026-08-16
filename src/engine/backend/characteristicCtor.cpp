#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"

#include "characteristicCtor.h"

#include <algorithm>

// Phase 3: GetClassInfo() override removed (Category B self-identifies via
// GetClassType()). Only CreateObject() remains.

// The chart overrides the type factory's `GetTypeDesc()` with its own CONTOUR (the composition
// `GetTypesOfCharacteristics` names), so the inherited `CreateValueRef()` already builds a value out
// of what a characteristic may be: one type in the contour gives that very type, several give an
// undefined. Nothing to add here — a branch of our own would be the same rule spelled twice.
ibValue* ibCtorMetaValueTypeCharacteristic::CreateObject() const
{
	return m_metaObject->CreateValueRef();
}

// ⭐ WHAT THIS TYPE ADMITS — the CONTOUR, i.e. the chart's own composition. A characteristic's value is
// never "an instance of Characteristic.<chart>"; it is one of the types the chart declares, so the
// inherited `clsid == GetClassType()` rejected every value that could legitimately be stored.
//
// Class ids in, yes/no out: nothing is created and no metadata is walked, which is what lets a write
// gate or a paste ask it freely.
//
// The narrowing BY KIND is deliberately not here. A kind carries its own `Type`, and that property is
// an ibValueTypeDescription — a value that already converts ("45.2" to a number, an empty reference
// when nothing fits). Whoever has a kind in hand reads that type and asks IT; a second cutting rule on
// this ctor would be the same conversion under another name.
bool ibCtorMetaValueTypeCharacteristic::AllowValue(const ibClassID& clsid) const
{
	// EMPTY PASSES (class id 0), as everywhere: a declaration says what a value IS when there is one,
	// never that there is one.
	if (clsid == g_valueUndefinedCLSID)
		return true;

	const std::vector<ibClassID>& allowed = m_metaObject->GetTypesOfCharacteristics().GetClsidList();
	return std::find(allowed.begin(), allowed.end(), clsid) != allowed.end();
}
