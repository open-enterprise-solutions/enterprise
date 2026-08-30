////////////////////////////////////////////////////////////////////////////
//	Description : how a person arranged a form — into the settings storage and back
////////////////////////////////////////////////////////////////////////////

#include "frontend/settings/formSettings.h"
#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/ctrl/formAttribute.h"   // the main attribute — what a generated form reads

#include "backend/srcDataObject.h"                    // …and the source behind it, which names its metaobject

#include "backend/appData.h"
#include "backend/diagnostics/journal.h"
#include "backend/metaCollection/metaFormObject.h"   // the form's metaobject — its guid IS the address
#include "backend/serialize/dataBuilder.h"
#include "backend/settings/settingsComposer.h"   // ibUserSettingsKey — whose settings, said once
#include "backend/settings/settingsStorage.h"

#include <array>
#include <functional>

namespace {

// The node: one child per control, its id as the child's identity, the properties in the child's
// property bag and the position as a field. Nothing else — the storage stores nodes, and this is
// the smallest node that says what a form looks like.
const wxChar* const s_fieldControl  = wxT("Control");
const wxChar* const s_fieldPosition = wxT("Position");

// ⭐⭐ A GENERATED FORM IS ADDRESSED BY WHAT IT SHOWS. Not every form is declared in the Designer —
// a catalog with no list form of its own still opens as a list, built on the spot by `BuildForm`,
// and those are exactly the ones people most want to arrange (Max, 2026-08-30: *"for a default form
// you have to take the id of the main object it refers to — the object, the report and so on"*).
// Such a form has no metaobject of its own, so the FIRST attempt refused to save at all.
//
// So there are two addresses and one rule behind them — a form belongs to whatever declares it:
//
//   declared form   → the FORM metaobject's guid.
//   generated form  → the guid of the metaobject its main attribute reads (`GetSourceMetaObject`).
//
// ⚠ AND THE GENERATED ONE NEEDS THE FORM TYPE BESIDE IT. One catalog generates several forms — a
// list, an object, a choice — and all of them name the same metaobject, so keyed by that alone they
// would share one row. Control ids are per-form and start again in each, so a list's stored id 5
// would land on whatever the object form's id 5 happens to be: not a refusal, a silently wrong
// form. The type goes in `m_settingKey`, which is exactly what that part of the address is for —
// WHICH of this object's settings.
//
// ⭐ A GUID IS BUILT FROM BYTES, deterministically, rather than minted: this is an address that has
// to come out the same on every open, and the first fourteen bytes stay zero so it can never
// collide with a real minted guid.
ibGuid FormTypeKey(const ibFormID& formType)
{
	std::array<unsigned char, 16> bytes{};
	bytes[14] = static_cast<unsigned char>((formType >> 8) & 0xFF);
	bytes[15] = static_cast<unsigned char>(formType & 0xFF);
	return ibGuid(bytes);
}

bool SettingsKey(const ibValueForm* form, ibSettingsKey& key)
{
	if (form == nullptr)
		return false;

	// A DECLARED form: it survives a rename, which a name does not, and it is the same address
	// whichever record it happens to be showing — a person arranges the FORM, not one document.
	if (const ibValueMetaObjectFormBase* creator = form->GetFormMetaObject()) {
		key = ibUserSettingsKey(ibSettingsCategory::Form, creator->GetGuid(),
			wxNullGuid, &appData->GetUserInfo());
		return key.IsOk();
	}

	// A GENERATED one: the object it reads, plus which of that object's forms this is.
	const ibFormAttributeValue* mainAttr = form->GetMainAttribute();
	const ibSourceDataObject* source = mainAttr != nullptr ? mainAttr->GetSourceValue() : nullptr;
	const ibValueMetaObjectGenericData* owner = source != nullptr ? source->GetSourceMetaObject() : nullptr;
	if (owner == nullptr)
		return false;   // a form over nothing — there is genuinely no address for it

	key = ibUserSettingsKey(ibSettingsCategory::Form, owner->GetGuid(),
		FormTypeKey(form->GetTypeForm()), &appData->GetUserInfo());

	return key.IsOk();
}

// Everything under this control, itself included.
void EachControl(ibValueFrame* control, const std::function<void(ibValueFrame*)>& visit)
{
	if (control == nullptr)
		return;
	visit(control);
	for (unsigned int idx = 0; idx < control->GetChildCount(); idx++)
		EachControl(control->GetChild(idx), visit);
}

} // namespace

//////////////////////////////////////////////////////////////////////////////

void ibRestoreFormSettings(ibValueForm* form)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	ibSettingsKey key;
	if (storage == nullptr || !SettingsKey(form, key))
		return;

	ibDataNode root;
	if (!storage->Restore(key, root)) {
		ibJournalInfo(wxT("ui.form"), wxT("restore form setting: nothing stored for object %s, type %s"),
			key.m_objectKey.str(), key.m_settingKey.str());
		return;   // nobody saved anything here — the ordinary case, and not an error
	}

	int stored_count = 0, found = 0, missing = 0, applied = 0;

	for (const ibDataNode& stored : root.Children()) {

		stored_count++;

		const ibFormID controlId = (ibFormID)stored.GetValue<s32>(s_fieldControl);
		if (controlId == 0)
			continue;

		ibValueFrame* control = form->FindControlByID(controlId);
		if (control == nullptr) {
			missing++;
			continue;   // the author removed it — the rest of the setting still stands
		}
		found++;

		// ⭐ THE WHITELIST GATES THE WAY IN AS WELL AS THE WAY OUT. Read any other way, a value
		// saved while a property was allowed would become a way past the list the day it is taken
		// off — so the loop walks the LIST, not whatever the row happens to hold.
		for (const wxString& name : ibValueFrame::GetAllowedUserProperty()) {

			if (stored.FindProperty(name) == nullptr)
				continue;

			ibProperty* property = control->GetProperty(name);
			if (property == nullptr)
				continue;   // the author renamed or dropped it

			// 🛑 ONE PROPERTY AT A TIME, GUARDED — a value written by an older shape of this form
			// may no longer be of the kind the property expects, and this runs inside the form's
			// initialisation.
			try {
				property->PasteNodeValue(stored.GetProperty(name));
				applied++;
			}
			catch (const ibBackendException& err) {
				ibJournalWarning(wxT("ui.form"), wxT("saved form setting: '%s' not restored (%s)"),
					name, err.GetErrorDescription());
			}
			catch (...) {
				ibJournalWarning(wxT("ui.form"), wxT("saved form setting: '%s' not restored"), name);
			}
		}

		// …and where it sits among its siblings. Clamped rather than refused: the author may have
		// removed the very controls this one was pushed past.
		const int position = stored.GetValue<s32>(s_fieldPosition);
		ibValueFrame* parent = control->GetParent();
		if (position != wxNOT_FOUND && parent != nullptr && parent->GetChildCount() > 0) {
			const int last = (int)parent->GetChildCount() - 1;
			parent->ChangeChildPosition(control, (unsigned int)wxMax(0, wxMin(position, last)));
		}
	}

	// PROBE — how much of the stored setting reached a live control. `found` far below `stored` means
	// the ids do not match this build of the form; `applied` at zero with `found` high means the
	// values are being refused rather than the controls being missing.
	ibJournalInfo(wxT("ui.form"),
		wxT("restore form setting: object %s, type %s, stored %d, found %d, missing %d, applied %d"),
		key.m_objectKey.str(), key.m_settingKey.str(), stored_count, found, missing, applied);
}

ibFormSettingsResult ibSaveFormSettings(const ibValueForm* form)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr)
		return ibFormSettingsResult::NoStorage;

	ibSettingsKey key;
	if (!SettingsKey(form, key))
		return ibFormSettingsResult::NoAddress;

	ibDataNode root;

	int seen = 0, written = 0, unaddressed = 0;

	EachControl(const_cast<ibValueForm*>(form), [&](ibValueFrame* control) {

		seen++;

		const ibFormID controlId = control->GetControlID();
		if (controlId == 0) {
			unaddressed++;
			return;   // nothing addressable to write it under
		}
		written++;

		ibDataNode& child = root.AddChild(0, (ibMetaID)controlId);
		child.SetValue(s_fieldControl, (s32)controlId);

		for (const wxString& name : ibValueFrame::GetAllowedUserProperty()) {
			ibProperty* property = control->GetProperty(name);
			if (property == nullptr)
				continue;
			// Through the property's OWN node pair — that is what survives the round trip for a
			// font or a colour, which a string would not.
			ibDataValue value;
			if (property->CopyNodeValue(value))
				child.SetProperty(name, value);
		}

		ibValueFrame* parent = control->GetParent();
		child.SetValue(s_fieldPosition,
			parent != nullptr ? (s32)parent->GetChildPosition(control) : (s32)wxNOT_FOUND);
	});

	const bool saved = storage->Save(key, root);

	// PROBE — how much of the form actually reached the row. A control with no id is invisible to
	// the setting, and that is the difference between "nothing was saved" and "nothing was there".
	ibJournalInfo(wxT("ui.form"),
		wxT("save form setting: object %s, type %s, controls seen %d, written %d, no id %d, saved %d"),
		key.m_objectKey.str(), key.m_settingKey.str(), seen, written, unaddressed, (int)saved);

	return saved ? ibFormSettingsResult::Ok : ibFormSettingsResult::Refused;
}

ibFormSettingsResult ibResetFormSettings(const ibValueForm* form)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr)
		return ibFormSettingsResult::NoStorage;

	ibSettingsKey key;
	if (!SettingsKey(form, key))
		return ibFormSettingsResult::NoAddress;

	return storage->Remove(key) ? ibFormSettingsResult::Ok : ibFormSettingsResult::Refused;
}

bool ibHasFormSettings(const ibValueForm* form)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	ibSettingsKey key;
	if (storage == nullptr || !SettingsKey(form, key))
		return false;

	ibDataNode node;
	return storage->Restore(key, node);
}
