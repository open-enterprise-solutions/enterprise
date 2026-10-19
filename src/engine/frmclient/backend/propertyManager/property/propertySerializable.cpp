#include "propertySerializable.h"

#include "frmclient/win/picture.h"   // ibProtocolPicture — a choice's picture, as the wire carries it
#include "protocol/protocol.h"       // ibProtocol::ibSchema::ibProperties — the row's keys, as the server's inspector writes them

namespace {

// A u64 as the wire carries it — a decimal (past what a JSON number keeps); none: 0.
u64 ReceiveU64(const ibDataNode& node, const char* name)
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
	const wxBitmap picture = ibProtocolPicture(entry.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Picture));
	wxIcon icon;
	if (picture.IsOk())
		icon.CopyFromBitmap(picture);
	ibValue::RegisterCtor(clsid, name, icon);
	return clsid;
}

}

ibPropertySerializable::ibPropertySerializable(ibPropertyCategory* cat, const ibDataNode& row) :
	ibProperty(cat, row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Name), row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Title),
		row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Help), wxVariant(row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Presentation))),
	m_row(row),
	m_type(row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Class)),
	m_readOnly(row.GetValue<bool>(ibProtocol::ibSchema::ibProperties::ReadOnly))
{
	if (m_propLabel.IsEmpty())
		m_propLabel = m_propName;

	// …as the server wrote it (ibPropertyInspector): a scalar a field, a composite a node.
	if (const ibDataValue* const scalar = row.FindField(ibProtocol::ibSchema::ibProperties::Value))
		m_value = *scalar;
	else
		m_value = row.GetProperty(ibProtocol::ibSchema::ibProperties::Value);

	// The classes its choices' data names (Classes) — what a window groups them under.
	if (const ibDataNode* const classes = row.FindChild(ibProtocol::ibSchema::ibProperties::Classes)) {
		for (const ibDataNode& entry : classes->Children())
			RegisterCtor(entry, entry.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Name));
	}

	// What it may be set to — its children with an Id, each with its data; a type's, the types it may hold (Type — the
	// class id, named and drawn as the server offered it: what the picker and the cell read it by).
	for (const ibDataNode& choice : row.Children()) {
		if (choice.FindField(ibProtocol::ibSchema::ibProperties::Type) != nullptr) {
			const wxString title = choice.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Title);
			const ibClassID clsid = RegisterCtor(choice, title);
			m_choices.Add(m_choices.GetCount(), title, wxEmptyString, wxVariant(wxULongLong(clsid)));
			continue;
		}
		if (choice.FindField(ibProtocol::ibSchema::ibProperties::Id) == nullptr)
			continue;
		const long id = choice.GetValue<s32>(ibProtocol::ibSchema::ibProperties::Id);
		m_choices.Add(id, choice.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Title), wxEmptyString, wxVariant(id),
			ibProtocolPicture(choice.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Picture)), ReceiveU64(choice, ibProtocol::ibSchema::ibProperties::Data));
	}
	m_mode = row.GetValue<bool>(ibProtocol::ibSchema::ibProperties::Multiple) ? ibPropertyChoiceMode::Mult
		: m_choices.GetCount() > 0 ? ibPropertyChoiceMode::Single : ibPropertyChoiceMode::None;
}

ibPropertyChoiceMode ibPropertySerializable::GetValueList(ibPropertyChoiceList& list)
{
	list = m_choices;
	return m_mode;
}

bool ibPropertySerializable::ReadNodeValue(const ibDataValue& value)
{
	// The value an edit gives — the one the owner puts before the server (ibValueMetaObject::OnPropertyChanging). Its
	// presentation is the server's to say: what reads here is a new cell of the old words until the rows come back.
	m_value = value;
	m_propValue = wxVariant(m_propValue.GetString());
	return true;
}

bool ibPropertySerializable::WriteNodeValue(ibDataValue& value) const
{
	value = m_value;
	return true;
}
