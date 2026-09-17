#include "advpropCalcSchedule.h"

#include "backend/propertyManager/property/propertyCalcSchedule.h"
#include "backend/propertyManager/property/variant/variantCalcSchedule.h"

#include "frontend/mainFrame/objinspect/objinspect.h"                  // the rebuild another set of link rows needs
#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"

// -----------------------------------------------------------------------
// ibPGCalcScheduleProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGCalcScheduleProperty, wxPGProperty, Choice)

// register frontend property
class ibPropertyCalcScheduleLoader
{
public:
	ibPropertyCalcScheduleLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyCalcSchedule* prop) -> wxPGProperty* {
			return new ibPGCalcScheduleProperty(prop, prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
} g_calcScheduleLoader;

wxPGChoices ibPGCalcScheduleProperty::GetChoices(const ibPropertyChoiceList& list)
{
	wxPGChoices choices;
	choices.Add(_("<not set>"), 0);
	for (unsigned int idx = 0; idx < list.GetCount(); idx++)
		choices.Add(list.GetName(idx), list.GetBitmap(idx), list.GetId(idx));   // names, as the type row names its types
	return choices;
}

std::vector<ibMetaID> ibPGCalcScheduleProperty::GetLinkDimensions(const ibPropertyChoiceList& list) const
{
	std::vector<ibMetaID> dimensions;
	for (unsigned int idx = 0; idx < list.GetCount(); idx++)
		dimensions.push_back(list.GetId(idx));
	return dimensions;
}

ibPGCalcScheduleProperty::ibPGCalcScheduleProperty(ibPropertyCalcSchedule* property, const wxString& label,
	const wxString& strName, const wxVariant& value)
	: wxPGProperty(label, strName), m_property(property)
{
	ibPropertyChoiceList registers;
	if (m_property != nullptr)
		m_property->GetValueList(registers);
	m_choices = GetChoices(registers);

	m_scheduleValue = new wxEnumProperty(_("Schedule value"), wxT("value"), wxArrayString(), wxArrayInt(), 0);
	AddPrivateChild(m_scheduleValue);
	m_scheduleDate = new wxEnumProperty(_("Schedule date"), wxT("date"), wxArrayString(), wxArrayInt(), 0);
	AddPrivateChild(m_scheduleDate);

	// the link rows of the register bound now
	const ibVariantDataCalcSchedule* data = property_cast(value, ibVariantDataCalcSchedule);
	if (m_property != nullptr && data != nullptr) {
		ibPropertyChoiceList dimensions;
		m_property->GetScheduleLinkList(data->GetScheduleDesc(), dimensions);
		m_linkDimensions = GetLinkDimensions(dimensions);
		for (unsigned int idx = 0; idx < dimensions.GetCount(); idx++) {
			ibPropertyChoiceList fields;
			m_property->GetScheduleFieldList(dimensions.GetId(idx), fields);
			wxPGChoices choices = GetChoices(fields);
			// Named as its neighbours are — what the row IS, not the field a configuration happens to link.
			const wxString label = dimensions.GetCount() == 1 ? wxString(_("Schedule link"))
				: wxString::Format(_("Schedule link %u"), idx + 1);
			wxEnumProperty* row = new wxEnumProperty(label,
				wxString::Format(wxT("link%li"), dimensions.GetId(idx)), choices, 0);
			row->SetHelpString(wxString::Format(_("The field of the register that answers for the schedule's dimension '%s'."),
				dimensions.GetName(idx)));
			AddPrivateChild(row);
			m_linkRows.push_back(row);
		}
	}

	SetValue(value);
}

bool ibPGCalcScheduleProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	ibVariantDataCalcSchedule* data = property_cast(value, ibVariantDataCalcSchedule);
	if (data != nullptr) {
		ibVariantDataCalcSchedule* newData = data->Clone();
		wxASSERT(newData);
		newData->GetScheduleDesc().SetSchedule(m_choices.GetValue(number), 0, 0);
		value = newData;
		return true;
	}
	return false;
}

int ibPGCalcScheduleProperty::GetChoiceSelection() const
{
	ibVariantDataCalcSchedule* data = property_cast(m_value, ibVariantDataCalcSchedule);
	return data != nullptr ? m_choices.Index(data->GetScheduleDesc().GetRegister()) : wxNOT_FOUND;
}

wxVariant ibPGCalcScheduleProperty::ChildChanged(wxVariant& thisValue, int childIndex, wxVariant& childValue) const
{
	ibVariantDataCalcSchedule* data = property_cast(thisValue, ibVariantDataCalcSchedule);
	if (data != nullptr) {
		ibVariantDataCalcSchedule* newData = data->Clone();
		wxASSERT(newData);
		ibCalcScheduleDescription& sd = newData->GetScheduleDesc();
		const ibMetaID id = childValue.GetLong();
		if (childIndex == 0) {
			sd.SetSchedule(sd.GetRegister(), id, sd.GetDate());
		}
		else if (childIndex == 1) {
			sd.SetSchedule(sd.GetRegister(), sd.GetValue(), id);
		}
		else {
			const size_t link = childIndex - 2;   // the link rows follow the value and the date
			if (link < m_linkDimensions.size())
				sd.SetLink(m_linkDimensions[link], id);
		}
		return newData;
	}
	return wxNullVariant;
}

void ibPGCalcScheduleProperty::RefreshChildren()
{
	ibVariantDataCalcSchedule* data = property_cast(m_value, ibVariantDataCalcSchedule);
	if (m_property == nullptr || data == nullptr)
		return;

	const ibCalcScheduleDescription& sd = data->GetScheduleDesc();

	// Another set of dimensions to link (another register, or the date moved): the rows are made anew by a rebuild.
	ibPropertyChoiceList dimensions;
	m_property->GetScheduleLinkList(sd, dimensions);
	if (GetLinkDimensions(dimensions) != m_linkDimensions) {
		if (ibObjectInspector* inspector = ibObjectInspector::GetObjectInspector())
			inspector->SelectObject(inspector->GetSelectedObject(), true);
		return;
	}

	ibPropertyChoiceList values;
	m_property->GetScheduleValueList(sd, values);
	wxPGChoices valueChoices = GetChoices(values);
	m_scheduleValue->SetChoices(valueChoices);
	m_scheduleValue->SetValue(sd.GetValue());
	m_scheduleValue->Enable(sd.IsOk());

	ibPropertyChoiceList dates;
	m_property->GetScheduleDateList(sd, dates);
	wxPGChoices dateChoices = GetChoices(dates);
	m_scheduleDate->SetChoices(dateChoices);
	m_scheduleDate->SetValue(sd.GetDate());
	m_scheduleDate->Enable(sd.IsOk());

	for (size_t idx = 0; idx < m_linkRows.size(); idx++)
		m_linkRows[idx]->SetValue(sd.GetLinkedField(m_linkDimensions[idx]));
}
