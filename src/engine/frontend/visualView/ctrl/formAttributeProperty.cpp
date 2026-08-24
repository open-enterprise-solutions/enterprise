#include "formAttribute.h"
#include "form.h"

#include "frontend/visualView/visualHost.h"   // ibFrontendVisualEditorNotebook — the editor a change is reported to
#include "frontend/docView/docView.h"         // ibMetaDocument::Modify — where modified-ness lives

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
	ibPropertyObject* owner = property != nullptr ? property->GetPropertyObject() : nullptr;

	// The attribute's Type drives the value's type: re-materialise + re-accumulate, so the
	// inspector shows the new type's properties.
	if (owner == static_cast<ibPropertyObject*>(&*m_attribute) && m_attribute->IsTypeProperty(property))
		Refresh();

#ifndef OES_USE_WEB
	// EXACTLY the path a control edit takes (see controlProperty / frameProperty): route through the visual
	// editor's ModifyProperty command. That command is the ONE place a property edit marks the form modified
	// AND rebuilds the editor (canvas + object tree + attribute tree — a Type/source change can cascade into
	// bindings and controls). The holder IS the inspector's selected object, so this fires once; a genuinely
	// nested value (a value-table column) is edited as its OWN selection and bubbles via OnChildChanged instead.
	if (ibFrontendVisualEditorNotebook* editor = ibFrontendVisualEditorNotebook::FindEditorByForm(m_attribute->GetOwnerForm()))
		editor->ModifyProperty(property, oldValue, newValue);
#endif
}

void ibFormAttributeValue::OnChildChanged()
{

	// A nested value changed (a value-table column-info edited in the inspector). The holder is the
	// frontend end of the attach chain — re-render the bound control live. Keep bubbling up too (the
	// base is a no-op once there is no further owner).
	ibPropertyObject::OnChildChanged();

#ifndef OES_USE_WEB
	if (ibFrontendVisualEditorNotebook* editor = ibFrontendVisualEditorNotebook::FindEditorByForm(m_attribute->GetOwnerForm())) {
		// 🛑 AND IT IS A MODIFICATION, not just a repaint (Max, 2026-08-20: "the same defect is in
		// the dynamic list — when I change something it has to be able to say it changed").
		//
		// A property edit one function up goes through ModifyProperty, which is the one place that
		// marks the form modified. This road had only the repaint half: editing a dynamic list's
		// SETTINGS — a filter, a sort, a grouping — redrew the editor and left the configuration
		// looking untouched, so Save had nothing to do and the work was gone on the next open.
		//
		// The DOCUMENT is asked rather than the metadata directly: ibMetaDocument::Modify delegates
		// to ibMetaData::Modify, which is where modified-ness lives, and going through the document
		// also lets the view put its asterisk in the tab title.
		//
		// ⚠ Not routed through ModifyProperty: this signal deliberately carries no property (see
		// ibPropertyObject::OnChildChanged), and inventing one to push onto the undo stack would put
		// a command there that cannot undo what actually changed.
		if (ibMetaDocument* document = editor->GetEditorDocument())
			document->Modify(true);

		editor->RefreshEditor();
	}
#endif
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
