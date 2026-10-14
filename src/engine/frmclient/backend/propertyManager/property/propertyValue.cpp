#include "propertyValue.h"

#include "frmclient/win/picture.h"   // ibProtocolPicture — a choice's picture, as the wire carries it

ibPropertyValue::ibPropertyValue(ibPropertyCategory* cat, const ibDataNode& row) :
	ibProperty(cat, row.GetValue<wxString>(wxT("Name")), row.GetValue<wxString>(wxT("Title")),
		row.GetValue<wxString>(wxT("Help")), wxVariant(row.GetValue<wxString>(wxT("Presentation")))),
	m_type(row.GetValue<wxString>(wxT("Class"))),
	m_readOnly(row.GetValue<bool>(wxT("ReadOnly"))),
	m_objectId(row.GetValue<s32>(wxT("Object")))
{
	if (m_propLabel.IsEmpty())
		m_propLabel = m_propName;

	// …as the server wrote it (ibPropertyInspector): a scalar a field, a composite a node.
	if (const ibDataValue* const scalar = row.FindField(wxT("Value")))
		m_value = *scalar;
	else
		m_value = row.GetProperty(wxT("Value"));

	// What it may be set to — its children with an Id; a type's, the types it may hold (Type — the class id, named and
	// drawn as the server offered it: what the picker and the cell read it by, ibValue::GetAvailableCtor).
	for (const ibDataNode& choice : row.Children()) {
		if (const ibDataValue* const type = choice.FindField(wxT("Type"))) {
			unsigned long long clsid = 0;
			type->AsString().ToULongLong(&clsid);
			const wxString title = choice.GetValue<wxString>(wxT("Title"));
			const wxBitmap picture = ibProtocolPicture(choice.GetValue<wxString>(wxT("Picture")));
			wxIcon icon;
			if (picture.IsOk())
				icon.CopyFromBitmap(picture);
			ibValue::RegisterCtor(clsid, title, icon);
			m_choices.Add(m_choices.GetCount(), title, wxEmptyString, wxVariant(wxULongLong(clsid)));
			continue;
		}
		if (choice.FindField(wxT("Id")) == nullptr)
			continue;
		m_choices.Add(choice.GetValue<wxString>(wxT("Title")), choice.GetValue<s32>(wxT("Id")),
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
