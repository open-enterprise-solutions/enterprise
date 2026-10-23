#include "settingsRequest.h"

#include "backend/backend_exception.h"     // a setting the schema refuses — said to the window, which stays open
#include "backend/backend_mainFrame.h"
#include "backend/backend_picture.h"       // ibBackendPicture::SendPicture — a field's picture, by its id
#include "backend/backend_composition.h"   // ibValidateSettings — a setting the schema refuses
#include "backend/metaData.h"              // the cell's value read and made, its type's quick choice
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

void ibWriteSettingsField(ibDataNode& fields, const wxString& name, const wxString& presentation, ibMetaID id,
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
			reference.SetValue(ibProtocol::ibRequest::ibListSettings::Type, wxString::Format(wxT("%llu"), (unsigned long long)clsid));
			std::vector<ibDataValue> ids;
			for (const ibMetaID& target : targets)
				ids.push_back(ibDataValue::Int(target));
			reference.AddField(ibProtocol::ibRequest::ibListSettings::Targets, ibDataValue::Array(ids));
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
	std::map<wxString, ibTypeDescription> united;
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
	std::set<wxString> shown;
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

// THE CELL A VALUE IS CHOSEN FOR — the window's on the client (a condition's value), and here what the choice is made for:
// the desktop's filter cell (ibFilterValueRenderer), the owner its quick choice and its choice form hand the value to.
// What is chosen goes to the window with its question (Chosen) — asked again when the choice is made at once, or said
// anew when a choice form, opened over the window, makes it while the window waits.
// Counted: the choice form holds its owner (ControlIncrRef) and may outlive the window — a choice made after it closed
// is told to nobody (Detach).
class ibListSettingsChoice : public ibControlFrame, public ibTypeControlFactory {
public:

	ibListSettingsChoice(const ibMetaData* metaData, const ibClassID& clsid, const ibValue& value, std::int32_t choice)
		: m_metaData(metaData), m_typeDesc(clsid), m_value(value), m_choice(choice) {
	}

	// ibControlFrame
	virtual bool GetControlValue(ibValue& value) const override { value = m_value; return true; }
	virtual bool SetControlValue(const ibValue& value = ibValue()) override { m_value = value; return true; }
	virtual ibGuid GetControlGuid() const override { return m_guid; }
	virtual void ControlIncrRef() override { ++m_refCount; }
	virtual void ControlDecrRef() override {
		if (--m_refCount == 0)
			delete this;
	}
	virtual bool HasQuickChoice() const override {
		return m_metaData != nullptr && ::HasQuickChoice(m_metaData->GetAvailableCtor(m_value.GetClassType()));
	}
	virtual void ChoiceProcessing(ibValue& selected) override {
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

	// ibTypeControlFactory — no column behind the cell: its type is the one it stands on.
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const override { return nullptr; }
	virtual ibSourceObject* GetSourceObject() const override { return nullptr; }
	virtual ibSourceDescription& GetSourceDesc() const override { return m_sourceDesc; }
	virtual const ibMetaData* GetMetaData() const override { return m_metaData; }
	virtual ibTypeDescription& GetTypeDesc() const override { return m_typeDesc; }

	// The value chosen, put into the window's question — how it reads with it, said by the configuration.
	void WriteChosen(ibDataNode& request) const {
		if (!m_chosen)
			return;
		ibDataNode& chosen = request.Child(ibProtocol::ibRequest::ibListSettings::Chosen);
		chosen.SetValue(ibProtocol::ibRequest::ibListSettings::Choice, m_choice);
		ibDataNode& value = chosen.Child(ibProtocol::ibRequest::ibListSettings::Value);
		m_value.Serialize(value);
		value.SetValue(ibProtocol::ibRequest::ibListSettings::Presentation, wxString(m_value.GetString()));
	}

	// The window closed: a choice made later is nobody's.
	void Detach() { m_detached = true; }

private:

	const ibMetaData*           m_metaData;
	mutable ibTypeDescription   m_typeDesc;
	mutable ibSourceDescription m_sourceDesc;
	ibValue                     m_value;
	const std::int32_t                   m_choice;
	const ibGuid                m_guid = ibGuid::newGuid();
	unsigned                    m_refCount = 0;
	bool                        m_chosen = false;
	bool                        m_detached = false;
};

// THE CELL'S CHOICE, MADE — what the window's act names (Act Choose: the type, the value in the cell, the number it is
// asked under): the value's quick choice, or its choice form opened with the cell for its owner. The cell is the
// window's to hold (`choosing`), counted.
void ChooseValue(const ibDataNode& act, const ibMetaData* metaData, ibListSettingsChoice*& choosing)
{
	if (choosing != nullptr) {
		choosing->Detach();
		choosing->ControlDecrRef();
		choosing = nullptr;
	}
	unsigned long long parsed = 0;
	if (metaData == nullptr || !act.GetValue<wxString>(ibProtocol::ibRequest::ibListSettings::Type).ToULongLong(&parsed) || parsed == 0)
		return;
	const ibClassID clsid = parsed;

	// The value in the cell — the server's own node, given back; none, or one the configuration cannot read: the empty
	// one of the type.
	ibValue value;
	if (const ibDataNode* const written = act.FindChild(ibProtocol::ibRequest::ibListSettings::Value)) {
		try {
			value = metaData->Deserialize(*written);
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

// THE WINDOW'S QUESTION — what the door tells it, the setting it edits, and `error` — why the one it gave back was refused.
ibDataNode SettingsRequest(const ibDataNode& question, const ibSettingsDescription& shown, const wxString& error)
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

bool ibRequestSettings(const ibDataNode& question, ibSettingsDescription& edited, const ibMetaData* metaData)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	// A setting refused is asked again AS IT WAS GIVEN, with why — the person's edits stay in the window.
	ibSettingsDescription shown = edited;
	wxString error;
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
		if (response.FindField(ibProtocol::ibRequest::ibListSettings::Act) != nullptr) {
			const ibProtocol::ibRequest::ibListSettings::ibAct act = static_cast<ibProtocol::ibRequest::ibListSettings::ibAct>(response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibListSettings::Act));
			request = SettingsRequest(question, shown, error);
			if (act == ibProtocol::ibRequest::ibListSettings::ibAct::Expand) {
				std::vector<ibMetaID> targets;
				if (const ibDataValue* const listed = response.FindField(ibProtocol::ibRequest::ibListSettings::Targets)) {
					if (listed->Kind() == ibDataKind::Array)
						for (const ibDataValue& id : listed->AsArray())
							targets.push_back(static_cast<ibMetaID>(id.AsInt()));
				}
				WriteTargetFields(request.Child(ibProtocol::ibRequest::ibListSettings::Expanded), request.Child(ibProtocol::ibRequest::ibListSettings::References),
					targets, metaData);
			}
			else if (act == ibProtocol::ibRequest::ibListSettings::ibAct::Choose) {
				ChooseValue(response, metaData, choosing);
				if (choosing != nullptr)
					choosing->WriteChosen(request);
			}
			continue;
		}

		const ibDataNode* const settings = response.FindChild(ibProtocol::ibRequest::ibListSettings::Settings);
		if (settings == nullptr)
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
