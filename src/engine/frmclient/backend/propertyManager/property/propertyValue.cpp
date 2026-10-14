#include "propertyValue.h"

#include "frmclient/win/picture.h"   // ibProtocolPicture — a choice's picture, as the wire carries it

ibPropertyValue::ibPropertyValue(ibPropertyCategory* cat, const ibDataNode& row) :
	ibProperty(cat, row.GetValue<wxString>(wxT("Name")), row.GetValue<wxString>(wxT("Title")),
		row.GetValue<wxString>(wxT("Help")), wxVariant(row.GetValue<wxString>(wxT("Presentation"))))
{
	ReadProperty(row);
}

void ibPropertyValue::ReadProperty(const ibDataNode& row)
{
	const wxString title = row.GetValue<wxString>(wxT("Title"));
	m_propLabel = title.IsEmpty() ? m_propName : title;
	m_propHelp = row.GetValue<wxString>(wxT("Help"));
	m_type = row.GetValue<wxString>(wxT("Class"));
	m_readOnly = row.GetValue<bool>(wxT("ReadOnly"));
	m_objectId = static_cast<ibMetaID>(row.GetValue<s32>(wxT("Object")));

	// …as the server wrote it (ibPropertyInspector): a scalar a field, a composite a node.
	if (const ibDataValue* const scalar = row.FindField(wxT("Value")))
		m_value = *scalar;
	else
		m_value = row.GetProperty(wxT("Value"));
	m_propValue = wxVariant(row.GetValue<wxString>(wxT("Presentation")));

	// What it may be set to — its children with an Id (a type's children name the types it may hold, not choices).
	m_choices = ibPropertyChoiceList();
	for (const ibDataNode& choice : row.Children()) {
		if (choice.FindField(wxT("Id")) == nullptr)
			continue;
		m_choices.Add(choice.GetValue<wxString>(wxT("Title")), static_cast<long>(choice.GetValue<s32>(wxT("Id"))),
			ibProtocolPicture(choice.GetValue<wxString>(wxT("Picture"))));
	}
	m_mode = row.GetValue<bool>(wxT("Multiple")) ? ibPropertyChoiceMode::Mult
		: m_choices.GetCount() > 0 ? ibPropertyChoiceMode::Single : ibPropertyChoiceMode::None;
}

ibPropertyChoiceMode ibPropertyValue::GetValueList(ibPropertyChoiceList& list)
{
	list = m_choices;
	return m_mode;
}

bool ibPropertyValue::ReadNodeValue(const ibDataValue& value)
{
	// The value an edit gives — the one the owner puts before the server (ibValueMetaObject::OnPropertyChanging). Its
	// presentation is the server's to say: what reads here is a new cell of the old words until the rows come back.
	m_value = value;
	m_propValue = wxVariant(m_propValue.GetString());
	return true;
}

bool ibPropertyValue::WriteNodeValue(ibDataValue& value) const
{
	value = m_value;
	return true;
}
