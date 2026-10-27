#include "propertyInspector.h"

#include "backend/backend_picture.h"               // ibBackendPicture::SendPicture — a choice's picture, by its id
#include "backend/compiler/value.h"              // ibValue::GetAvailableCtor — a class, as the engine registers it
#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject — a tree's object, shown by its own id
#include "core/serialize/dataBuilder.h"

#include <algorithm>
#include <vector>

// ONE PROPERTY'S ROW — a property's or an event's, of any object, a form's control or a metaobject: what the property
// says of itself (GetProtocolValue), and what it may be set to (GetValueList, as the MCP server asks it) — one of them
// (Multiple: several at once), by its id and its name, as the desktop's cell writes it (ValueToString), with its
// picture as the desktop's list drew it beside it. The client shows it by its class (ibPropertyRegistry), knowing no
// property of its own. A property with no row is not sent.
void ibPropertyInspector::AppendProperty(ibDataNode& parent, ibBackendProperty* property)
{
	// Filled apart and put in whole — a property with no row leaves its parent as it was.
	ibDataNode row(0, static_cast<ibMetaID>(parent.Children().size() + 1));
	if (!property->GetProtocolValue(row, ibBackendProperty::ibReader::Cell))
		return;

	// The pictures of the configuration of the object the property is of — an inspected object is one of a configuration
	// (a metaobject, a form's control, attribute or command).
	const ibPropertyObject* const owner = property->GetPropertyObject();   // CONST: the non-const GetMetaData wxFAILs
	const ibMetaData* const metaData = owner->GetMetaData();

	ibPropertyChoiceList choices;
	if (property->GetValueList(choices) == ibPropertyChoiceMode::Mult)
		row.SetValue(ibProtocol::ibSchema::ibProperties::Multiple, true);
	std::vector<std::uint64_t> classes;   // the classes the choices' data names, each once, in the order they first appear
	for (unsigned int index = 0; index < choices.GetCount(); ++index) {
		ibDataNode& choice = row.AddChild(0, static_cast<ibMetaID>(row.Children().size() + 1));
		choice.SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<std::int32_t>(choices.GetId(index)));
		choice.SetValue(ibProtocol::ibSchema::ibProperties::Title, choices.GetName(index));
		const ibPictureDescription& picture = choices.GetPicture(index);
		if (!picture.IsEmptyPicture())
			choice.SetValue(ibProtocol::ibSchema::ibProperties::Picture, ibBackendPicture::SendPicture(picture, metaData));
		// Its data, a std::uint64_t — past what a JSON number keeps, so as a decimal.
		if (const std::uint64_t data = choices.GetData(index)) {
			choice.SetValue(ibProtocol::ibSchema::ibProperties::Data, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(data)));
			if (std::find(classes.begin(), classes.end(), data) == classes.end())
				classes.push_back(data);
		}
	}

	// …and the classes it names, each once (Type, Name, Picture), as the engine registers them: the client has no
	// configuration to name a kind by, and a window groups the choices under them, as the desktop's did.
	if (!classes.empty()) {
		ibDataNode& named = row.Child(ibProtocol::ibSchema::ibProperties::Classes);
		for (const std::uint64_t clsid : classes) {
			const ibCtorAbstractType* const ctor = ibValue::GetAvailableCtor(clsid);
			if (ctor == nullptr)
				continue;
			ibDataNode& entry = named.AddChild(0, static_cast<ibMetaID>(named.Children().size() + 1));
			entry.SetValue(ibProtocol::ibSchema::ibProperties::Type, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(clsid)));
			entry.SetValue(ibProtocol::ibSchema::ibProperties::Name, ctor->GetClassName());
			entry.SetValue(ibProtocol::ibSchema::ibProperties::Picture, ibBackendPicture::SendPicture(clsid));
		}
	}
	parent.AddChild(row);
}

namespace {

// ONE CATEGORY'S ROWS — as the desktop's inspector laid it out (ibObjectInspector::AddItems): its properties, then its
// categories, each a row with its Title and its own; a property met before is not met again. ⭐ WHAT THE OBJECT'S STATE
// HIDES IS NOT SENT — nor a category left with nothing: the rows ride every frame, so one shown again comes back with
// it.
void AppendCategory(ibDataNode& parent, ibPropertyObject* object, const ibPropertyCategory* category,
	const std::set<const ibProperty*>& hidden, std::set<ibString>& met)
{
	for (unsigned int index = 0; index < category->GetPropertyCount(); ++index) {
		ibProperty* const property = object->GetProperty(category->GetPropertyName(index));
		if (property == nullptr || hidden.find(property) != hidden.end() || !met.insert(property->GetName()).second)
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
		// …put in only when it holds something.
		ibDataNode row(0, static_cast<ibMetaID>(parent.Children().size() + 1));
		row.SetValue(ibProtocol::ibSchema::ibProperties::Name, nextCategory->GetName());
		row.SetValue(ibProtocol::ibSchema::ibProperties::Title, nextCategory->GetLabel());
		AppendCategory(row, object, nextCategory, hidden, met);
		if (!row.Children().empty())
			parent.AddChild(row);
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

void ibPropertyInspector::SelectObject(ibPropertyObject* object, ibMetaID id, std::int32_t tabId)
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

void ibPropertyInspector::SelectObject(ibValueMetaObject* object, std::int32_t tabId)
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
		node.SetValue(ibProtocol::ibSchema::ibProperties::Tab, m_tabId);

	std::set<ibString> met;
	AppendCategory(node, object, object->GetCategory(), m_hidden, met);
}
