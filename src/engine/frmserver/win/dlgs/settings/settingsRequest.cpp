#include "settingsRequest.h"

#include "backend/backend_exception.h"     // a setting the schema refuses — said to the window, which stays open
#include "backend/backend_mainFrame.h"
#include "backend/backend_picture.h"       // ibBackendPicture::SendPicture — a field's picture, by its id
#include "backend/backend_composition.h"   // ibValidateSettings — a setting the schema refuses
#include "backend/metaData.h"              // the cell's value made, its type's quick choice
#include "backend/metadataSerialization.h" // …and read
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/srcDataObject.h"                                    // ibSourceDataObject::ibSourceExplorer — a target's fields
#include "backend/metaCollection/partial/reference/reference.h"     // ibValueReferenceDataObject — reference-as-source
#include "backend/query/columnLayout.h"                              // ibIsComparableType — a whole-value blob has nothing to compare

#include "frmserver/client/clientFrame.h"          // ibClientFrame::ReplacePendingRequest — a value chosen, told to the window
#include "frmserver/visualView/ctrl/frame.h"       // ibControlFrame — the cell a value is chosen for
#include "frmserver/visualView/ctrl/typeControl.h" // ibTypeControlFactory::ChooseValue — the one road a Select button walks

#include "protocol/protocol.h"   // ibProtocol::ibRequest::ibListSettings::ibAct

#include <map>
#include <set>

void ibWriteSettingsField(ibDataNode& fields, const ibString& name, const ibString& presentation, ibMetaID id,
	const ibTypeDescription& type, bool available, const ibPictureDescription& picture, const ibMetaData* metaData)
{
	ibDataNode& field = fields.AddChild(0, 0);
	field.SetValue(ibProtocol::ibRequest::ibListSettings::Name, name);
	if (!presentation.IsEmpty())
		field.SetValue(ibProtocol::ibRequest::ibListSettings::Presentation, presentation);
	field.SetValue(ibProtocol::ibRequest::ibListSettings::Id, static_cast<std::int32_t>(id));
	field.SetValue(ibProtocol::ibRequest::ibListSettings::Available, available);
	// A field's picture is its source's, of the configuration the fields were read from.
	if (!picture.IsEmptyPicture())
		field.SetValue(ibProtocol::ibRequest::ibListSettings::Picture, ibBackendPicture::SendPicture(picture, metaData));
	ibDataValue written;
	ibTypeDescriptionMemory::WriteNode(written, type);
	field.SetProperty(ibProtocol::ibRequest::ibListSettings::Type, written);
}

void ibWriteSettingsReferences(ibDataNode& references, const std::vector<ibTypeDescription>& types,
	const ibMetaData* metaData)
{
	std::set<ibClassID> seen;
	for (const ibTypeDescription& type : types) {
		for (const ibClassID& clsid : type.GetClsidList()) {
			if (!seen.insert(clsid).second)
				continue;
			const std::vector<ibMetaID> targets = ibValueReferenceDataObject::ConvertToMetaIds({ clsid }, metaData);
			if (targets.empty())
				continue;
			ibDataNode& reference = references.AddChild(0, 0);
			reference.SetValue(ibProtocol::ibRequest::ibListSettings::Type, ibString::Format(ibT("%llu"), (unsigned long long)clsid));
			std::vector<ibDataValue> ids;
			for (const ibMetaID& target : targets)
				ids.push_back(ibDataValue::Int(target));
			reference.AddField(ibProtocol::ibRequest::ibListSettings::Targets, ibDataValue::Array(ids));
			// …and its empty value, written as a value chosen is (WriteChosen): read as it reads here — in the designer
			// `CatalogRef.Goods.EmptyRef`, at run time nothing. The client has no name of its own for a reference. A family
			// (AnyRef) has no value of its own to write.
			if (metaData != nullptr && metaData->IsRegisterCtor(clsid)) {
				const ibValue empty = metaData->CreateObject(clsid);
				ibDataNode& value = reference.Child(ibProtocol::ibRequest::ibListSettings::Value);
				empty.Serialize(value);
				value.SetValue(ibProtocol::ibRequest::ibListSettings::Presentation, ibString(empty.GetString()));
			}
		}
	}
}

void ibWriteSettingsClasses(ibDataNode& classes, const std::vector<ibTypeDescription>& types, const ibMetaData* metaData)
{
	if (metaData == nullptr)
		return;
	std::set<ibClassID> seen;
	for (const ibTypeDescription& type : types) {
		for (const ibClassID& clsid : type.GetClsidList()) {
			if (!seen.insert(clsid).second)
				continue;
			const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(clsid);
			if (ctor == nullptr)
				continue;
			ibDataNode& entry = classes.AddChild(0, static_cast<ibMetaID>(classes.Children().size() + 1));
			entry.SetValue(ibProtocol::ibSchema::ibProperties::Type, ibString::Format(ibT("%llu"), (unsigned long long)clsid));
			entry.SetValue(ibProtocol::ibSchema::ibProperties::Name, ctor->GetClassName());
			entry.SetValue(ibProtocol::ibSchema::ibProperties::Picture, ibBackendPicture::SendPicture(clsid));
		}
	}
}

namespace {

// A field this tree can offer at all — the desktop's ibFieldIsOfferable: a tabular section binds no condition, a value
// kept WHOLE is one BLOB field SQL compares in no way, and a field the options of this base take away is not offered.
bool ibFieldIsOfferable(const ibSourceDataObject::ibSourceExplorer* col)
{
	return col != nullptr && !col->IsTableSection() && ibIsComparableType(col->GetTypeValueDesc())
		&& (col->GetColumn() == nullptr || col->GetColumn()->IsAvailable());
}

// THE FIELDS OF A REFERENCE'S TARGETS — the desktop's ExpandSourceFieldNode, answered here where the configuration is.
// ⭐⭐ ONE NAME, ONE FIELD — AND ALL THE NAMES THERE ARE: a field several targets share is one field, COMPOSITE, its type
// the union of what each declares; what is shown is the union of the names, in the order the targets declare them.
void WriteTargetFields(ibDataNode& expanded, ibDataNode& references, const std::vector<ibMetaID>& targetIds,
	const ibMetaData* metaData)
{
	// The reference is only what VENDS the fields, and nothing is read to make one (ibReferenceLoad::OnDemand). It is held
	// for as long as its explorer is walked below.
	struct ibTargetSource {
		ibValue                                     m_ref;
		const ibSourceDataObject::ibSourceExplorer* m_explorer = nullptr;
	};
	std::vector<ibTargetSource> targets;
	for (const ibMetaID& target : targetIds) {
		ibTargetSource one;
		one.m_ref = ibValueReferenceDataObject::Create(metaData, target);
		ibSourceDataObject* refObj = nullptr;
		if (!one.m_ref.ConvertToValue(refObj) || refObj == nullptr)
			continue;
		one.m_explorer = refObj->GetSourceExplorer();
		if (one.m_explorer != nullptr)
			targets.push_back(one);
	}

	// WHAT EACH NAME MAY BE, ACROSS THE TARGETS — gathered first, so a name is put up once and already knows its whole type.
	std::map<ibString, ibTypeDescription> united;
	for (const ibTargetSource& one : targets)
		for (unsigned int i = 0; i < one.m_explorer->GetHelperCount(); ++i) {
			const auto* col = one.m_explorer->GetHelper(i);
			if (!ibFieldIsOfferable(col))
				continue;
			ibTypeDescription& type = united[col->GetSourceName()];
			for (const ibClassID& clsid : col->GetTypeValueDesc().GetClsidList())
				if (!type.ContainType(clsid))
					type.AppendMetaType(clsid);
		}

	// …then put up in the order the targets declare them; a name already up is not put up again.
	std::set<ibString> shown;
	std::vector<ibTypeDescription> types;
	for (const ibTargetSource& one : targets)
		for (unsigned int i = 0; i < one.m_explorer->GetHelperCount(); ++i) {
			const auto* col = one.m_explorer->GetHelper(i);
			if (!ibFieldIsOfferable(col) || !shown.insert(col->GetSourceName()).second)
				continue;
			const ibTypeDescription& type = united[col->GetSourceName()];
			ibWriteSettingsField(expanded, col->GetSourceName(),
				col->GetSourceSynonym().IsEmpty() ? col->GetSourceName() : col->GetSourceSynonym(),
				static_cast<ibMetaID>(col->GetSourceId()), type, true, col->GetSourcePicture(), metaData);
			types.push_back(type);
		}
	ibWriteSettingsReferences(references, types, metaData);
}

// THE WINDOW'S QUESTION — what the door tells it, the setting it edits, and `error` — why the one it gave back was refused.
ibDataNode SettingsRequest(const ibDataNode& question, const ibSettingsDescription& shown, const ibString& error)
{
	ibDataNode request = question;
	ibSettingsDescriptionMemory::WriteNode(request.Child(ibProtocol::ibRequest::ibListSettings::Settings), shown);
	if (!error.IsEmpty())
		request.SetValue(ibProtocol::ibRequest::ibListSettings::Error, error);
	return request;
}

} // namespace

void ibWriteSettingsTargetFields(ibDataNode& expanded, ibDataNode& references, const std::vector<ibMetaID>& targets,
	const ibMetaData* metaData)
{
	WriteTargetFields(expanded, references, targets, metaData);
}

bool ibListSettingsChoice::HasQuickChoice() const
{
	return m_metaData != nullptr && ::HasQuickChoice(m_metaData->GetAvailableCtor(m_value.GetClassType()));
}

void ibListSettingsChoice::ChoiceProcessing(ibValue& selected)
{
	m_value = selected;
	m_chosen = true;
	// …said to the window waiting: its question, the newest, said anew with the value in it.
	ibClientFrame* const frame = ibClientFrame::GetFrame();
	if (!m_detached && frame != nullptr && frame->HasPendingRequest()) {
		ibDataNode request = frame->PeekPendingRequest().request;
		WriteChosen(request);
		frame->ReplacePendingRequest(request);
	}
}

void ibListSettingsChoice::WriteChosen(ibDataNode& request) const
{
	if (!m_chosen)
		return;
	ibDataNode& chosen = request.Child(ibProtocol::ibRequest::ibListSettings::Chosen);
	chosen.SetValue(ibProtocol::ibRequest::ibListSettings::Choice, m_choice);
	ibDataNode& value = chosen.Child(ibProtocol::ibRequest::ibListSettings::Value);
	m_value.Serialize(value);
	value.SetValue(ibProtocol::ibRequest::ibListSettings::Presentation, ibString(m_value.GetString()));
}

void ibChooseSettingsValue(const ibDataNode& act, const ibMetaData* metaData, ibListSettingsChoice*& choosing)
{
	if (choosing != nullptr) {
		choosing->Detach();
		choosing->ControlDecrRef();
		choosing = nullptr;
	}
	unsigned long long parsed = 0;
	if (metaData == nullptr || !act.GetValue<ibString>(ibProtocol::ibRequest::ibListSettings::Type).ToULongLong(&parsed) || parsed == 0)
		return;
	const ibClassID clsid = parsed;

	// The value in the cell — the server's own node, given back; none, or one the configuration cannot read: the empty
	// one of the type.
	ibValue value;
	if (const auto written = act.FindChild(ibProtocol::ibRequest::ibListSettings::Value)) {
		try {
			value = ibDeserializeValue(*written, metaData);
		}
		catch (const ibCoreException&) {
		}
	}
	if (value.GetClassType() != clsid && metaData->IsRegisterCtor(clsid))
		value = metaData->CreateObject(clsid);

	choosing = new ibListSettingsChoice(metaData, clsid, value, act.GetValue<std::int32_t>(ibProtocol::ibRequest::ibListSettings::Choice));
	choosing->ControlIncrRef();
	ibTypeControlFactory::ChooseValue(choosing);
}

bool ibRequestSettings(const ibDataNode& question, ibSettingsDescription& edited, const ibMetaData* metaData)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	// A setting refused is asked again AS IT WAS GIVEN, with why — the person's edits stay in the window.
	ibSettingsDescription shown = edited;
	ibString error;
	ibDataNode request = SettingsRequest(question, shown, error);

	// The cell a value is being chosen for — held while the window is up, let go with it.
	ibListSettingsChoice* choosing = nullptr;
	struct ibChoosingRelease {
		ibListSettingsChoice*& choosing;
		~ibChoosingRelease() {
			if (choosing != nullptr) {
				choosing->Detach();
				choosing->ControlDecrRef();
			}
		}
	} release{ choosing };

	for (;;) {
		ibDataNode response;
		if (!frame->Request(request, response))
			return false;

		// ON THE WAY — a reference field opened: the fields of its targets, and the window asked again with them; a cell's
		// value to choose: chosen, and the window asked again — with it, when the choice was made at once.
		if (response.Has(ibProtocol::ibRequest::ibListSettings::Act)) {
			const ibProtocol::ibRequest::ibListSettings::ibAct act = static_cast<ibProtocol::ibRequest::ibListSettings::ibAct>(response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibListSettings::Act));
			request = SettingsRequest(question, shown, error);
			if (act == ibProtocol::ibRequest::ibListSettings::ibAct::Expand) {
				std::vector<ibMetaID> targets;
				if (const auto listed = response.FindField(ibProtocol::ibRequest::ibListSettings::Targets)) {
					if (listed->Kind() == ibDataKind::Array)
						for (const ibDataValue& id : listed->AsArray())
							targets.push_back(static_cast<ibMetaID>(id.AsInt()));
				}
				WriteTargetFields(request.Child(ibProtocol::ibRequest::ibListSettings::Expanded), request.Child(ibProtocol::ibRequest::ibListSettings::References),
					targets, metaData);
			}
			else if (act == ibProtocol::ibRequest::ibListSettings::ibAct::Choose) {
				ibChooseSettingsValue(response, metaData, choosing);
				if (choosing != nullptr)
					choosing->WriteChosen(request);
			}
			continue;
		}

		const auto settings = response.FindChild(ibProtocol::ibRequest::ibListSettings::Settings);
		if (!settings)
			return false;   // …and Cancel leaves the composer exactly as it was: the copy is dropped

		// What the window gave back — its values stay packed, restored by whoever runs them (backend_composition.h) — and
		// refused as the desktop's window refuses it: said, and the window stays open.
		ibSettingsDescription given;
		try {
			ibSettingsDescriptionMemory::ReadNode(*settings, given);
			ibValidateSettings(given);
		}
		catch (const ibCoreException& err) {
			shown = given;
			error = err.GetErrorDescription();
			request = SettingsRequest(question, shown, error);
			continue;
		}
		edited = given;
		return true;
	}
}
