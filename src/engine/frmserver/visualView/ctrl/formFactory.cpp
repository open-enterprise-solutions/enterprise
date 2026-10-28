#include "form.h"
#include "frmserver/visualView/ctrl/sizer.h"
#include "backend/backend_exception.h"   // ibBackendException — a control that refuses to build keeps its reason

#include <algorithm>
#include <cwctype>
#include <string>
#include <unordered_set>

// 🛑 THE ASSERT STAYS — IT IS THE TRIPWIRE FOR OUR OWN CALLERS. A name coming from the palette or
// from a form resource IS an invariant, and a typo there must stop the developer.
//
// What went is the line AFTER it: `objectSingle->GetTypeControlName()` dereferenced the very null
// the assert had just complained about, so a release build died where a debug build merely
// grumbled. It died for real over a string that arrived from OUTSIDE — `form_accepts` called with
// no `class` reached here with an empty one and took the designer with it (dump designer_25968,
// 2026-09-01). Outside names are validated at the door they arrive through; here they only have to
// not be fatal.
inline ibString GetClassType(const ibString& className)
{
	const ibCtorControlTypeBase* objectSingle =
		dynamic_cast<const ibCtorControlTypeBase*>(ibValue::GetAvailableCtor(className));
	wxASSERT(objectSingle);
	return objectSingle != nullptr ? objectSingle->GetTypeControlName() : ibString(ibEmptyString);
}

inline void SetDefaultLayoutProperties(ibValueSizerItem* sizerItem)
{
	ibValueFrame* child = sizerItem->GetChild(0);
	const ibString& obj_type = child->GetObjectTypeName();

	if (obj_type == ibT("Sizer")) {
		sizerItem->SetBorder(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == ibT("Splitter") ||
		child->GetClassName() == ibT("Spacer")) {
		sizerItem->SetProportion(1);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == ibT("Staticline")) {
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == ibT("Toolbar")) {
		sizerItem->SetBorder(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (child->GetClassName() == ibT("Notebook")) {
		sizerItem->SetProportion(1);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
	else if (obj_type == ibT("Widget") ||
		obj_type == ibT("Statusbar")) {
		sizerItem->SetProportion(0);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxSHRINK);
	}
	else if (obj_type == ibT("Container")) {
		sizerItem->SetProportion(1);
		sizerItem->SetFlagBorder(wxALL);
		sizerItem->SetFlagState(wxEXPAND);
	}
}

ibValueFrame* ibValueForm::NewObject(const ibClassID& clsid, ibValueFrame* controlParent, const ibValue& generateId)
{
	if (ibValue::IsRegisterCtor(clsid)) {
		ibValuePtr<ibValueFrame> newControl;
		ibValue* ppParams[] = { this, controlParent, const_cast<ibValue*>(&generateId) };
		try {
			newControl = ibValue::CreateObject(clsid, ppParams, 3);
			if (newControl == nullptr) {
				// Built, but not an element of a form — the class id names something else. Said, as below.
				ibJournalError(ibT("ui.form"), _("Class id %lld is not a form element"), (long long)clsid);
				return nullptr;
			}
			// A parented control is owned by the parent's vector (AddChild inside
			// Init). A rootless control has no vector owner, so the caller takes
			// ownership via this reference (e.g. an ibValuePtr member) — taken before
			// the holder here lets go.
			if (controlParent == nullptr) newControl->IncrRef();
		}
		// 🛑 THE REFUSAL KEEPS ITS REASON. A control that cannot be built returns nullptr, and every
		// caller treats that as "not this one" — a form LOADING its controls simply skipped the one
		// that threw, so a saved control disappeared from the form with nothing said anywhere. The
		// nullptr contract stays (an old file may name a control this build does not have, and that
		// must not cost the whole form); what changes is that the engine's own words survive it.
		catch (const ibCoreException& err) {
			ibJournalError(ibT("ui.form"), ibT("%s"), err.GetErrorDescription());
			return nullptr;
		}
		catch (...) {
			ibJournalError(ibT("ui.form"), _("Failed to create form element (class id %lld)"), (long long)clsid);
			return nullptr;
		}
		return newControl;
	}
	return nullptr;
}

void ibValueForm::ResolveNameConflict(ibValueFrame* control)
{
	if (control->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		return;

	// Derive the base name: the original control name with any trailing
	// digits stripped, so "Button3" reuses the base "Button".
	ibString strOriginalName;

	if (control->GetControlNameAsString(strOriginalName)) {

		if (!strOriginalName.IsEmpty()) {
			size_t length = strOriginalName.length();
			while (length > 0 && stringUtils::IsDigit(strOriginalName[length - 1]))
				--length;
			strOriginalName = strOriginalName.Left(length);
		}
		else {
			strOriginalName = control->GetClassName();
		}
	}

	// Normalise to upper case, matching stringUtils::IsSameAs's
	// case-insensitive semantics, so set lookups are exact comparisons.
	const auto toKey = [](const ibString& name) {
		return name.Upper();
	};

	// Collect the names already taken by the other controls once, so each
	// candidate is probed in O(1). The old do/while rescanned the whole
	// control list for every candidate index — O(controls * candidates)
	// per call and O(N^3) when laying out N like-named controls.
	std::unordered_set<ibString> takenNames;
	ibString strControlName;
	for (const auto valueControl : control->GetOwnerForm()->GetControlList()) {

		if (0 == valueControl->GetControlID())
			continue;
		if (control == valueControl)
			continue;
		if (!valueControl->GetControlNameAsString(strControlName))
			continue;

		takenNames.insert(toKey(strControlName));
	}

	// the name cannot be repeated within the same form: bump the
	// numeric suffix until the candidate is free.
	ibString strGenerateName = strOriginalName;
	for (unsigned int index = 0; takenNames.count(toKey(strGenerateName)) > 0; )
		strGenerateName = ibString::Format(ibT("%s%i"), strOriginalName, ++index);

	control->SetControlName(strGenerateName);
}

ibValueFrame* ibValueForm::CreateObject(const ibString& className, ibValueFrame* controlParent)
{
	ibValueFrame* object = nullptr;

	// No name at all is not a typo to trip the assert on — it is nothing asked for, and the answer
	// is the same null every unbuildable control gets. The caller already has the wording for it.
	if (className.IsEmpty())
		return nullptr;

	ibString classType = ::GetClassType(className);
	if (classType.IsEmpty())
		return nullptr;

	if (controlParent) {
		//FIXME! This is a patch to avoid creating the menubar, statusbar and
		//toolbar types in a form that is not a wxFrame.
		//The set of types needs to be modified to allow having several kinds
		//of forms (like childType of project), but there is a lot of code that is invalid
		//for forms that are not of type "form". In other words, there is
		//code that depends on the type name, which must be avoided.
		if (controlParent->GetObjectTypeName() == ibT("Form") && controlParent->GetClassName() != ibT("ClientForm") &&
			(classType == ibT("Statusbar") ||
				classType == ibT("Menubar") ||
				classType == ibT("Ribbonbar") ||
				classType == ibT("Toolbar")))

			return nullptr; // not a valid type

		// No menu dropdown for wxToolBar until wx 2.9 :(
		if (controlParent->GetObjectTypeName() == ibT("Tool"))
		{
			ibValueFrame* gParent = controlParent->GetParent();

			if (
				(gParent->GetClassName() == ibT("Toolbar")) &&
				(className == ibT("Menu"))
				)
				return nullptr; // not a valid type
		}

		if (controlParent->GetObjectTypeName() == ibT("Toolbar"))
		{
			if (classType == ibT("Tool") /*|| classType == wxT("widget")*/)
			{
				object = NewObject(className, controlParent);
				//set enabled item
				//object->SetReadOnly(controlParent->IsEditable());
			}
		}
		else if (controlParent->GetObjectTypeName() == ibT("Notebook"))
		{
			if (classType == ibT("NotebookPage")) {
				object = NewObject(className, controlParent);
				//set enabled item
				//object->SetReadOnly(controlParent->IsEditable());
			}
		}
		else if (controlParent->GetObjectTypeName() == ibT("Container")
			&& controlParent->GetClassName() == ibT("Tablebox"))
		{
			if (classType == ibT("TableboxColumn"))
			{
				object = NewObject(className, controlParent);
				//set enabled item
				//object->SetReadOnly(controlParent->IsEditable());
			}
		}
		// A column GROUP takes exactly what a table takes — columns, and groups of
		// their own. Its own kind IS "TableboxColumn", which is what lets the branch
		// above accept it into a table without a second rule for it.
		else if (controlParent->GetClassName() == ibT("TableboxColumnGroup"))
		{
			if (classType == ibT("TableboxColumn"))
			{
				object = NewObject(className, controlParent);
			}
		}
		else if (controlParent->GetObjectTypeName() == ibT("NotebookPage"))
		{
			ibValueSizerItem* sizerItem = NewObject<ibValueSizerItem>("SizerItem", controlParent);
			ibValueFrame* obj = NewObject(className, sizerItem);

			if (controlParent) {
				//sizerItem->SetReadOnly(controlParent->IsEditable());
			}

			// the following condition must always hold,
			// since an item must always contain another object
			if (obj) {
				//set enabled item
				//obj->SetReadOnly(sizerItem->IsEditable());

				// sizerItem is a reserved object type; for convenience it gets default
				// values assigned according to the type of object created
				if (sizerItem->IsSubclassOf(ibT("SizerItem"))) {
					SetDefaultLayoutProperties(sizerItem);
				}

				object = sizerItem;
			}
		}
		else if (controlParent->GetComponentType() == COMPONENT_TYPE_FRAME ||
			controlParent->GetComponentType() == COMPONENT_TYPE_SIZER ||
			controlParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		{
			ibValueSizerItem* sizerItem = NewObject< ibValueSizerItem>("SizerItem", controlParent);
			ibValueFrame* obj = NewObject(className, sizerItem);

			//if (controlParent) {
			//	sizerItem->SetReadOnly(controlParent->IsEditable());
			//}

			// the following condition must always hold,
			// since an item must always contain another object
			if (obj) {
				//set enabled item
				//obj->SetReadOnly(sizerItem->IsEditable());
				// sizerItem is a reserved object type; for convenience it gets default
				// values assigned according to the type of object created
				if (sizerItem->IsSubclassOf(ibT("sizerItem"))) {
					SetDefaultLayoutProperties(sizerItem);
				}

				object = sizerItem;
			}
		}
	}
	else // controlParent == nullptr;
	{
		object = NewObject(className, nullptr);
	}

	return object;
}
