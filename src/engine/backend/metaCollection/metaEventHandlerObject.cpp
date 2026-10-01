////////////////////////////////////////////////////////////////////////////
//	Description : event handler — one event of the types it names, handled by code of its own
////////////////////////////////////////////////////////////////////////////

#include "metaEventHandlerObject.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"                    // ibCtorMetaValueType::GetEventModule
#include "backend/system/value/valueType.h"     // ibValueTypeDescription::AllowValue — the source's gates
#include "backend/backend_picture.h"            // ibBackendPicture::GetPicture — an event's picture in the Event list
#include "backend/picturePredefined.h"          // g_picEventCLSID

//***********************************************************************
//*                            Event handler                            *
//***********************************************************************

ibValueMetaObjectEventHandler::ibValueMetaObjectEventHandler(const wxString& name, const wxString& synonym, const wxString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
	// NOTHING IS HANDLED until the source is said: the property's constructor wants a type to start
	// from, and any type there would be a source.
	m_propertySource->SetValue(ibTypeDescription());
}

//***********************************************************************
//*                    who raises it, and which event                   *
//***********************************************************************

long ibValueMetaObjectEventHandler::EventId(const ibString& eventName)
{
	// A long is what a list property keeps; the hash is 64 bits and the id a non-negative 31 — wxNOT_FOUND
	// stays free for "none". Over the name's UTF-8, the bytes ib_clsid_hash has always read: the ids already
	// kept in configurations do not move.
	return static_cast<long>(ib_clsid_hash(eventName.ToUtf8().c_str()) & 0x7FFFFFFF);
}

bool ibValueMetaObjectEventHandler::Handles(const ibClassID& sourceType, long eventId) const
{
	// The number the property keeps, as it is kept: the door asks this on every event raised, and the
	// property's own reading refills its list from the source first.
	const wxVariant& event = m_propertyEvent->GetValue();
	if (event.IsNull() || event.GetLong() != eventId)
		return false;
	return ibValueTypeDescription::AllowValue(GetSource(), sourceType, m_metaData);
}

std::map<wxString, ibValueMetaObjectEventHandler::ibSourceEvent> ibValueMetaObjectEventHandler::GetSourceEvents() const
{
	std::map<wxString, ibSourceEvent> events;
	if (m_metaData == nullptr || !GetSource().IsOk())
		return events;

	// EVERY TYPE THE SOURCE ADMITS, asked by the source's own gates — a family admits each of its members,
	// so `DocumentObject` reaches every document without anybody listing them.
	bool first = true;
	for (const ibCtorObjectMetaType kind : g_eventSourceKinds) {
		for (const ibCtorMetaValueType* type : m_metaData->GetListCtorsByType(kind)) {
			if (!ibValueTypeDescription::AllowValue(GetSource(), type->GetClassType(), m_metaData))
				continue;

			std::map<wxString, ibSourceEvent> own;
			if (const ibValueMetaObjectModuleBase* const module = type->GetEventModule()) {
				for (size_t idx = 0; idx < module->GetDefaultProcedureCount(); idx++) {
					ibSourceEvent& event = own[module->GetDefaultProcedureName(idx)];
					event.m_kind = module->GetDefaultProcedureType(idx);
					event.m_args = module->GetDefaultProcedureArgs(idx);
				}
			}

			// SEVERAL TYPES: an event every one of them raises by the same name and with AS MANY ARGUMENTS. They
			// are handed over by position, so a procedure written for one shape would read another type's third
			// argument as its own — a document's BeforeWrite passes WriteMode where a sequence's passes Replacing.
			// Such an event is dropped rather than offered.
			if (first) {
				events = std::move(own);
				first = false;
				continue;
			}
			for (auto it = events.begin(); it != events.end();) {
				const auto same = own.find(it->first);
				it = same == own.end() || same->second.m_args.size() != it->second.m_args.size() ? events.erase(it) : std::next(it);
			}
		}
	}
	return events;
}

bool ibValueMetaObjectEventHandler::FillEventList(ibPropertyList* prop)
{
	const wxBitmap picture = ibBackendPicture::GetPicture(g_picEventCLSID);   // the platform's picture of an event
	for (const auto& event : GetSourceEvents())
		prop->AppendItem(event.first, EventId(event.first), picture);
	return true;
}

void ibValueMetaObjectEventHandler::PrepareModule()
{
	ibValueMetaObjectManagerModule* const module = m_propertyHandlerModule->GetMetaObject();
	if (module == nullptr)
		return;

	// ONE PROCEDURE, the one the chosen event takes — a handler is about one event, and offering the
	// others would invite code the platform never calls.
	module->ClearDefaultProcedures();

	// The chosen one among the events the source raises, by the number the property keeps: none chosen
	// (wxNOT_FOUND) or one the source no longer raises finds nothing, and the module stays empty.
	const long id = m_propertyEvent->GetValueAsInteger();
	for (const auto& event : GetSourceEvents()) {
		if (EventId(event.first) != id)
			continue;
		std::vector<wxString> args = { wxT("Source") };
		args.insert(args.end(), event.second.m_args.begin(), event.second.m_args.end());
		if (event.second.m_kind == ibContentHelper::eFunctionHelper)
			module->SetDefaultFunction(event.first, args);
		else
			module->SetDefaultProcedure(event.first, ibContentHelper::eProcedureHelper, args);
		return;
	}
}

void ibValueMetaObjectEventHandler::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (property == m_propertySource || property == m_propertyEvent)
		PrepareModule();
	ibValueMetaObject::OnPropertyChanged(property, oldValue, newValue);
}

//***********************************************************************
//*        lifecycle — forward to the inner module (own load/save)      *
//***********************************************************************

bool ibValueMetaObjectEventHandler::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	return (*m_propertyHandlerModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectEventHandler::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyHandlerModule)->OnLoadMetaObject(metaData))
		return false;
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectEventHandler::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyHandlerModule)->OnSaveMetaObject(flags))
		return false;
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectEventHandler::OnDeleteMetaObject()
{
	if (!(*m_propertyHandlerModule)->OnDeleteMetaObject())
		return false;
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectEventHandler::OnBeforeRunMetaObject(int flags)
{
	// The module registers itself into the module storage here, so the door finds it by the time an
	// event is raised.
	if (!(*m_propertyHandlerModule)->OnBeforeRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectEventHandler::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyHandlerModule)->OnAfterRunMetaObject(flags))
		return false;
	if (!ibValueMetaObject::OnAfterRunMetaObject(flags))
		return false;

	// The procedure follows the loaded choice — prepared HERE, not on load: its arguments are asked of the
	// source's types, and a handler (a common object) is loaded before the objects whose events it handles.
	PrepareModule();
	return true;
}

bool ibValueMetaObjectEventHandler::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyHandlerModule)->OnBeforeCloseMetaObject())
		return false;
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectEventHandler::OnAfterCloseMetaObject()
{
	if (!(*m_propertyHandlerModule)->OnAfterCloseMetaObject())
		return false;
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          load & save from DB                        *
//***********************************************************************

bool ibValueMetaObjectEventHandler::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyHandlerModule->SetNodeValue(node.GetProperty(m_propertyHandlerModule->GetName()));

	// 🛑 THE EVENT BEFORE THE SOURCE, and the order is the fix. The event's door reads what it holds
	// first, and a list reads that through its functor — the source's events. Asked while the
	// configuration loads, the source names types nobody has registered yet (a catalog registers in its
	// run, after every load), and a type description drops what the registry does not know: a handler
	// of `CatalogObject.Goods` came back as `String` and never fired. Read first, the event asks an
	// empty source, and the source is asked again only in the run, when its types are there.
	m_propertyEvent->SetNodeValue(node.GetProperty(m_propertyEvent->GetName()));
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));
	return true;
}

bool ibValueMetaObjectEventHandler::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyHandlerModule->GetName(), m_propertyHandlerModule->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(),        m_propertySource->GetNodeValue());
	node.SetProperty(m_propertyEvent->GetName(),         m_propertyEvent->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectEventHandler, "EventHandler", g_metaEventHandlerCLSID);
