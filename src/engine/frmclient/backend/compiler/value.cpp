#include "value.h"

#include <map>
#include <memory>

#include "core/serialize/dataBuilder.h"   // ibDataNode — a value written, and read back
#include "frmclient/backend/system/value/composition/valueComposerField.h"   // a field of a composition, read back as one
#include "frmclient/backend/system/value/valueColour.h"   // …an appearance's colour
#include "frmclient/backend/system/value/valueFont.h"     // …and its font
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/win/picture.h"
#include "protocol/protocol.h"

ibValue::ibValue(ibValue* pParam)
	: ibLexValue(pParam != nullptr ? ibValueTypes::TYPE_REFFER : ibValueTypes::TYPE_EMPTY), m_pRef(pParam)
{
	if (m_pRef != nullptr)
		m_pRef->IncrRef();
}

void ibValue::Assign(const ibValue& rhs)
{
	ibLexValue::operator=(rhs);   // the primitive, without the count
	m_classType = rhs.m_classType;
	m_stored = rhs.m_stored;
	m_pRef = rhs.m_pRef;
	if (IsReference() && m_pRef != nullptr)
		m_pRef->IncrRef();
}

void ibValue::Release()
{
	if (IsReference() && m_pRef != nullptr) {
		ibValue* const held = m_pRef;
		m_pRef = nullptr;
		held->DecrRef();
	}
}

bool ibValue::FindValue(const wxString& findData, std::vector<ibValue>& foundedObjects) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->FindValue(findData, foundedObjects);

	// A FLAG IS ONE OF TWO — the engine's quick choice of a boolean.
	if (m_typeClass == ibValueTypes::TYPE_BOOLEAN) {
		for (const bool flag : { true, false }) {
			const ibValue one(flag);
			if (findData.IsEmpty() || wxString(one.GetString()).Lower().StartsWith(findData.Lower()))
				foundedObjects.push_back(one);
		}
		return true;
	}
	return false;
}

ibValue* ibValue::GetRef() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetRef();
	return const_cast<ibValue*>(this);
}

ibClassID ibValue::GetClassType() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetClassType();
	return m_classType != 0 ? m_classType : GetIDByVT(m_typeClass);
}

ibString ibValue::GetString() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->GetString();
	return ibLexValue::GetString();
}

bool ibValue::IsEmpty() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->IsEmpty();

	// The engine's: Undefined and Null are empty, and so is a primitive's empty value; a value of the server's type is
	// empty when the server wrote it so — it is not asked here, where nothing reads a base.
	switch (m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
	case ibValueTypes::TYPE_BOOLEAN:
	case ibValueTypes::TYPE_NUMBER:
	case ibValueTypes::TYPE_DATE:
	case ibValueTypes::TYPE_STRING:  return ibLexValue::IsEmpty();   // a primitive — the base's
	case ibValueTypes::TYPE_ENUM:    return m_number < 0;   // the negative range is "no member" — the engine's
	default:                         return m_stored == nullptr;
	}
}

bool ibValue::CompareValueEQ(const ibValue& rhs) const
{
	// An object held is compared as the object — its own comparison, or the same object.
	if (IsReference() || rhs.IsReference()) {
		const ibValue* const left = GetRef();
		const ibValue* const right = rhs.GetRef();
		if (left == right)
			return true;
		if (left != this)
			return left->CompareValueEQ(*right);
		return CompareValueEQ(*right);
	}

	if (m_typeClass != rhs.m_typeClass || GetClassType() != rhs.GetClassType())
		return false;

	switch (m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:    return true;
	case ibValueTypes::TYPE_BOOLEAN: return m_number == rhs.m_number;
	case ibValueTypes::TYPE_NUMBER:  return m_numeric == rhs.m_numeric;
	case ibValueTypes::TYPE_DATE:    return m_date == rhs.m_date;
	case ibValueTypes::TYPE_STRING:  return m_text == rhs.m_text;
	case ibValueTypes::TYPE_ENUM:    return m_number == rhs.m_number;
	default:
		break;
	}

	// Two values of the server's are the same when the server wrote them the same: their payloads, compared as text.
	if (m_stored == rhs.m_stored)
		return true;
	if (m_stored == nullptr || rhs.m_stored == nullptr)
		return false;
	const ibDataValue* const left = m_stored->FindField(kValueFieldData);
	const ibDataValue* const right = rhs.m_stored->FindField(kValueFieldData);
	return (left != nullptr ? left->AsString() : wxString()) == (right != nullptr ? right->AsString() : wxString());
}

bool ibValue::Serialize(ibDataNode& node) const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->Serialize(node);
	if (m_stored != nullptr) {
		node = *m_stored;   // the server's, given back as it came
		return true;
	}

	node.SetValue(kValueFieldClsid, wxString::Format(wxT("%llu"), (unsigned long long)GetClassType()));
	return DoSerialize(node);
}

bool ibValue::DoSerialize(ibDataNode& node) const
{
	switch (m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:    return true;
	case ibValueTypes::TYPE_BOOLEAN: node.SetValue(kValueFieldData, m_number != 0); return true;
	case ibValueTypes::TYPE_NUMBER:  node.SetValue(kValueFieldData, m_numeric); return true;
	case ibValueTypes::TYPE_DATE:    node.SetValue(kValueFieldData, m_date); return true;
	case ibValueTypes::TYPE_STRING:  node.SetValue(kValueFieldData, m_text); return true;
	case ibValueTypes::TYPE_ENUM:    node.SetValue(kValueFieldData, (s32)m_number); return true;   // the member, by its number
	default:                         return false;
	}
}

ibValue ibValue::FromNode(const ibDataNode& node)
{
	unsigned long long parsed = 0;
	const ibClassID clsid = node.GetValue<wxString>(kValueFieldClsid).ToULongLong(&parsed) && parsed != 0
		? static_cast<ibClassID>(parsed) : g_valueUndefinedCLSID;
	const ibDataValue* const data = node.FindField(kValueFieldData);

	switch (GetVTByID(clsid)) {
	case ibValueTypes::TYPE_EMPTY:   return ibValue();
	case ibValueTypes::TYPE_NULL: {
		ibValue made;
		made.m_typeClass = ibValueTypes::TYPE_NULL;
		return made;
	}
	case ibValueTypes::TYPE_BOOLEAN: return ibValue(data != nullptr && data->AsBool());
	case ibValueTypes::TYPE_NUMBER:
		// JSON carries a number as one, and the engine's reader reads it so; text is taken as the number it spells.
		if (data != nullptr && data->Kind() == ibDataKind::String)
			return ibValue(ibNumber(data->AsString()));
		return ibValue(data != nullptr ? data->AsNumber() : ibNumber());
	case ibValueTypes::TYPE_DATE: {
		// …and a date as text: it says it is a Date, so the text is read as one (the engine's DoDeserialize).
		if (data != nullptr && data->Kind() == ibDataKind::String) {
			ibDateTime date;
			date.FromString(data->AsString());
			return ibValue(date);
		}
		return ibValue(data != nullptr ? data->AsDate() : ibDateTime());
	}
	case ibValueTypes::TYPE_STRING:  return ibValue(data != nullptr ? data->AsString() : wxString());
	default:
		break;
	}

	// A FIELD OF A COMPOSITION — the client's own object (a condition's other side may be one): made and read, as the
	// engine's registry makes it.
	if (clsid == g_compositionFieldCLSID) {
		ibValueCompositionField* const field = new ibValueCompositionField();
		static_cast<ibValue*>(field)->DoDeserialize(node);
		return ibValue(field);
	}
	// …and an appearance's colour and font, the same way.
	if (clsid == ibValueColour().GetClassType()) {
		ibValueColour* const colour = new ibValueColour();
		static_cast<ibValue*>(colour)->DoDeserialize(node);
		return ibValue(colour);
	}
	if (clsid == ibValueFont().GetClassType()) {
		ibValueFont* const font = new ibValueFont();
		static_cast<ibValue*>(font)->DoDeserialize(node);
		return ibValue(font);
	}

	// A type of the server's — kept as it was written, to be given back; it reads as the server said it reads, when it
	// said so (Presentation: the server's GetString — a reference's name is the configuration's).
	ibValue made;
	made.m_typeClass = ibValueTypes::TYPE_VALUE;
	made.m_classType = clsid;
	made.m_text = node.GetValue<wxString>(ibProtocolName::Presentation);
	made.m_stored = std::make_shared<const ibDataNode>(node);
	return made;
}

namespace {
std::map<ibClassID, std::unique_ptr<ibCtorAbstractType>>& NamedTypes()
{
	static std::map<ibClassID, std::unique_ptr<ibCtorAbstractType>> s_types;
	return s_types;
}
} // namespace

ibCtorAbstractType* ibValue::GetAvailableCtor(const ibClassID& clsid)
{
	const auto found = NamedTypes().find(clsid);
	return found != NamedTypes().end() ? found->second.get() : nullptr;
}

void ibValue::RegisterCtor(const ibClassID& clsid, const wxString& className, const wxIcon& classIcon)
{
	if (clsid == 0 || className.IsEmpty())
		return;
	// A type the server drew no picture of wears a value's own (value_res.cpp): a list of types puts up a picture for
	// each, and an empty one is refused.
	NamedTypes()[clsid] = std::make_unique<ibCtorAbstractType>(className, classIcon.IsOk() ? classIcon : GetIconGroup());
}

ibClassID ibValue::GetIDByVT(const ibValueTypes& valueType)
{
	return ::GetIDByVT(valueType);   // the core's table (core/core.h)
}

ibValueTypes ibValue::GetVTByID(const ibClassID& clsid)
{
	if (clsid == g_valueUndefinedCLSID)
		return ibValueTypes::TYPE_EMPTY;
	else if (clsid == g_valueBooleanCLSID)
		return ibValueTypes::TYPE_BOOLEAN;
	else if (clsid == g_valueNumberCLSID)
		return ibValueTypes::TYPE_NUMBER;
	else if (clsid == g_valueDateCLSID)
		return ibValueTypes::TYPE_DATE;
	else if (clsid == g_valueStringCLSID)
		return ibValueTypes::TYPE_STRING;
	else if (clsid == g_valueNullCLSID)
		return ibValueTypes::TYPE_NULL;

	// Not a primitive — the server's own: a reference, an enum member.
	return ibValueTypes::TYPE_VALUE;
}

std::vector<ibCtorAbstractType*> ibValue::GetListCtorsByType(ibCtorObjectType objectType)
{
	// The question pending names them — a form editor's Classes, its controls'. Kept until the next ask, as the engine's
	// registry keeps its own for the run.
	static std::vector<std::unique_ptr<ibCtorAbstractType>> s_ctors;
	s_ctors.clear();

	std::vector<ibCtorAbstractType*> listed;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || objectType != ibCtorObjectType::ibCtorObjectType_object_control)
		return listed;

	for (const ibProtocolNode& described : frame->GetRequest().FindChild(ibProtocolName::Classes).Children()) {
		wxIcon icon;
		const wxBitmap picture = ibProtocolPicture(described.GetString(ibProtocolName::Picture));
		if (picture.IsOk())
			icon.CopyFromBitmap(picture);
		s_ctors.push_back(std::make_unique<ibCtorAbstractType>(described.GetString(ibProtocolName::Name), icon));
		listed.push_back(s_ctors.back().get());
	}
	return listed;
}
