#include "formAttribute.h"
#include "form.h"

//*********************************************************************************************
//*                            the attribute's PROPERTY aspect                                *
//*                                                                                            *
//* Split out of formAttribute.cpp the way every control in this folder has it (control,       *
//* frame, checkbox, textctrl, tableBox, tableBoxColumn, gridBox, toolBar): what an object      *
//* DOES when its properties are edited is one aspect and lives in one file.                    *
//*********************************************************************************************

void ibFormAttributeValue::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// ⭐⭐ THE CHILD CALLS THIS ITSELF NOW — nothing is pushed down to it from here (Max, 2026-08-24).
	// A property belongs to the object that created it, that object reacts to its own change, and
	// ibPropertyObject's default carries the news UP the attach chain, which is how it arrives here.
	//
	// 🛑 THIS USED TO FORCE IT DOWNWARD: `property->GetPropertyObject()->OnPropertyChanged(...)`. Once
	// the child spoke for itself, that closed a ring — down to the child, up from the child, down
	// again — and changing an attribute's Type recursed until the stack was gone. Two roads between
	// the same two objects is a ring however carefully each one is written.
	//
	// The attribute's Type drives the value's type: re-materialise + re-accumulate, so the
	// inspector shows the new type's properties. Asked of the attribute — its own Type property is
	// its own, so whose property this is needs no second question.
	if (m_attribute->IsTypeProperty(property))
		Refresh();
}

void ibFormAttributeValue::OnChildChanged()
{
	// A nested value changed (a value-table column-info). Keep bubbling up (the base is a no-op once there is
	// no further owner); the next frame the form writes reads the value as it now is.
	ibPropertyObject::OnChildChanged();
}

// The FillCheck list a property offers — a property's own content, so it belongs beside the
// handlers rather than in the runtime file.
bool ibFormAttributeValue::ibFormAttribute::FillFillCheck(ibPropertyList* prop)
{
	// THE KINDS THEMSELVES, not the numbers they happen to have — the list is a VIEW of the
	// enumeration, so adding a third answer is one enumerator and one line here rather than a hunt
	// for every place a literal 1 stood for "show an error".
	prop->AppendItem(_("Don't check"), ibFormAttributeFillCheck_DontCheck, wxBitmap());
	prop->AppendItem(_("Show error"),  ibFormAttributeFillCheck_ShowError, wxBitmap());
	return true;
}
