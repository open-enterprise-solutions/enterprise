#include "advpropCalcSchedule.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the schedule's node
#include "core/description/calcScheduleDescription.h"                                 // ibCalcScheduleDescriptionMemory

#include "frmclient/propertyManager/property/private/prop.h"
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"

#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibProperties — the row's keys

namespace {

// The row's lists (`Schedule`): what each kind of sub-row chooses from.
enum : std::int32_t {
	kListValue = 1,   // a resource of the register — the value
	kListDate = 2,    // a dimension of the register — the date
	kListLink = 3,    // a dimension to link; its children the fields that may answer for it
};

// The schedule a value holds, as the server stores it…
ibCalcScheduleDescription ReadSchedule(const ibVariant& value)
{
	ibCalcScheduleDescription scheduleDesc;
	ibCalcScheduleDescriptionMemory::ReadNode(ibVariantSerializable::GetNodeValue(value), scheduleDesc);
	return scheduleDesc;
}

// …and the value of another one: the same property's, named by the cell (ValueToString).
ibVariant WriteSchedule(const ibCalcScheduleDescription& scheduleDesc)
{
	ibDataValue node;
	ibCalcScheduleDescriptionMemory::WriteNode(node, scheduleDesc);
	return ibVariant(new ibVariantSerializable(node, wxEmptyString));
}

} // namespace

// -----------------------------------------------------------------------
// ibPGCalcScheduleProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGCalcScheduleProperty, ibPGProperty, Choice)

// register frontend property
class ibPropertyCalcScheduleLoader
{
public:
	ibPropertyCalcScheduleLoader()
	{
		// ibPropertyCalcSchedule — the schedule as the server stores it (ibCalcScheduleDescriptionMemory), the registers
		// it may read the row's choices.
		ibPropertyRegistry::Register(wxT("ibPropertyCalcSchedule"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGCalcScheduleProperty(property, property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
} g_calcScheduleLoader;

ibPGChoices ibPGCalcScheduleProperty::GetChoices(const std::vector<ibDataNode>& items)
{
	ibPGChoices choices;
	choices.Add(_("<not set>"), 0);
	for (const ibDataNode& item : items)
		choices.Add(item.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Title), item.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id));   // names, as the type row names its types
	return choices;
}

std::vector<ibDataNode> ibPGCalcScheduleProperty::GetItems(std::int32_t kind) const
{
	std::vector<ibDataNode> items;
	for (const ibDataNode& item : m_lists.Children()) {
		if (item.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Kind) == kind)
			items.push_back(item);
	}
	return items;
}

ibPGCalcScheduleProperty::ibPGCalcScheduleProperty(ibBackendProperty* property, const wxString& label,
	const wxString& strName, const ibVariant& value)
	: ibPGProperty(label, strName), m_property(property)
{
	ibPropertyChoiceList registers;
	if (m_property != nullptr)
		m_property->GetValueList(registers);
	m_choices.Add(_("<not set>"), 0);
	for (unsigned int idx = 0; idx < registers.GetCount(); idx++)
		m_choices.Add(registers.GetName(idx), registers.GetBitmap(idx), registers.GetId(idx));

	// THE ROW'S LISTS — the server's property's answers for the register bound now (its GetProtocolValue).
	ibDataNode row;
	if (m_property != nullptr && m_property->GetProtocolValue(row)) {
		if (const auto node = row.FindProperty(ibProtocol::ibSchema::ibProperties::Schedule)) {
			if (node->Kind() == ibDataKind::Child) {
				m_lists = *node->AsChild();
				m_hasLists = true;
			}
		}
	}

	ibPGChoices valueChoices = GetChoices(GetItems(kListValue));
	m_scheduleValue = new ibEnumProperty(_("Schedule value"), wxT("value"), valueChoices, 0);
	AddPrivateChild(m_scheduleValue);
	ibPGChoices dateChoices = GetChoices(GetItems(kListDate));
	m_scheduleDate = new ibEnumProperty(_("Schedule date"), wxT("date"), dateChoices, 0);
	AddPrivateChild(m_scheduleDate);

	// the link rows of the register bound now — as many dimensions as the server listed, each with the fields that
	// may answer for it (its children)
	const std::vector<ibDataNode> links = GetItems(kListLink);
	for (size_t idx = 0; idx < links.size(); idx++) {
		const ibDataNode& link = links[idx];
		const ibMetaID dimension = link.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id);
		std::vector<ibDataNode> fields;
		for (const ibDataNode& field : link.Children())
			fields.push_back(field);
		// Named as its neighbours are — what the row IS, not the field a configuration happens to link.
		const wxString rowLabel = links.size() == 1 ? _("Schedule link")
			: wxString::Format(_("Schedule link %u"), static_cast<unsigned>(idx + 1));
		ibPGChoices fieldChoices = GetChoices(fields);
		ibEnumProperty* row = new ibEnumProperty(rowLabel, wxString::Format(wxT("link%li"), static_cast<long>(dimension)),
			fieldChoices, 0);
		row->SetHelpString(ibString::Format(_("The field of the register that answers for the schedule's dimension '%s'."),
			link.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Title)));
		AddPrivateChild(row);
		m_linkRows.push_back(row);
		m_linkDimensions.push_back(dimension);
	}

	SetValue(value);

	// Read-only, the rows below as well, when the row brought no lists (see the header).
	if (!m_hasLists)
		ibPGProperty::SetFlagRecursively(ibPGFlags::ReadOnly, true);
}

wxString ibPGCalcScheduleProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
	// ⚠ NAMED BY THE CHOICES — the client holds no configuration to name the register by (the engine's variant asked
	// it); the row's choices are named as the server named them.
	const ibCalcScheduleDescription sd = ReadSchedule(value);
	if (!sd.IsOk())
		return wxEmptyString;
	const int index = m_choices.Index(static_cast<int>(sd.GetRegister()));
	return index != wxNOT_FOUND ? m_choices.GetLabel(index) : wxString();
}

bool ibPGCalcScheduleProperty::IntToValue(ibVariant& value, int number, ibPGPropValFormatFlags flags) const
{
	ibCalcScheduleDescription sd = ReadSchedule(value);
	sd.SetSchedule(m_choices.GetValue(number), 0, 0);
	value = WriteSchedule(sd);
	return true;
}

int ibPGCalcScheduleProperty::GetChoiceSelection() const
{
	return m_choices.Index(ReadSchedule(m_value).GetRegister());
}

ibVariant ibPGCalcScheduleProperty::ChildChanged(ibVariant& thisValue, int childIndex, ibVariant& childValue) const
{
	ibCalcScheduleDescription sd = ReadSchedule(thisValue);
	const ibMetaID id = childValue.GetInteger();
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
	return WriteSchedule(sd);
}

void ibPGCalcScheduleProperty::RefreshChildren()
{
	if (m_scheduleValue == nullptr)
		return;

	// The lists are the row's, for the register it was written for: another register or date is the server's to
	// answer — the row's Set comes back with the rows, and those carry their lists.
	const ibCalcScheduleDescription sd = ReadSchedule(m_value);
	m_scheduleValue->SetValue(sd.GetValue());
	m_scheduleValue->Enable(sd.IsOk());
	m_scheduleDate->SetValue(sd.GetDate());
	m_scheduleDate->Enable(sd.IsOk());
	for (size_t idx = 0; idx < m_linkRows.size(); idx++)
		m_linkRows[idx]->SetValue(sd.GetLinkedField(m_linkDimensions[idx]));
}
