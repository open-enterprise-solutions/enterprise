#include "form.h"
#include "backend/backend_exception.h"   // ibBackendException — a control that refuses to build keeps its reason
#include "core/serialize/dataBuilder.h"   // ibDataNode — a pasted control, as a form holds it

#include <unordered_set>

ibValueFrame* ibValueForm::NewObject(const ibClassID& clsid, ibValueFrame* controlParent, const ibValue& generateId)
{
	if (ibValue::IsRegisterCtor(clsid)) {
		ibValuePtr<ibValueFrame> newControl;
		ibValue id(generateId);   // CreateObject's arguments are slots, as a script's call hands them
		ibValue* ppParams[] = { this, controlParent, &id };
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
	for (unsigned int index = 0; takenNames.contains(toKey(strGenerateName)); )
		strGenerateName = ibString::Format(ibT("%s%i"), strOriginalName, ++index);

	control->SetControlName(strGenerateName);
}

namespace {

// The ids of a control and of what is in it — made for what has none (put on the form).
void GenerateId(ibValueFrame* object)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++)
		GenerateId(object->GetChild(idx));
	if (object->GetControlID() == 0)
		object->GenerateNewID();
}

// …and taken off — a pasted copy's were those of the form it was copied from (the desktop's ResetId).
void ResetId(ibValueFrame* object)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++)
		ResetId(object->GetChild(idx));
	object->SetControlID(0);
}

// The names of a control and of what is in it made unique on its form — a pasted copy's (the desktop's PasteObject).
void ResolveNameConflict(ibValueForm* form, ibValueFrame* object)
{
	form->ResolveNameConflict(object);
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++)
		ResolveNameConflict(form, object->GetChild(idx));
}

}

ibValueFrame* ibValueForm::PasteObject(ibValueFrame* dstObject, const ibDataNode& copied)
{
	// The copy read as the form reads a control of its own (LoadNode), made in what it is pasted on, then taken off it:
	// anchored across that — a child is owned by its parent's list.
	ibValueFrame* parentObject = dstObject;
	ibDataNode node = copied;
	ibValueFrame* const clipboard = NewObject(node.GetClsid(), parentObject, false);
	if (clipboard == nullptr)
		return nullptr;
	const ibValuePtr<ibValueFrame> clipboardKeep(clipboard);
	const bool loaded = clipboard->LoadNode(node);
	parentObject->RemoveChild(clipboard);
	clipboard->SetParent(nullptr);
	if (!loaded)
		return nullptr;

	// If it cannot be put in what it was pasted on, it is put in its parent, right after it — and so on up: the first
	// that takes one of its class (FilterChild).
	while (parentObject != nullptr && !parentObject->FilterChild(clipboard->GetClassType()))
		parentObject = parentObject->GetParent();
	if (parentObject == nullptr)
		return nullptr;

	// …under this form's ids, its names unique on it — the ids first: a name is compared with the controls that have one.
	ResetId(clipboard);
	parentObject->AddChild(clipboard);
	clipboard->SetParent(parentObject);
	GenerateId(clipboard);
	::ResolveNameConflict(this, clipboard);
	return clipboard;
}
