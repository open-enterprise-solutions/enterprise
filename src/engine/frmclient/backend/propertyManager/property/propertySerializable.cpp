#include "propertySerializable.h"

#include "frmclient/win/picture.h"   // ibProtocolPicture — a choice's picture, as the wire carries it
#include "protocol/protocol.h"       // ibProtocolName — the row's keys, as the server's inspector writes them

ibPropertySerializable::ibPropertySerializable(ibPropertyCategory* cat, const ibDataNode& row) :
	ibProperty(cat, row.GetValue<wxString>(ibProtocolName::Name), row.GetValue<wxString>(ibProtocolName::Title),
		row.GetValue<wxString>(ibProtocolName::Help), wxVariant(row.GetValue<wxString>(ibProtocolName::Presentation))),
	m_type(row.GetValue<wxString>(ibProtocolName::Class)),
	m_readOnly(row.GetValue<bool>(ibProtocolName::ReadOnly)),
	m_objectId(row.GetValue<s32>(ibProtocolName::Object))
{
	if (m_propLabel.IsEmpty())
		m_propLabel = m_propName;

	// …as the server wrote it (ibPropertyInspector): a scalar a field, a composite a node.
	if (const ibDataValue* const scalar = row.FindField(ibProtocolName::Value))
		m_value = *scalar;
	else
		m_value = row.GetProperty(ibProtocolName::Value);

	// What it may be set to — its children with an Id; a type's, the types it may hold (Type — the class id, named and
	// drawn as the server offered it: what the picker and the cell read it by, ibValue::GetAvailableCtor).
	for (const ibDataNode& choice : row.Children()) {
		if (const ibDataValue* const type = choice.FindField(ibProtocolName::Type)) {
			unsigned long long clsid = 0;
			type->AsString().ToULongLong(&clsid);
			const wxString title = choice.GetValue<wxString>(ibProtocolName::Title);
			const wxBitmap picture = ibProtocolPicture(choice.GetValue<wxString>(ibProtocolName::Picture));
			wxIcon icon;
			if (picture.IsOk())
				icon.CopyFromBitmap(picture);
			ibValue::RegisterCtor(clsid, title, icon);
			m_choices.Add(m_choices.GetCount(), title, wxEmptyString, wxVariant(wxULongLong(clsid)));
			continue;
		}
		if (choice.FindField(ibProtocolName::Id) == nullptr)
			continue;
		m_choices.Add(choice.GetValue<wxString>(ibProtocolName::Title), choice.GetValue<s32>(ibProtocolName::Id),
			ibProtocolPicture(choice.GetValue<wxString>(ibProtocolName::Picture)));
	}
	m_mode = row.GetValue<bool>(ibProtocolName::Multiple) ? ibPropertyChoiceMode::Mult
		: m_choices.GetCount() > 0 ? ibPropertyChoiceMode::Single : ibPropertyChoiceMode::None;

	// An event's — its procedure's parameters.
	if (const ibDataValue* const args = row.FindField(ibProtocolName::Args)) {
		if (args->Kind() == ibDataKind::Array) {
			for (const ibDataValue& arg : args->AsArray())
				m_args.Add(arg.AsString());
		}
	}
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
