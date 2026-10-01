////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider-team
//	Description : base object for property objects
////////////////////////////////////////////////////////////////////////////

#include "propertyObject.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — property node serialization
#include "backend/metaData.h"                                        // GetAnyArrayObject — the candidates
#include "backend/propertyManager/property/variant/variantOwner.h"   // the variant a relationship holds

#define propBlock 0x00023456
#define eventBlock 0x00023457

namespace ibPropertyGate {

// Said once, so neither verb below repeats the null check at every place it has something to say.
static void Refuse(wxString* refusal, const wxString& text)
{
	if (refusal != nullptr)
		*refusal = text;
}

bool SetValue(ibPropertyObject* asked, ibProperty* property, const wxVariant& newValue, wxString* refusal)
{
	if (property == nullptr)
		return false;

	// The property's own owner when no selection was named — a caller outside an editor has one
	// object in hand and no notion of "what is selected".
	if (asked == nullptr)
		asked = property->GetPropertyObject();

	if (asked == nullptr) {
		Refuse(refusal, wxString::Format(
			_("'%s' belongs to nothing that could accept a change."), property->GetName()));
		return false;
	}

	const wxVariant oldValue = property->GetValue();

	// A VETO IS AN ANSWER, not a failure — an object refusing a value knows something the caller
	// does not, and it is the same refusal a person meets in the inspector.
	if (!asked->OnPropertyChanging(property, newValue)) {
		Refuse(refusal, wxString::Format(
			_("'%s' would not accept that value - the object refused the change."),
			property->GetName()));
		return false;
	}

	property->SetValue(newValue);
	asked->OnPropertyChanged(property, oldValue, newValue);

	// FROM THE PROPERTY'S REAL OWNER, which may be a nested child the asked object only accumulates.
	if (ibPropertyObject* owner = property->GetPropertyObject())
		owner->OnChildChanged();

	return true;
}

bool SetEvent(ibPropertyObject* asked, ibEvent* event, const wxVariant& newValue, wxString* refusal)
{
	if (event == nullptr)
		return false;

	if (asked == nullptr)
		asked = event->GetPropertyObject();

	if (asked == nullptr) {
		Refuse(refusal, wxString::Format(
			_("'%s' belongs to nothing that could accept a handler."), event->GetName()));
		return false;
	}

	const wxVariant oldValue = event->GetValue();

	if (!asked->OnEventChanging(event, newValue)) {
		Refuse(refusal, wxString::Format(
			_("'%s' would not accept that handler - the object refused the change."),
			event->GetName()));
		return false;
	}

	event->SetValue(newValue);
	asked->OnEventChanged(event, oldValue, newValue);

	if (ibPropertyObject* owner = event->GetPropertyObject())
		owner->OnChildChanged();

	return true;
}

} // namespace ibPropertyGate

ibPropertyChoiceMode ibBackendProperty::CreateValueList(ibPropertyChoiceList& list,
	ibPropertyChoiceMode mode, const std::initializer_list<ibClassID> classes,
	bool (*accept)(const ibPropertyObject*))
{
	// ⚠ THE CONST OWNER, deliberately. GetMetaData has an overload pair and the non-const one is a
	// wxFAIL unless a subclass overrode it — reading through the const handle is both the working
	// road and the honest one: listing what a property MAY hold changes nothing.
	//
	// No dynamic_cast: the owner answers GetMetaData virtually. It used to be cast to
	// ibValueMetaObjectGenericData first, which is the narrower type nobody here needed.
	const ibPropertyObject* owner = m_owner;
	const ibMetaData* metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	if (metaData == nullptr)
		return ibPropertyChoiceMode::None;

	for (const ibValueMetaObject* object : metaData->GetAnyArrayObject(classes)) {

		if (accept != nullptr && !accept(object))
			continue;

		// ⭐ THE VARIANT IS BUILT WHILE LISTING, so a caller that picked an item has nothing left to
		// work out: it places this. One metaID per choice, because one choice is one relationship —
		// a Mult property composes the set out of the ones that were picked.
		// ⭐ THE NUMBER SAYS WHICH, THE VARIANT IS WHAT TO PLACE. The metaID identifies the candidate
		// — a wxPGChoices entry carries it and a caller points at it by that. The value is the
		// METADESCRIPTION, built here while listing, so whoever picked this item places it as it
		// stands and nothing downstream has to turn a number back into a relationship.
		list.Add((long)object->GetMetaID(), object->GetName(), object->GetSynonym(),
			wxVariant(new ibVariantDataOwner(owner, ibMetaDescription(object->GetMetaID()))),
			object->GetIcon());
	}

	// THE MODE EVEN WHEN THE LIST CAME OUT EMPTY. "There is nothing of that kind in this
	// configuration yet" is a real answer — it is what a caller sees before the first chart of
	// accounts exists — and it is NOT "this property is not chosen from a list". Collapsing the two
	// would make an empty configuration look like an unsupported property.
	return mode;
}

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
	return SetNodeValue(value);   // through the door, so a pasted "nothing" cannot unset a default
}

// The read door — the gate every property type is spared. See the header for what conflating
// "absent" with "the type's zero" cost, and why the answer for absent is `false` rather than a
// value: nothing was read, and the constructor's default stands.
bool ibBackendProperty::SetNodeValue(const ibDataValue& value)
{
	if (value.IsEmpty())
		return false;

	// ⭐⭐ …AND THE SHAPE IS GATED HERE TOO, for the same reason absence is: so a property type is
	// spared the question and cannot answer it by RAISING.
	//
	// A property that holds a NODE reads it with `AsChild()`, which throws on any other kind — the
	// type description says so in a comment beside the call, and four more readers do the same
	// (the two event properties, the form's pair). Handed a string, the throw goes straight past
	// every caller that was collecting refusals: `metadata_create` answered
	// `ibDataValue: wrong value kind (expected 6, got 4)` — numbers, about a serialised shape — and
	// the object it had ALREADY created was never mentioned, so a dimension nobody knew about
	// stayed behind in the register (measured over MCP, 2026-09-03; a half-built object blocks the
	// configuration from being saved at all).
	//
	// The comparison is with what the property HOLDS, which is the same fact the refusal upstairs
	// already prints ("It holds a %s - send one of that shape"). A property whose value is not
	// readable yet answers nothing and is let through, as before: this decides a mismatch, not a
	// shape.
	const ibDataValue held = GetNodeValue();
	if (held.Kind() == ibDataKind::Child && value.Kind() != ibDataKind::Child)
		return false;

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
	//
	// Deliberately NOT ibSession::CurrentFrame(): that door is shut on a
	// force-exiting session, and rightly so — but this is not someone
	// reaching for a window to work with, it is the window's own slot
	// being cleaned of a pointer that is about to dangle. Exactly then it
	// matters most, because a forced close is when this object dies with
	// the frame still standing.
	ibSession* const owner = ibSession::Current();
	if (auto* frame = owner != nullptr ? owner->GetFrame() : nullptr) {
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
	// ⭐ …AND UP THE ATTACH CHAIN. An attached object's properties are DRAWN by its owner's
	// inspector, and the notifier lives on the owner — so an attached object hiding one of its own
	// properties reached nobody at all. (That is why a composition kept showing Source and Query
	// text after being told to hide them: it was talking to an empty notifier list.)
	if (m_attachOwner != nullptr && m_attachOwner != this && m_attachOwner->HideProperty(property, hide))
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

	// PASTE IS A MERGE BY NAME, and the loop is over the TARGET's properties for that reason: what
	// both sides have is carried over, what only the source has is dropped, and what only the
	// TARGET has KEEPS ITS DEFAULT. That last case is why the absent value is skipped instead of
	// being handed over as an empty ibDataValue — a property type that (rightly) refuses an empty
	// value made the whole paste fail, so pasting a Document onto a Constant died instead of
	// copying the properties the two do share. "Nothing arrived" is not "an empty value arrived".
	if (const ibDataNode* propsNode = builder.Root().FindChild(wxT("props"))) {
		for (unsigned int idx = 0; idx < GetPropertyCount(); idx++) {
			ibProperty* prop = GetProperty(idx);
			wxASSERT(prop);
			const ibDataValue* incoming = propsNode->FindProperty(prop->GetName());
			if (incoming == nullptr)
				continue;
			if (!prop->PasteNodeValue(*incoming))
				return false;
		}
	}
	if (const ibDataNode* eventsNode = builder.Root().FindChild(wxT("events"))) {
		for (unsigned int idx = 0; idx < GetEventCount(); idx++) {
			ibEvent* event = GetEvent(idx);
			wxASSERT(event);
			const ibDataValue* incoming = eventsNode->FindProperty(event->GetName());
			if (incoming == nullptr)
				continue;
			if (!event->PasteNodeValue(*incoming))
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
