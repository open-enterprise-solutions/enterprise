#include "docViewComposerEdit.h"

#include "backend/backend_type.h"                        // GetTypesByFilter — what a parameter may be given
#include "backend/backend_picture.h"                    // ibBackendPicture::SendPicture — a class's picture, by its id
#include "backend/metaData.h"
#include "backend/metaCollection/metaComposerObject.h"   // the composer — its description, its configuration
#include "backend/query/queryConstructorModel.h"         // ibQueryFieldsOfText — the fields its query offers
#include "backend/query/queryableFactory.h"              // a list's main table — its descriptor
#include "backend/srcDataObject.h"                       // …which describes itself (ibSourceExplorer)
#include "core/description/compositionDescription.h"     // ibCompositionDescriptionMemory — the node form
#include "core/serialize/dataBuilder.h"
#include "core/stringTokenizer.h"                        // ibSplit — the cell's declared types, as the event lists them
#include "frmserver/visualView/ctrl/frame.h"             // ibControlFrame — the cell a value is chosen for
#include "frmserver/visualView/ctrl/typeControl.h"       // ibTypeControlFactory::ChooseValue — the one road a Select walks
#include "frmserver/win/dlgs/settings/settingsRequest.h" // ibWriteSettingsField / References — a field as the windows read it
#include "queryConstructorRequest.h"                      // ibFetchQueryConstructorRequest — the query constructor over its query

#include <set>
#include <vector>

namespace {

// THE NAMES A TYPE CELL READS — the panel's Type columns (the fields', the parameters') name each class as the
// configuration does (the desktop's ibDescribeTypes, over its own configuration). The client has none: every class the
// cells may show is named here, each once — the primitive kinds always, a new parameter being a String before anybody
// chose its type (2026-10-29: every cell read <any>) — and the types a parameter may be given, when they are offered.
void WriteTypeClasses(ibDataNode& into, std::vector<ibTypeDescription> types, const std::vector<ibClassID>& offered,
	const ibMetaData* metaData)
{
	ibTypeDescription primitives;
	for (const ibValueTypes kind : { ibValueTypes::TYPE_BOOLEAN, ibValueTypes::TYPE_NUMBER, ibValueTypes::TYPE_DATE,
			ibValueTypes::TYPE_STRING })
		primitives.AppendMetaType(kind);
	types.push_back(primitives);
	std::vector<ibClassID> listed;
	for (const ibTypeDescription& type : types)
		for (const ibClassID& clsid : type.GetClsidList())
			listed.push_back(clsid);
	listed.insert(listed.end(), offered.begin(), offered.end());

	ibDataNode& named = into.Child(ibProtocol::ibView::ibComposer::Classes);
	std::set<ibClassID> seen;
	for (const ibClassID& clsid : listed) {
		if (!seen.insert(clsid).second)
			continue;
		const ibCtorAbstractType* const ctor = metaData != nullptr ? metaData->GetAvailableCtor(clsid)
			: ibValue::GetAvailableCtor(clsid);
		if (ctor == nullptr)
			continue;
		ibDataNode& entry = named.AddChild(0, static_cast<ibMetaID>(named.Children().size() + 1));
		entry.SetValue(ibProtocol::ibSchema::ibProperties::Type, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid)));
		entry.SetValue(ibProtocol::ibSchema::ibProperties::Name, ctor->GetClassName());
		entry.SetValue(ibProtocol::ibSchema::ibProperties::Picture, ibBackendPicture::SendPicture(clsid));
	}

	if (offered.empty())
		return;
	std::vector<ibDataValue> ids;
	for (const ibClassID& clsid : offered)
		ids.push_back(ibDataValue::String(ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid))));
	into.AddField(ibProtocol::ibView::ibComposer::Types, ibDataValue::Array(ids));
}

// WHAT A QUERY TEXT OFFERS — written as the settings windows' questions write it (ComposerSettings: no Id, a field of a
// parsed text stands behind no attribute), with the parser's complaint when it offers none — and the names of the
// classes its fields and the composition's parameters hold, and of those `offered` a parameter may be given.
void WriteQueryFields(ibDataNode& into, const ibString& text, const ibMetaData* metaData,
	const std::vector<ibParameterDescription>& parameters = {}, const std::vector<ibClassID>& offered = {})
{
	ibString error;
	ibDataNode& fields = into.Child(ibProtocol::ibView::ibComposer::Fields);
	std::vector<ibTypeDescription> types;
	for (const ibQueryConstructorField& field : ibQueryFieldsOfText(text, metaData, &error)) {
		ibWriteSettingsField(fields, field.m_name, field.m_caption, wxNOT_FOUND, field.m_type, field.m_available,
			ibPictureDescription(), metaData);
		types.push_back(field.m_type);
	}
	ibWriteSettingsReferences(into.Child(ibProtocol::ibView::ibComposer::References), types, metaData);
	if (!error.IsEmpty())
		into.SetValue(ibProtocol::ibView::ibComposer::Error, error);

	for (const ibParameterDescription& parameter : parameters)
		types.push_back(parameter.m_type);
	WriteTypeClasses(into, std::move(types), offered, metaData);
}


// WHAT A DYNAMIC LIST OFFERS — the desktop's list settings window's answer (ibListSettingsPanel::BindFieldSource): the
// fields of the table the list IS, and what its query adds on top; a name the table already offers is not added twice.
void WriteListSourceFields(ibDataNode& into, ibMetaID mainTable, const ibString& text, const ibMetaData* metaData)
{
	ibDataNode& fields = into.Child(ibProtocol::ibView::ibComposer::Fields);
	std::vector<ibTypeDescription> types;
	std::vector<ibString> names;

	ibQueryableFactory* const factory = metaData != nullptr ? metaData->GetSourceFactory() : nullptr;
	if (const ibQueryableSourceDescriptor* const descriptor = (factory != nullptr && mainTable != wxNOT_FOUND)
		? factory->ResolveDescriptorById(mainTable) : nullptr) {
		ibSourceDataObject::ibSourceExplorer explorer;
		descriptor->FillSourceExplorer(explorer);
		for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
			const ibSourceDataObject::ibSourceExplorer* const node = explorer.GetHelper(i);
			if (!node || node->IsTableSection())
				continue;
			ibWriteSettingsField(fields, node->GetSourceName(), node->GetSourceSynonym(),
				static_cast<ibMetaID>(node->GetSourceId()), node->GetTypeValueDesc(),
				node->GetColumn() == nullptr || node->GetColumn()->IsAvailable(), node->GetSourcePicture(), metaData);
			types.push_back(node->GetTypeValueDesc());
			names.push_back(node->GetSourceName());
		}
	}

	ibString error;
	if (!text.IsEmpty()) {
		for (const ibQueryConstructorField& field : ibQueryFieldsOfText(text, metaData, &error)) {
			bool already = false;
			for (const ibString& name : names)
				already = already || name.IsSameAs(field.m_name, false);
			if (already)
				continue;
			ibWriteSettingsField(fields, field.m_name, field.m_caption, wxNOT_FOUND, field.m_type, field.m_available,
				ibPictureDescription(), metaData);
			types.push_back(field.m_type);
		}
	}
	ibWriteSettingsReferences(into.Child(ibProtocol::ibView::ibComposer::References), types, metaData);
	if (!error.IsEmpty())
		into.SetValue(ibProtocol::ibView::ibComposer::Error, error);
}

// THE CELL A VALUE IS CHOSEN FOR, on the panel — a parameter's value, a condition's right side: what the server's own road
// (ibTypeControlFactory::ChooseValue) hands the value to. In the designer that road is the declared-value window
// (ibShowPredefinedSelector), which asks the client and has its answer by the time the road returns — so the value is
// read off here, and nothing outlives the event. Counted, as every owner a choice is made for is.
class ibComposerValueChoice : public ibControlFrame, public ibTypeControlFactory {
public:

	ibComposerValueChoice(const ibMetaData* metaData, const ibTypeDescription& declared, const ibValue& value)
		: m_metaData(metaData), m_typeDesc(declared), m_value(value) {
	}

	bool IsChosen() const { return m_chosen; }
	const ibValue& GetChosen() const { return m_value; }

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
	}

	// ibTypeControlFactory — no column behind the cell: its type is the one the panel declares for it.
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const override { return nullptr; }
	virtual ibSourceObject* GetSourceObject() const override { return nullptr; }
	virtual ibSourceDescription& GetSourceDesc() const override { return m_sourceDesc; }
	virtual const ibMetaData* GetMetaData() const override { return m_metaData; }
	virtual ibTypeDescription& GetTypeDesc() const override { return m_typeDesc; }

private:

	const ibMetaData*           m_metaData;
	mutable ibTypeDescription   m_typeDesc;
	mutable ibSourceDescription m_sourceDesc;
	ibValue                     m_value;
	const ibGuid                m_guid = ibGuid::newGuid();
	unsigned                    m_refCount = 0;
	bool                        m_chosen = false;
};

} // namespace

// ----------------------------------------------------------------------------
// ibComposerCompositionView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComposerCompositionView, ibMetaView);

void ibComposerCompositionView::OnDraw(ibDataNode& frame)
{
	const ibValueMetaObjectComposer* const metaComposer =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectComposer>();
	if (metaComposer == nullptr)
		return;

	// Read-only where the configuration may not be edited — the desktop's panel took SetReadOnly(ibDOC_READONLY).
	ibDataNode& state = frame.Child(ibProtocol::ibView::State);
	state.SetValue(ibProtocol::ibView::ibComposer::ReadOnly, !metaComposer->IsEditable());

	const ibCompositionDescription& composition = metaComposer->GetCompositionDesc();
	ibCompositionDescriptionMemory::WriteNode(frame.Child(ibProtocol::ibView::ibComposer::Composition), composition);
	// …AND WHAT A PARAMETER MAY BE GIVEN — the desktop's type picker over a parameter offered what a reference-shaped
	// field may hold (GetTypesByFilter); the client's picker shows only what the server offered, and was offered
	// nothing (2026-10-29: its "..." opened no window). Said once, with the view: a fetch of the fields does not repeat it.
	std::vector<ibClassID> offered;
	ibBackendTypeConfigFactory::GetTypesByFilter(ibSelectorDataType::ibSelectorDataType_reference,
		metaComposer->GetMetaData(), offered);
	WriteQueryFields(frame, composition.m_query, metaComposer->GetMetaData(), composition.m_parameters, offered);

	// …and the last value chosen for a cell of the panel (Select), under the number its choice was asked by.
	if (m_choice != 0) {
		ibDataNode& chosen = state.Child(ibProtocol::ibView::ibComposer::Chosen);
		chosen.SetValue(ibProtocol::ibView::ibComposer::Choice, m_choice);
		ibDataNode& value = chosen.Child(ibProtocol::ibView::ibComposer::Value);
		m_chosen.Serialize(value);
		// …with how it reads, said by the configuration — the client has none to say it (as a settings window's Chosen).
		value.SetValue(ibProtocol::ibRequest::ibListSettings::Presentation, ibString(m_chosen.GetString()));
	}
}

bool ibComposerCompositionView::Fetch(const ibDataNode& request, ibDataNode& response)
{
	const ibValueMetaObjectComposer* const metaComposer =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectComposer>();
	return metaComposer != nullptr
		&& (ibFetchQueryConstructorRequest(request, response, metaComposer->GetMetaData())
		    || ibFetchComposerRequest(request, response, metaComposer->GetMetaData()));
}

bool ibComposerCompositionView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	ibMetaDocument* const document = GetDocument();
	ibValueMetaObjectComposer* const metaComposer = document->ConvertMetaObjectToType<ibValueMetaObjectComposer>();
	if (metaComposer == nullptr || !metaComposer->IsEditable())
		return false;

	switch (event) {
	case ibProtocol::ibEvent::ibKind::Input:
		document->Modify(true);
		return true;
	case ibProtocol::ibEvent::ibKind::Change: {
		const auto node = args.FindChild(ibProtocol::ibView::ibComposer::Composition);
		ibCompositionDescription composition;
		if (!node || !ibCompositionDescriptionMemory::ReadNode(*node, composition))
			return false;
		// Where the desktop's panel wrote — the composer's own description — and the configuration modified
		// (ibComposerEditor::MarkModified).
		metaComposer->SetCompositionDesc(composition);
		document->Modify(true);
		return true;
	}
	case ibProtocol::ibEvent::ibKind::Select: {
		// A CELL'S "..." — a parameter's value, a condition's right side: the desktop's panel chose it through the
		// configuration (ChooseValue → the declared-value window), which is here. The cell's declared types and the
		// value in it come with the event; the value chosen goes back with the view (State Chosen), under its number.
		const ibMetaData* const metaData = metaComposer->GetMetaData();
		ibTypeDescription declared;
		for (const ibString& written : ibSplit(args.GetValue<ibString>(ibProtocol::ibView::ibComposer::Type), ibT(','))) {
			unsigned long long clsid = 0;
			if (written.ToULongLong(&clsid) && clsid != 0)
				declared.AppendMetaType(static_cast<ibClassID>(clsid));
		}
		ibValue value;
		if (const auto written = args.FindChild(ibProtocol::ibView::ibComposer::Value)) {
			try {
				value = metaData->Deserialize(*written);
			}
			catch (const ibCoreException&) {
			}
		}
		// One type and nothing in the cell: it stands on that type's empty value, as the desktop's cell did.
		if (value.IsEmpty() && declared.GetClsidCount() == 1 && metaData->IsRegisterCtor(declared.GetByIdx(0)))
			value = metaData->CreateObject(declared.GetByIdx(0));

		ibComposerValueChoice* const choosing = new ibComposerValueChoice(metaData, declared, value);
		choosing->ControlIncrRef();
		ibTypeControlFactory::ChooseValue(choosing);
		if (choosing->IsChosen()) {
			m_choice = args.GetValue<std::int32_t>(ibProtocol::ibView::ibComposer::Choice);
			m_chosen = choosing->GetChosen();
		}
		choosing->ControlDecrRef();
		return true;
	}
	default:
		return false;
	}
}

// ----------------------------------------------------------------------------
// ibComposerCompositionDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComposerCompositionDocument, ibMetaDocument);

// ----------------------------------------------------------------------------
// ibFetchComposerRequest
// ----------------------------------------------------------------------------

bool ibFetchComposerRequest(const ibDataNode& request, ibDataNode& response, const ibMetaData* metaData)
{
	switch (static_cast<ibProtocol::ibView::ibComposer::ibRequest>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibComposer::Kind))) {
	case ibProtocol::ibView::ibComposer::ibRequest::Fields:
		WriteQueryFields(response, request.GetValue<ibString>(ibProtocol::ibView::ibComposer::Text), metaData);
		return true;
	case ibProtocol::ibView::ibComposer::ibRequest::Expand: {
		std::vector<ibMetaID> targets;
		if (const auto listed = request.FindField(ibProtocol::ibView::ibComposer::Targets)) {
			if (listed->Kind() == ibDataKind::Array)
				for (const ibDataValue& id : listed->AsArray())
					targets.push_back(static_cast<ibMetaID>(id.AsInt()));
		}
		ibWriteSettingsTargetFields(response.Child(ibProtocol::ibView::ibComposer::Expanded), response.Child(ibProtocol::ibView::ibComposer::References),
			targets, metaData);
		return true;
	}
	case ibProtocol::ibView::ibComposer::ibRequest::Source:
		WriteListSourceFields(response, request.Has(ibProtocol::ibView::ibComposer::Id)
			? static_cast<ibMetaID>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibComposer::Id)) : static_cast<ibMetaID>(wxNOT_FOUND),
			request.GetValue<ibString>(ibProtocol::ibView::ibComposer::Text), metaData);
		return true;
	default:
		return false;
	}
}
