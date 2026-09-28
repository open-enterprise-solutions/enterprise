#ifndef __META_EVENT_HANDLER_OBJECT_H__
#define __META_EVENT_HANDLER_OBJECT_H__

// AN EVENT HANDLER — one event of the types it names, handled by code of its own.
//
// SOURCE says who raises it: an object, a manager, a record set — of one metaobject
// (`DocumentObject.Invoice`) or of every metaobject of a metatype (`DocumentObject`, the family).
// EVENT is one of the events those types raise, offered from what they declare: the procedures their
// own modules are prepared with (SetDefaultProcedure — `BeforeWrite(Cancel)`, `Posting(Cancel,
// PostingMode)`…). For several types, only an event every one of them raises, with as many arguments.
//
// The procedure lives in the handler's OWN module, as a job's does in its job module — the handler
// is the whole of it, and nothing else in the configuration has to be edited to add one. The module
// is prepared with that one procedure, named as the event and taking `Source` — the value that
// raised it — before the event's own arguments:
//     Procedure BeforeWrite(Source, Cancel)
// A choice of another event prepares that one instead.
//
// THE PLATFORM CALLS IT wherever the event is raised, through the owner's one door
// (ibRuntimeModuleDataObject::ExecAsEvent): the type's own module first, then every handler of the
// event, with the same arguments — a `Cancel` set here cancels as one set there does. A procedure
// takes as many arguments as it declares (ibProcUnit::CallAsProc).

#include "metaModuleObject.h"   // ibPropertyInnerModule + ibValueMetaObjectManagerModule
#include "backend/backend_type.h"
#include "backend/propertyManager/property/propertyType.h"   // Source
#include "backend/propertyManager/property/propertyList.h"   // Event

#include <map>
#include <vector>

// THE KINDS OF TYPE THAT RAISE EVENTS — an object, a manager, a record set: what a handler's Source is made
// of, and what its filter offers (ibBackendTypeConfigFactory::GetTypesByFilter).
constexpr ibCtorObjectMetaType g_eventSourceKinds[] = {
	ibCtorObjectMetaType::ibCtorObjectMetaType_Object,
	ibCtorObjectMetaType::ibCtorObjectMetaType_Manager,
	ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet,
};

class BACKEND_API ibValueMetaObjectEventHandler : public ibValueMetaObject, public ibBackendTypeConfigFactory {
public:

	ibValueMetaObjectEventHandler(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	// WHO RAISES IT — the types, a family among them.
	const ibTypeDescription& GetSource() const { return m_propertySource->GetValueAsTypeDesc(); }

	// The module the procedure lives in. A manager module, so it is registered and compiled with the
	// session's modules, and the platform finds it the way it finds any other.
	const ibValueMetaObjectManagerModule* GetHandlerModule() const { return m_propertyHandlerModule->GetMetaObject(); }

	// ⭐ DOES IT HANDLE THIS EVENT OF A VALUE OF THIS TYPE — the question the door asks, on every
	// event raised. The event by its number (EventId — the door's, made once per event, not once per
	// handler), the type by the source's own gates — a family admits its members
	// (ibValueTypeDescription::AllowValue).
	bool Handles(const ibClassID& sourceType, long eventId) const;

	// An event's number — what the Event property keeps. The name's own hash, so it is the same
	// whichever type the event is asked of, and it does not move when the list of events around it
	// changes with the source.
	static long EventId(const ibString& eventName);

	// One event the source raises: the procedure's shape its own module is prepared with.
	struct ibSourceEvent {
		ibContentHelper       m_kind = ibContentHelper::eProcedureHelper;
		std::vector<wxString> m_args;   // the event's own arguments, without Source
	};

	// THE EVENTS THE SOURCE RAISES, by name — for several types, those every one of them raises by the same
	// name and with as many arguments. Asked of each type's event module (ibCtorMetaValueType::GetEventModule).
	std::map<wxString, ibSourceEvent> GetSourceEvents() const;

	// --- ibBackendTypeConfigFactory: the Source property's type resolves through its owner (as a
	// command's parameter type does). The filter offers what raises events. ---
	virtual ibTypeDescription& GetTypeDesc() const override { return m_propertySource->GetValueAsTypeDesc(); }
	virtual ibSelectorDataType GetFilterDataType() const override { return ibSelectorDataType::ibSelectorDataType_eventSource; }
	virtual const ibMetaData* GetMetaData() const override { return m_metaData; }
	virtual ibMetaData* GetMetaData() override { return m_metaData; }

	// Hosts no children — a handler is a choice plus its code.
	virtual ibClassID ResolveChild(const ibClassID&) const override { return 0; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	// designer context menu — "Open handler module", as a job's module is opened.
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items) override;

	// A new source or a new event prepares the module for the procedure that now applies.
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	//lifecycle — forward to the inner module (its own registration)
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// The Event property's choices — the source's events.
	bool FillEventList(ibPropertyList* prop);

	// The module prepared with the procedure the chosen event takes.
	void PrepareModule();

private:

	ibPropertyCategory* m_categoryHandler = ibPropertyObject::CreatePropertyCategory(wxT("Handler"), _("Event handler"));
	ibPropertyType* m_propertySource = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryHandler, wxT("Source"), _("Source"),
		_("What raises the event: the objects, managers or record sets of one metaobject, or of every metaobject of a kind (DocumentObject - every document's object). Several may be named; the event is then one every one of them raises."), ibValueTypes::TYPE_EMPTY);
	ibPropertyList* m_propertyEvent = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryHandler, wxT("Event"), _("Event"),
		_("Which of the source's events is handled - the list is what the source raises. The handler module is prepared with its procedure, named as the event and taking Source first."), &ibValueMetaObjectEventHandler::FillEventList);

	// The handler's module — a MANAGER module (registered, compiled with the session), as a job's is.
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyHandlerModule =
		ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(
			m_categoryContext, wxT("HandlerModule"), _("Handler module"), _("The handler's code: a procedure named as the event, taking Source - the value that raised it - before the event's own arguments. Called after the source's own module has handled the event, with the same arguments."));

	friend class ibMetaData;
};

#endif // !__META_EVENT_HANDLER_OBJECT_H__
