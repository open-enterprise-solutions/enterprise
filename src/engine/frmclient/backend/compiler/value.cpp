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

ibValue::ibValue(const ibNumber& number)
	: m_typeClass(ibValueTypes::TYPE_NUMBER), m_text(number.ToString()), m_numeric(number)
{
	long long whole = 0;
	if (number.ToInt(whole))
		m_number = static_cast<long>(whole);
}

ibValue::ibValue(const ibDateTime& date)
	: m_typeClass(ibValueTypes::TYPE_DATE), m_text(date.ToString()), m_date(date)
{
}

ibValue::ibValue(ibValue* pParam)
	: m_typeClass(pParam != nullptr ? ibValueTypes::TYPE_REFFER : ibValueTypes::TYPE_EMPTY), m_pRef(pParam)
{
	if (m_pRef != nullptr)
		m_pRef->IncrRef();
}

void ibValue::Assign(const ibValue& rhs)
{
	m_typeClass = rhs.m_typeClass;
	m_classType = rhs.m_classType;
	m_text = rhs.m_text;
	m_number = rhs.m_number;
	m_numeric = rhs.m_numeric;
	m_date = rhs.m_date;
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

bool ibValue::GetBoolean() const
{
	if (m_typeClass == ibValueTypes::TYPE_STRING)
		return m_text.IsSameAs(wxT("true"), false) || m_text.IsSameAs(_("True"), false) || m_text == wxT("1");
	if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		return !m_numeric.IsZero();
	return m_number != 0;
}

ibNumber ibValue::GetNumber() const
{
	if (m_typeClass == ibValueTypes::TYPE_STRING) {
		// A decimal comma reads as the point — what a person types in the locale's way.
		wxString text = m_text;
		text.Trim().Trim(false);
		text.Replace(wxT(","), wxT("."));
		text.Replace(wxT(" "), wxEmptyString);
		ibNumber number;
		return number.FromString(text) ? number : ibNumber();
	}
	if (m_typeClass == ibValueTypes::TYPE_BOOLEAN)
		return ibNumber(m_number != 0 ? 1 : 0);
	return m_numeric;
}

ibDateTime ibValue::GetDate() const
{
	if (m_typeClass == ibValueTypes::TYPE_STRING) {
		ibDateTime date;
		date.FromString(m_text);
		return date;
	}
	return m_date;
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
	return m_text;
}

bool ibValue::IsEmpty() const
{
	if (m_pRef != nullptr && IsReference())
		return m_pRef->IsEmpty();

	// The engine's: Undefined and Null are empty, and so is a primitive's empty value; a value of the server's type is
	// empty when the server wrote it so — it is not asked here, where nothing reads a base.
	switch (m_typeClass) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:    return true;
	case ibValueTypes::TYPE_BOOLEAN: return m_number == 0;
	case ibValueTypes::TYPE_NUMBER:  return m_numeric.IsZero();
	case ibValueTypes::TYPE_DATE:    return m_date.IsEmpty();
	case ibValueTypes::TYPE_STRING:  return m_text.IsEmpty();
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

void ibValue::RegisterCtor(const ibClassID& clsid, const wxString& className)
{
	if (clsid == 0 || className.IsEmpty())
		return;
	// The server names a type and draws no picture of it — it wears a value's own (value_res.cpp): a list of types puts up
	// a picture for each, and an empty one is refused.
	NamedTypes()[clsid] = std::make_unique<ibCtorAbstractType>(className, GetIconGroup());
}

ibClassID ibValue::GetIDByVT(const ibValueTypes& valueType)
{
	if (valueType == ibValueTypes::TYPE_EMPTY)
		return g_valueUndefinedCLSID;
	else if (valueType == ibValueTypes::TYPE_BOOLEAN)
		return g_valueBooleanCLSID;
	else if (valueType == ibValueTypes::TYPE_NUMBER)
		return g_valueNumberCLSID;
	else if (valueType == ibValueTypes::TYPE_DATE)
		return g_valueDateCLSID;
	else if (valueType == ibValueTypes::TYPE_STRING)
		return g_valueStringCLSID;
	else if (valueType == ibValueTypes::TYPE_NULL)
		return g_valueNullCLSID;

	return 0;
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
