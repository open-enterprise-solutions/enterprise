////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider-team
//	Description : base object for property objects
////////////////////////////////////////////////////////////////////////////

#include "propertyObject.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — property node serialization

#define propBlock 0x00023456
#define eventBlock 0x00023457

////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////

bool ibBackendProperty::IsEditable() const
{
	return m_owner ? m_owner->IsEditable() : false;
}

///////////////////////////////////////////////////////////////////////////////

void ibProperty::InitProperty(ibPropertyCategory* cat, const wxVariant& value)
{
	m_owner->AddProperty(this);
	if (cat != nullptr) cat->AddProperty(this);
	DoSetValue(value);
}

void ibEvent::InitEvent(ibPropertyCategory* cat, const wxVariant& value)
{
	m_owner->AddEvent(this);
	if (cat != nullptr) cat->AddEvent(this);
	DoSetValue(value);
}

///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// copy & paste hooks over the node value. Default = the property's node value; the
// owner's CopyProperty / PasteProperty does the byte transport once via the provider.
bool ibBackendProperty::CopyNodeValue(ibDataValue& value) const
{
	return WriteNodeValue(value);
}

bool ibBackendProperty::PasteNodeValue(const ibDataValue& value)
{
	return ReadNodeValue(value);
}

// by-value convenience — delegates to the virtual out-param getter (so it dispatches
// to the property type's override) and yields the value for inline placement.
// Yields null when the getter declines (returns false).
ibDataValue ibBackendProperty::GetNodeValue() const
{
	ibDataValue value;
	if (!WriteNodeValue(value))
		return ibDataValue();
	return value;
}

///////////////////////////////////////////////////////////////////////////////

#include "backend/backend_mainFrame.h"
#include "backend/appData.h"
#include "backend/session/session.h"

ibPropertyObject::ibPropertyObject()
{
	m_category = new ibPropertyCategory(this);
}

ibPropertyObject::~ibPropertyObject()
{
	// Sever the attach graph both ways so no back-link outlives us: our attached children drop their
	// upward owner-link, and if we were attached to an owner, we leave its downward list.
	DetachAllPropertyObjects();
	if (m_attachOwner != nullptr)
		m_attachOwner->RemoveAttachedObject(this);

	// Same rule for the notifiers: a front outlives the object it shows (a reload, a deleted node, a
	// re-materialised value all kill us under a live inspector). Clearing the owner is what tells it
	// the pointer it kept is now a corpse — it can no longer ask us anything after this line.
	for (ibPropertyObjectNotifier* notifier : m_notifiers)
		if (notifier->GetOwner() == this)
			notifier->SetOwner(nullptr);
	m_notifiers.clear();

	wxDELETE(m_category);

	for (auto& property : m_properties)
		wxDELETE(property.second);

	for (auto& event : m_events)
		wxDELETE(event.second);

	// Clear dangling reference on the main frame's property slot if this
	// is the current selection. Explicit chain through appData → main
	// session → frame; null-guarded for headless (no UI). TODO: replace
	// this with an observer pattern so ibPropertyObject doesn't reach
	// the frame at destruction time.
	if (auto* frame = ibSession::CurrentFrame()) {
		if (this == frame->GetProperty()) {
			frame->SetProperty(nullptr);
		}
	}
}

wxString ibPropertyObject::GetIndentString(int indent) const
{
	wxString s;
	for (int i = 0; i < indent; i++) s += wxT(" ");
	return s;
}

ibProperty* ibPropertyObject::GetProperty(const wxString& nameParam) const
{
	std::map<wxString, ibProperty*>::const_iterator it = std::find_if(m_properties.begin(), m_properties.end(),
		[nameParam](const std::pair<wxString, ibProperty*>& pair) {
			return stringUtils::CompareString(nameParam, pair.first);
		}
	);

	if (it != m_properties.end())
		return it->second;

	// Route to an attached object — its properties appear as part of ours in the
	// inspector, but the property (its owner, its OnPropertyChanged) lives on IT.
	for (ibPropertyObject* other : m_attachedObjects)
		if (ibProperty* p = other->GetProperty(nameParam))
			return p;

	return nullptr;
}

ibProperty* ibPropertyObject::GetProperty(unsigned int idx) const
{
	assert(idx < m_properties.size());

	if (idx < m_properties.size()) {
		std::map<wxString, ibProperty*>::const_iterator iterator = m_properties.begin();
		std::advance(iterator, idx);
		return iterator->second;
	}

	return nullptr;
}

ibEvent* ibPropertyObject::GetEvent(const wxString& nameParam) const
{
	std::map<wxString, ibEvent*>::const_iterator it = std::find_if(m_events.begin(), m_events.end(),
		[nameParam](const std::pair<wxString, ibEvent*>& pair) {
			return stringUtils::CompareString(nameParam, pair.first);
		}
	);

	if (it != m_events.end())
		return it->second;

	//LogDebug("[ibPropertyObject::GetEvent] ibEvent " + name + " not found!");
	return nullptr;
}

ibEvent* ibPropertyObject::GetEvent(unsigned int idx) const
{
	assert(idx < m_events.size());

	std::map<wxString, ibEvent*>::const_iterator it = m_events.begin();
	unsigned int i = 0;
	while (i < idx && it != m_events.end()) {
		i++; it++;
	}

	if (it != m_events.end())
		return it->second;

	return nullptr;
}

void ibPropertyObject::AddProperty(ibProperty* prop)
{
	m_properties.emplace(std::map<wxString, ibProperty*>::value_type(prop->GetName(), prop));
}

void ibPropertyObject::AddEvent(ibEvent* event)
{
	m_events.emplace(std::map<wxString, ibEvent*>::value_type(event->GetName(), event));
}

void ibPropertyObject::AddNotifier(ibPropertyObjectNotifier* notifier)
{
	if (notifier == nullptr)
		return;
	m_notifiers.push_back(notifier);
	notifier->SetOwner(this);
}

void ibPropertyObject::RemoveNotifier(ibPropertyObjectNotifier* notifier)
{
	if (notifier == nullptr)
		return;
	m_notifiers.erase(
		std::remove(m_notifiers.begin(), m_notifiers.end(), notifier),
		m_notifiers.end());
	if (notifier->GetOwner() == this)
		notifier->SetOwner(nullptr);
}

bool ibPropertyObject::HideProperty(const ibProperty* property, bool hide)
{
	if (property == nullptr)
		return false;
	bool result = false;
	for (ibPropertyObjectNotifier* notifier : m_notifiers)
		if (notifier->PropertyHidden(property, hide))
			result = true;
	return result;
}

void ibPropertyObject::AttachPropertyObject(ibPropertyObject* other)
{
	if (other == nullptr || other == this)
		return;
	m_attachedObjects.push_back(other);
	other->m_attachOwner = this;   // upward back-link: a change in the attached object bubbles up to us
}

void ibPropertyObject::DetachAllPropertyObjects()
{
	for (ibPropertyObject* other : m_attachedObjects)
		if (other != nullptr && other->m_attachOwner == this)
			other->m_attachOwner = nullptr;   // drop the back-link so it never dangles past us
	m_attachedObjects.clear();
}

void ibPropertyObject::RemoveAttachedObject(ibPropertyObject* other)
{
	if (other == nullptr)
		return;
	m_attachedObjects.erase(
		std::remove(m_attachedObjects.begin(), m_attachedObjects.end(), other),
		m_attachedObjects.end());
	if (other->m_attachOwner == this)
		other->m_attachOwner = nullptr;
}

bool ibPropertyObject::ReadProperty(const ibDataNode& node)
{
	// Base default: route through attached objects — their data is part of us. A type
	// with own data overrides, does its own, then calls this base to include attached.
	for (ibPropertyObject* other : m_attachedObjects)
		other->ReadProperty(node);
	return true;
}

bool ibPropertyObject::WriteProperty(ibDataNode& node) const
{
	for (ibPropertyObject* other : m_attachedObjects)
		other->WriteProperty(node);
	return true;
}

unsigned int ibPropertyObject::GetPropertyIndex(const wxString& nameParam) const {
	return std::distance(m_properties.begin(),
		std::find_if(m_properties.begin(), m_properties.end(),
			[nameParam](const  std::pair<wxString, ibProperty*>& pair) {
				return stringUtils::CompareString(nameParam, pair.first);
			}
		)
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Copy/paste build ONE node tree (props + events as named values under two child
// sub-nodes) and let the binary provider do the single byte transport to/from the
// clipboard. No per-property byte writer — each property yields its node value.
bool ibPropertyObject::CopyProperty(ibWriterMemory& writer) const
{
	ibDataBuilder builder;

	ibDataNode& propsNode = builder.Root().Child(wxT("props"));
	for (unsigned int idx = 0; idx < GetPropertyCount(); idx++) {
		ibProperty* prop = GetProperty(idx);
		wxASSERT(prop);
		ibDataValue value;
		if (!prop->CopyNodeValue(value))
			return false;
		propsNode.SetProperty(prop->GetName(), value);
	}

	ibDataNode& eventsNode = builder.Root().Child(wxT("events"));
	for (unsigned int idx = 0; idx < GetEventCount(); idx++) {
		ibEvent* event = GetEvent(idx);
		wxASSERT(event);
		ibDataValue value;
		if (!event->CopyNodeValue(value))
			return false;
		eventsNode.SetProperty(event->GetName(), value);
	}

	return builder.Save(ibBinaryProvider(), writer);
}

bool ibPropertyObject::PasteProperty(ibReaderMemory& reader)
{
	ibDataBuilder builder;
	if (!builder.Load(ibBinaryProvider(), reader))
		return false;

	if (const ibDataNode* propsNode = builder.Root().FindChild(wxT("props"))) {
		for (unsigned int idx = 0; idx < GetPropertyCount(); idx++) {
			ibProperty* prop = GetProperty(idx);
			wxASSERT(prop);
			if (!prop->PasteNodeValue(propsNode->GetProperty(prop->GetName())))
				return false;
		}
	}
	if (const ibDataNode* eventsNode = builder.Root().FindChild(wxT("events"))) {
		for (unsigned int idx = 0; idx < GetEventCount(); idx++) {
			ibEvent* event = GetEvent(idx);
			wxASSERT(event);
			if (!event->PasteNodeValue(eventsNode->GetProperty(event->GetName())))
				return false;
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibPropertyCategory::AddProperty(ibProperty* property)
{
	m_properties.emplace_back(property->GetName());
}

ibPropertyCategory* ibPropertyCategory::GetCategory(unsigned int index) const
{
	if (index < m_categories.size()) return m_categories[index];
	index -= m_categories.size();
	// Attached: flatten the attached objects' OWN sub-categories straight in (skip their
	// hidden "property and event" root), so they show under us without that wrapper.
	if (this == m_owner->GetCategory()) {
		for (ibPropertyObject* other : m_owner->GetAttachedObjects()) {
			ibPropertyCategory* root = other->GetCategory();
			const unsigned int cnt = root->GetCategoryCount();
			if (index < cnt) return root->GetCategory(index);
			index -= cnt;
		}
	}
	// Out of range. Returning a fresh category here leaked it (nothing owns it — the
	// dtor only sweeps m_categories) AND hid the bad index behind an empty stand-in.
	// Null says what happened; GetCategoryCount is the bound callers walk.
	return nullptr;
}

unsigned int ibPropertyCategory::GetCategoryCount() const
{
	unsigned int n = (unsigned int)m_categories.size();
	if (this == m_owner->GetCategory())
		for (ibPropertyObject* other : m_owner->GetAttachedObjects())
			n += other->GetCategory()->GetCategoryCount();
	return n;
}

void ibPropertyCategory::AddEvent(ibEvent* event)
{
	m_events.emplace_back(event->GetName());
}

void ibPropertyCategory::AddCategory(ibPropertyCategory* cat)
{
	m_categories.emplace_back(cat);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
