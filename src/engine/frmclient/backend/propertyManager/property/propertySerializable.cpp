#include "propertySerializable.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // its value — the one variant

#include "frmclient/win/picture.h"   // ibProtocolPicture — a choice's picture, as the wire carries it
#include "protocol/protocol.h"       // ibProtocol::ibSchema::ibProperties — the row's keys, as the server's inspector writes them

namespace {

// A std::uint64_t as the wire carries it — a decimal (past what a JSON number keeps); none: 0.
std::uint64_t ReceiveU64(const ibDataNode& node, const char* name)
{
	unsigned long long value = 0;
	node.GetValue<wxString>(name).ToULongLong(&value);
	return value;
}

// A class as the server named and drew it (Type, Picture), registered under that name: what a picker, a cell and a
// window read it by (ibValue::GetAvailableCtor).
ibClassID RegisterCtor(const ibDataNode& entry, const wxString& name)
{
	const ibClassID clsid = ReceiveU64(entry, ibProtocol::ibSchema::ibProperties::Type);
	// Held already as the server names it — its picture is not made again: every property of every control drawn
	// registered every type it offers, a GDI icon made of a bitmap each time (two thirds of a property's making,
	// 2026-10-22).
	const ibCtorAbstractType* const held = ibValue::GetAvailableCtor(clsid);
	if (held != nullptr && held->GetClassName() == name)
		return clsid;
	const wxBitmap picture = ibProtocolPicture(entry.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Picture));
	wxIcon icon;
	if (picture.IsOk())
		icon.CopyFromBitmap(picture);
	ibValue::RegisterCtor(clsid, name, icon);
	return clsid;
}

// Its value as the server wrote it (ibPropertyInspector): a scalar a field, a composite a node — in the one variant,
// named by its class and reading as its words.
wxVariant ReceiveValue(const ibDataNode& row)
{
	const ibDataValue* const scalar = row.FindField(ibProtocol::ibSchema::ibProperties::Value);
	return wxVariant(new ibVariantDataSerializable(row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Class),
		scalar != nullptr ? *scalar : row.GetProperty(ibProtocol::ibSchema::ibProperties::Value),
		row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Presentation)));
}

}

ibPropertySerializable::ibPropertySerializable(ibPropertyCategory* cat, ibDataNode&& row) :
	ibProperty(cat, row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Name), row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Title),
		row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Help), ReceiveValue(row)),
	m_row(std::move(row)),
	m_type(m_row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Class)),
	m_readOnly(m_row.GetValue<bool>(ibProtocol::ibSchema::ibProperties::ReadOnly))
{
	if (m_propLabel.IsEmpty())
		m_propLabel = m_propName;

	// The classes its choices' data names (Classes) — what a window groups them under.
	if (const ibDataNode* const classes = m_row.FindChild(ibProtocol::ibSchema::ibProperties::Classes)) {
		for (const ibDataNode& entry : classes->Children())
			RegisterCtor(entry, entry.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Name));
	}

	// What it may be set to — its children with an Id, each with its data; a type's, the types it may hold (Type — the
	// class id, named and drawn as the server offered it: what the picker and the cell read it by).
	for (const ibDataNode& choice : m_row.Children()) {
		if (choice.FindField(ibProtocol::ibSchema::ibProperties::Type) != nullptr) {
			const wxString title = choice.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Title);
			const ibClassID clsid = RegisterCtor(choice, title);
			m_choices.Add(m_choices.GetCount(), title, wxEmptyString, wxVariant(wxULongLong(clsid)));
			continue;
		}
		if (choice.FindField(ibProtocol::ibSchema::ibProperties::Id) == nullptr)
			continue;
		const long id = choice.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id);
		m_choices.Add(id, choice.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Title), wxEmptyString, wxVariant(id),
			ibProtocolPicture(choice.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Picture)), ReceiveU64(choice, ibProtocol::ibSchema::ibProperties::Data));
	}
	m_mode = m_row.GetValue<bool>(ibProtocol::ibSchema::ibProperties::Multiple) ? ibPropertyChoiceMode::Mult
		: m_choices.GetCount() > 0 ? ibPropertyChoiceMode::Single : ibPropertyChoiceMode::None;
}

ibPropertyChoiceMode ibPropertySerializable::GetValueList(ibPropertyChoiceList& list)
{
	list = m_choices;
	return m_mode;
}

bool ibPropertySerializable::GetProtocolValue(ibDataNode& row) const
{
	if (m_type.IsEmpty())
		return ibProperty::GetProtocolValue(row);
	row = m_row;
	return true;
}

bool ibPropertySerializable::ReadNodeValue(const ibDataValue& value)
{
	m_propValue = wxVariant(new ibVariantDataSerializable(GetType(), value, m_propValue.GetString()));
	return true;
}

bool ibPropertySerializable::WriteNodeValue(ibDataValue& value) const
{
	value = ibVariantDataSerializable::GetNodeValue(m_propValue);
	return true;
}

