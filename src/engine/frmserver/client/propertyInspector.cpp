#include "propertyInspector.h"

#include <algorithm>

#include "core/typeName.h"   // ibTypeInfoName — a property's class, named as the client registers it

#include "backend/metaData.h"   // what a type's property may hold, named as its configuration names it
#include "backend/objCtor.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaModuleObject.h"      // ibPropertyInnerModule — a module its owner holds
#include "backend/metaCollection/metaObjectComposite.h"   // ibPropertyContainer — an inner attribute, a table
#include "backend/metaCollection/partial/chartOfAccountsDimensionKindsTable.h"
#include "backend/metaCollection/partial/chartOfCalculationTypesRelationTable.h"
#include "backend/propertyManager/property/propertyType.h"   // ibPropertyType — a type description, and what it may hold
#include "backend/propertyManager/property/propertyDynamicSource.h"   // ibPropertyDynamicSource — a list's main table
#include "backend/query/queryableFactory.h"                              // …picked from the sources the configuration has
#include "backend/query/queryable.h"
#include "backend/propertyManager/property/propertyCalcSchedule.h"   // ibPropertyCalcSchedule — a register's schedule, and its lists
#include "backend/propertyManager/property/propertyChoiceLink.h"     // ibPropertyChoiceLink / …Parameters, ibFieldReferenceTypes
#include "backend/propertyManager/property/variant/variantChoiceLink.h"   // ibChoiceHolderName, ibChoiceParametersForSource
#include "core/serialize/dataBuilder.h"

#include "clientFrame.h"   // ibClientFrame::SendPicture — a type's, a choice's picture, by its id

namespace {

// A PROPERTY THAT CARRIES COMPOSITION — an inner attribute (Ref, Code, Description, …), a table — not a value to edit:
// the desktop's grid has no editor for it (advpropModule.cpp, RegisterNoEditor) and draws no row, nor does this.
// ibPropertyContainer is a template with no non-template base, so each instantiation is named, as there.
bool IsComposition(const ibProperty* property)
{
	return dynamic_cast<const ibPropertyContainer<>*>(property) != nullptr
		|| dynamic_cast<const ibPropertyContainer<ibValueMetaObjectAccountDimensionKindsTable>*>(property) != nullptr
		|| dynamic_cast<const ibPropertyContainer<ibValueMetaObjectCalculationTypeRelationTable>*>(property) != nullptr;
}

// THE MODULE AN INNER MODULE'S PROPERTY HOLDS — opened from the desktop's grid by a link (advpropModule.cpp,
// ibPGHyperLinkProperty), not edited; none for any other property. A template too, each instantiation named as there.
ibValueMetaObject* GetInnerModule(const ibBackendProperty* property)
{
	if (const auto* module = dynamic_cast<const ibPropertyInnerModule<ibValueMetaObjectModule>*>(property))
		return module->GetMetaObject();
	if (const auto* module = dynamic_cast<const ibPropertyInnerModule<ibValueMetaObjectManagerModule>*>(property))
		return module->GetMetaObject();
	return nullptr;
}

// A CALCULATION REGISTER'S SCHEDULE — the lists its sub-rows choose from, for the register bound now, as the desktop's
// cell asked them of the property (ibPGCalcScheduleProperty): Kind 1 the resources that may be the value, 2 the
// dimensions that may be the date, 3 each dimension to link, its children the fields that may answer for it. Optional:
// a client that reads none shows the register's choice alone.
void AppendCalcSchedule(ibDataNode& row, const ibPropertyCalcSchedule* property)
{
	const ibCalcScheduleDescription& desc = property->GetValueAsScheduleDesc();
	auto lists = std::make_shared<ibDataNode>();
	const auto append = [](ibDataNode& parent, s32 kind, const ibPropertyChoiceList& list) {
		for (unsigned int index = 0; index < list.GetCount(); ++index) {
			ibDataNode& item = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
			if (kind != 0)
				item.SetValue(ibProtocolName::Kind, kind);
			item.SetValue(ibProtocolName::Id, static_cast<s32>(list.GetId(index)));
			item.SetValue(ibProtocolName::Title, list.GetName(index));   // names, as the desktop's cell named them
		}
	};

	ibPropertyChoiceList values, dates, links;
	property->GetScheduleValueList(desc, values);
	append(*lists, 1, values);
	property->GetScheduleDateList(desc, dates);
	append(*lists, 2, dates);
	property->GetScheduleLinkList(desc, links);
	for (unsigned int index = 0; index < links.GetCount(); ++index) {
		ibDataNode& link = lists->AddChild(0, static_cast<ibMetaID>(lists->Children().size() + 1));
		link.SetValue(ibProtocolName::Kind, static_cast<s32>(3));
		link.SetValue(ibProtocolName::Id, static_cast<s32>(links.GetId(index)));
		link.SetValue(ibProtocolName::Title, links.GetName(index));
		ibPropertyChoiceList fields;
		property->GetScheduleFieldList(links.GetId(index), fields);
		append(link, 0, fields);
	}
	row.SetProperty(ibProtocolName::Schedule, ibDataValue::Child(lists));
}

// WHAT A FIELD'S CHOICE WINDOWS ASK OF THE CONFIGURATION — the desktop's link-by-type and choice-parameters windows
// (frontend/win/dlgs/choiceLink, choiceParameters) asked these of the property at the moment they opened; the client
// has no configuration to ask. Optional: a client that reads none opens the link's window without the question of
// which type it decides, and says the parameters' window is not here.
//
// Targets — the reference types of the field (ibFieldReferenceTypes): Id (the type's metaobject), Title, Type (the
// class its reference is registered under, as a link keeps it), Picture; for the parameters, each its children — the
// fields a row may fill (GetParameterList): Id, Title, Picture.
void AppendChoiceTargets(ibDataNode& row, const ibBackendProperty* property, const ibPropertyChoiceParameters* parameters)
{
	const ibPropertyObject* const owner = property->GetPropertyObject();
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;

	const auto append = [](ibDataNode& parent, const ibPropertyChoiceList& list, unsigned int index) -> ibDataNode& {
		ibDataNode& item = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
		item.SetValue(ibProtocolName::Id, static_cast<s32>(list.GetId(index)));
		item.SetValue(ibProtocolName::Title, list.GetName(index));   // names, as the desktop's windows named them
		const ibPictureDescription& picture = list.GetPicture(index);
		if (!picture.IsEmptyPicture())
			item.SetValue(ibProtocolName::Picture, ibClientFrame::SendPicture(picture));
		return item;
	};

	auto targets = std::make_shared<ibDataNode>();
	ibPropertyChoiceList types;
	ibFieldReferenceTypes(owner, types);
	for (unsigned int index = 0; index < types.GetCount(); ++index) {
		ibDataNode& target = append(*targets, types, index);
		const ibClassID clsid = metaData != nullptr ? metaData->GetIDObjectFromMetaID(
			static_cast<ibMetaID>(types.GetId(index)), ibCtorObjectMetaType::ibCtorObjectMetaType_Reference) : 0;
		target.SetValue(ibProtocolName::Type, wxString::Format(wxT("%llu"), static_cast<unsigned long long>(clsid)));
		if (parameters == nullptr || clsid == 0)
			continue;
		ibPropertyChoiceList fields;
		parameters->GetParameterList(clsid, fields);
		for (unsigned int field = 0; field < fields.GetCount(); ++field)
			append(target, fields, field);
	}
	row.SetProperty(ibProtocolName::Targets, ibDataValue::Child(targets));

	if (parameters == nullptr)
		return;

	// Sources — the fields standing beside it that may supply a value (GetSourceList), Title the holder they stand in
	// (ibChoiceHolderName); each Id, Title, Picture, its children (Id) the parameters it could fill, the likeliest first
	// (ibChoiceParametersForSource), and Default the one that is obvious — none: a real choice.
	auto sources = std::make_shared<ibDataNode>();
	sources->SetValue(ibProtocolName::Title, ibChoiceHolderName(owner));
	ibPropertyChoiceList neighbours;
	parameters->GetSourceList(neighbours);
	for (unsigned int index = 0; index < neighbours.GetCount(); ++index) {
		ibDataNode& source = append(*sources, neighbours, index);
		std::vector<ibMetaID> fits;
		ibMetaID obvious = 0;
		ibChoiceParametersForSource(owner, static_cast<ibMetaID>(neighbours.GetId(index)), fits, &obvious);
		if (obvious != 0)
			source.SetValue(ibProtocolName::Default, static_cast<s32>(obvious));
		for (const ibMetaID& fit : fits) {
			ibDataNode& item = source.AddChild(0, static_cast<ibMetaID>(source.Children().size() + 1));
			item.SetValue(ibProtocolName::Id, static_cast<s32>(fit));
		}
	}
	row.SetProperty(ibProtocolName::Sources, ibDataValue::Child(sources));
}

// WHAT A TYPE'S PROPERTY MAY HOLD — the desktop's picker's offer (ibShowTypeSelector): the types the configuration offers
// a field of its shape, and those the value holds already — an editor does not drop what it was opened on; each by its
// class id, named and drawn as it is offered. Several at once, but in a table's shape, which takes one.
void AppendTypes(ibDataNode& row, const ibPropertyType* property)
{
	const ibSelectorDataType filter = property->GetFilterDataType();
	const ibBackendTypeConfigFactory* const factory =
		dynamic_cast<const ibBackendTypeConfigFactory*>(property->GetPropertyObject());
	const ibMetaData* const metaData = factory != nullptr ? factory->GetMetaData() : nullptr;

	std::vector<ibClassID> offered;
	ibBackendTypeConfigFactory::GetTypesByFilter(filter, metaData, offered);
	for (const ibClassID& held : property->GetValueAsTypeDesc().GetClsidList()) {
		if (std::find(offered.begin(), offered.end(), held) == offered.end())
			offered.push_back(held);
	}

	row.SetValue(ibProtocolName::Multiple, filter != ibSelectorDataType::ibSelectorDataType_table);
	for (const ibClassID& clsid : offered) {
		const ibCtorAbstractType* const so = metaData != nullptr
			? metaData->GetAvailableCtor(clsid) : ibValue::GetAvailableCtor(clsid);
		if (so == nullptr)
			continue;
		ibDataNode& type = row.AddChild(0, static_cast<ibMetaID>(row.Children().size() + 1));
		type.SetValue(ibProtocolName::Type, wxString::Format(wxT("%llu"), static_cast<unsigned long long>(clsid)));
		type.SetValue(ibProtocolName::Title, so->GetClassName());
		// Its picture is its class's: by the class id.
		type.SetValue(ibProtocolName::Picture, ibClientFrame::SendPicture(clsid));
	}
}

// WHAT A DYNAMIC LIST MAY READ — the desktop's Source cell's drop-down (ibPGDynamicSourceProperty): every source this
// configuration registers, by its table id (what the property stores), named namespace.name.
void AppendSources(ibDataNode& row, const ibPropertyDynamicSource* property)
{
	const ibPropertyObject* const owner = property->GetPropertyObject();
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	ibQueryableFactory* const factory = metaData != nullptr ? metaData->GetSourceFactory() : nullptr;
	if (factory == nullptr)
		return;
	for (ibQueryableSourceDescriptor* const descriptor : factory->GetDescriptors()) {
		if (descriptor == nullptr)
			continue;
		const ibBackendQueryable* const queryable = factory->Resolve(descriptor->GetNamespace(), descriptor->GetName());
		const ibMetaID tableId = queryable != nullptr ? queryable->GetQueryTableId() : 0;
		if (tableId == 0 || tableId == wxNOT_FOUND)
			continue;   // a source with no table id (temp, computed) cannot be stored
		ibDataNode& choice = row.AddChild(0, static_cast<ibMetaID>(row.Children().size() + 1));
		choice.SetValue(ibProtocolName::Id, static_cast<s32>(tableId));
		choice.SetValue(ibProtocolName::Title, descriptor->GetNamespace() + wxT(".") + descriptor->GetName());
	}
}

} // namespace

// ONE PROPERTY'S ROW — a property's or an event's, of any object, a form's control or a metaobject: its name and its
// class, its value as it is stored (GetNodeValue), and what it may be set to. The client shows it by its class
// (ibPropertyRegistry), knowing no property of its own.
void ibPropertyInspector::AppendProperty(ibDataNode& parent, ibBackendProperty* property)
{
	ibDataNode& row = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
	row.SetValue(ibProtocolName::Name, property->GetName());
	row.SetValue(ibProtocolName::Title, property->GetLabel());
	if (!property->GetHelp().IsEmpty())
		row.SetValue(ibProtocolName::Help, property->GetHelp());
	row.SetValue(ibProtocolName::Class, ibTypeInfoName(typeid(*property)));

	// …as it is stored: a scalar a field, a composite (a type description) a node of its own.
	const ibDataValue value = property->GetNodeValue();
	if (value.Kind() == ibDataKind::Child)
		row.SetProperty(ibProtocolName::Value, value);
	else
		row.AddField(ibProtocolName::Value, value);
	// …and what it reads as — what the client shows where it has no editor of its own for the class.
	row.SetValue(ibProtocolName::Presentation, property->GetValue().MakeString());

	// A MODULE its owner holds — opened from the desktop's grid by a link, not edited: the module itself, by its id.
	if (const ibValueMetaObject* const module = GetInnerModule(property))
		row.SetValue(ibProtocolName::Object, static_cast<s32>(module->GetMetaID()));

	// A TYPE — what it may hold, as the desktop's picker offered it.
	if (const ibPropertyType* const type = dynamic_cast<const ibPropertyType*>(property))
		AppendTypes(row, type);

	// A CALCULATION REGISTER'S SCHEDULE — the lists of its sub-rows.
	if (const ibPropertyCalcSchedule* const schedule = dynamic_cast<const ibPropertyCalcSchedule*>(property))
		AppendCalcSchedule(row, schedule);

	// A FIELD'S CHOICE — what its link-by-type and choice-parameters windows ask of the configuration.
	if (dynamic_cast<const ibPropertyChoiceLink*>(property) != nullptr)
		AppendChoiceTargets(row, property, nullptr);
	if (const ibPropertyChoiceParameters* const parameters = dynamic_cast<const ibPropertyChoiceParameters*>(property))
		AppendChoiceTargets(row, property, parameters);

	// A DYNAMIC LIST'S SOURCE — the sources it may read, as the desktop's drop-down offered them.
	if (const ibPropertyDynamicSource* const source = dynamic_cast<const ibPropertyDynamicSource*>(property))
		AppendSources(row, source);

	// AN EVENT — the parameters of its procedure, as the desktop's editor wrote the procedure's template.
	if (const ibEvent* const event = dynamic_cast<const ibEvent*>(property)) {
		std::vector<ibDataValue> args;
		for (const wxString& arg : event->GetArgs())
			args.push_back(ibDataValue::String(arg));
		if (!args.empty())
			row.AddField(ibProtocolName::Args, ibDataValue::Array(std::move(args)));
	}

	// WHAT IT MAY BE SET TO — one of them (Multiple: several at once), by its id, with its picture as the desktop's list
	// drew it beside its label (a language's icon).
	ibPropertyChoiceList choices;
	const ibPropertyChoiceMode mode = property->GetValueList(choices);
	if (mode == ibPropertyChoiceMode::Mult)
		row.SetValue(ibProtocolName::Multiple, true);
	for (unsigned int index = 0; index < choices.GetCount(); ++index) {
		ibDataNode& choice = row.AddChild(0, static_cast<ibMetaID>(index + 1));
		choice.SetValue(ibProtocolName::Id, static_cast<s32>(choices.GetId(index)));
		choice.SetValue(ibProtocolName::Title, choices.GetLabel(index));
		const ibPictureDescription& picture = choices.GetPicture(index);
		if (!picture.IsEmptyPicture())
			choice.SetValue(ibProtocolName::Picture, ibClientFrame::SendPicture(picture));
	}

	if (!property->IsEditable())
		row.SetValue(ibProtocolName::ReadOnly, true);
}

namespace {

// ONE CATEGORY'S ROWS — as the desktop's inspector laid it out (ibObjectInspector::AddItems): its properties, then its
// categories, each a row with its Title and its own; a property met before is not met again. ⭐ WHAT THE OBJECT'S STATE
// HIDES IS NOT SENT — nor a category left with nothing: the rows ride every frame, so one shown again comes back with
// it.
void AppendCategory(ibDataNode& parent, ibPropertyObject* object, const ibPropertyCategory* category,
	const std::set<const ibProperty*>& hidden, std::set<wxString>& met)
{
	for (unsigned int index = 0; index < category->GetPropertyCount(); ++index) {
		ibProperty* const property = object->GetProperty(category->GetPropertyName(index));
		if (property == nullptr || IsComposition(property) || hidden.find(property) != hidden.end()
			|| !met.insert(property->GetName()).second)
			continue;
		ibPropertyInspector::AppendProperty(parent, property);
	}
	// …and its events, as the desktop's grid drew them on their own page: a row each, of the event's class.
	for (unsigned int index = 0; index < category->GetEventCount(); ++index) {
		ibEvent* const event = object->GetEvent(category->GetEventName(index));
		if (event == nullptr || !met.insert(event->GetName()).second)
			continue;
		ibPropertyInspector::AppendProperty(parent, event);
	}

	for (unsigned int index = 0; index < category->GetCategoryCount(); ++index) {
		const ibPropertyCategory* const nextCategory = category->GetCategory(index);
		if (nextCategory == nullptr)
			continue;
		ibDataNode& row = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
		row.SetValue(ibProtocolName::Name, nextCategory->GetName());
		row.SetValue(ibProtocolName::Title, nextCategory->GetLabel());
		AppendCategory(row, object, nextCategory, hidden, met);
		if (row.Children().empty())
			parent.Children().pop_back();
	}
}

} // namespace

ibPropertyInspector::~ibPropertyInspector()
{
	if (ibPropertyObject* const object = GetSelectedObject())
		object->RemoveNotifier(this);
}

bool ibPropertyInspector::PropertyHidden(const ibProperty* property, bool hide)
{
	if (hide)
		m_hidden.insert(property);
	else
		m_hidden.erase(property);
	return true;
}

void ibPropertyInspector::SelectObject(ibPropertyObject* object, ibMetaID id, s32 tabId)
{
	if (ibPropertyObject* const shown = GetSelectedObject())
		shown->RemoveNotifier(this);

	m_currentSel = object;
	m_currentId = id;
	m_tabId = tabId;
	m_hidden.clear();

	// …and asked, as the desktop's grid asked it once its rows were built: what its state hides is pushed back.
	if (object != nullptr) {
		object->AddNotifier(this);
		object->OnRefresh();
	}
}

void ibPropertyInspector::SelectObject(ibValueMetaObject* object, s32 tabId)
{
	SelectObject(object, object != nullptr ? object->GetMetaID() : 0, tabId);
}

ibPropertyObject* ibPropertyInspector::GetSelectedObject() const
{
	return m_currentSel != nullptr && GetOwner() == m_currentSel ? m_currentSel : nullptr;
}

void ibPropertyInspector::OnDraw(ibDataNode& node) const
{
	ibPropertyObject* const object = GetSelectedObject();
	if (object == nullptr)
		return;

	node.SetMetaId(m_currentId);
	if (m_tabId != 0)
		node.SetValue(ibProtocolName::Tab, m_tabId);

	std::set<wxString> met;
	AppendCategory(node, object, object->GetCategory(), m_hidden, met);
}
