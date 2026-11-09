#ifndef _ATTRIBUTE_CONTROL_H__
#define _ATTRIBUTE_CONTROL_H__

#include "frmserver/frmserver.h"

#include <vector>

///////////////////////////////////////////////////////////////////////////
#include "backend/backend_type.h"
#include "core/description/sourceDescription.h"   // ibSourceDescription — the binding path (GetSourceDesc)
#include "backend/compiler/value.h"
#include "backend/createRequest.h"   // ibSelectMode (GetSelectMode's return type)
#include "backend/choiceLinkResolver.h"             // ibChoiceHolder — who holds this control's field
///////////////////////////////////////////////////////////////////////////

class BACKEND_API ibMetaData;

class BACKEND_API ibValueMetaObject;
class BACKEND_API ibValueMetaObjectAttributeBase;
class BACKEND_API ibValueMetaObjectGenericData;
class BACKEND_API ibValueMetaObjectTableData;

class BACKEND_API ibSourceDataObject;

///////////////////////////////////////////////////////////////////////////
class FRMSERVER_API ibControlFrame;
///////////////////////////////////////////////////////////////////////////

#include "backend/srcObject.h"

class FRMSERVER_API ibTypeControlFactory : public ibBackendTypeSourceFactory {
public:

	// What may be picked from a hierarchical catalog — asked of the source holding the bound field; a text field and a
	// table column answer with their own word over it (ChoiceFoldersAndItems).
	virtual ibSelectMode GetSelectMode() const;

	// ⭐ WHAT A CHOICE HERE IS MADE UNDER — the one thing a value is chosen and found by: the choice form opens with it,
	// the quick choice and the text typed in find under it (ibValue::FindValue), so neither finds more than the form shows
	// (#616). By default the bound field's parameters (ibChoiceLinkResolver::Resolve over the holder); a text field and a
	// table column add their own rows over them, read in the form.
	virtual ibChoiceCondition GetChoiceCondition() const;

	// GetSourceList is NOT implemented here — each concrete control overrides
	// ibBackendTypeSourceFactory::GetSourceList(out) using its own GetOwnerForm()
	// + GetFilterSourceDataType() (no cross-cast). See widgets / tableBox.

	// (A VALUE IS *NOT* ADJUSTED HERE. It was, for one draft — and a form is one way a value arrives,
	//  not the way it arrives most: a script, a record set written on the server and a data exchange
	//  reach the same fields with no control anywhere. The narrowing belongs where the data is, so it
	//  happens in the holder's own write, through ibChoiceLinkResolver::Adjust — Max, 2026-09-23.)

	// THE ONE ROUTE every value editor walks when its Select button is pressed:
	//
	//   Undefined      -> ShowSelectType decides the type (asking only when the cell
	//                     admits more than one) and the value is created;
	//   typed value    -> the quick choice for that type, and if the type has none,
	//                     the metaobject's own selection form.
	//
	// Written ONCE here rather than in each control: a text control on a form, a
	// table column and a filter cell are the same conversation with the user, and
	// the second copy of it is where they start to differ by accident.
	// `choiceForm` — the selection form the caller wants opened (a text control and a
	// table column each carry one as a property); null = the metaobject's own.
	// A number and a date are picked by the client itself (a calculator, a calendar):
	// what comes back is typed into the field like anything else.
	static bool ChooseValue(ibControlFrame* ownerValue, const class ibValueMetaObject* choiceForm = nullptr);

	// The type a value is created of, out of those a description admits — an "any" offers every reference it admits; asked
	// of the person when there are several, 0 when the person closes the choice.
	static ibClassID ShowSelectType(const ibMetaData* metadata, const ibTypeDescription& typeDescription);

protected:

	// ⭐ WHO HOLDS THIS CONTROL'S FIELD — the form's source for a control on a form, the ROW for a table
	// column. One question, asked the way this class already asks GetOwnerForm / GetSourceObject; the
	// answer is the tree's own pair of source shapes (choiceLinkResolver.h), not a new one.
	//
	// 🛑 AND NOT A PAIR OF VALUE VERBS, which is what stood here first: reading and writing values is
	// the source's job, and putting it on the control dragged it onto the SCHEMA, which must know
	// nothing about runtime values (Max, 2026-09-23).
	//
	// Empty by default: a control bound to nothing — a filter cell, a script's value — holds no fields
	// and nothing is chosen within it.
	virtual ibChoiceHolder GetChoiceHolder() const { return ibChoiceHolder(); }

	// ⭐ …AND WHAT A WRITE THROUGH THIS CONTROL LEAVES STALE (#616): `changed` — the control itself, its own `this` (an
	// implicit upcast, as ibValueControl::AutoBindNewSource takes its factory) — showed `before`, and if it shows anything
	// else now, every control of its form whose OWN links name this field — a link by type, or a row that does not say
	// Keep — is emptied through its own door. Asked by the control's write, the one door a value typed or chosen into it
	// passes; the emptied control's write asks it in turn, so the chain follows by itself and stops where a value is
	// already empty. The data's own links are its record's to empty (ibChoiceLinkResolver::ClearLinked).
	void ClearLinked(ibControlFrame* changed, const ibValue& before) const;

private:

	// The short list `current` is picked from (an enumeration's members, a boolean, a small reference list), among what
	// `condition` leaves, put to the person as a choice. False when its type has none.
	static bool QuickChoice(ibControlFrame* ownerValue, const ibValue& current, const ibChoiceCondition& condition,
		const ibMetaData* metaData);
	// A NUMBER ON ITS CALCULATOR, A DATE ON ITS CALENDAR — the desktop's SimpleChoice, drawn by the client under the
	// field pressed (ibProtocol::ibRequest::ibKind::SimpleChoice); the value chosen is handed to the field. False when the type
	// is neither. Asked first by the quick choice.
	static bool SimpleChoice(ibControlFrame* ownerValue, const ibValue& current, const ibMetaData* metaData);
};

#endif