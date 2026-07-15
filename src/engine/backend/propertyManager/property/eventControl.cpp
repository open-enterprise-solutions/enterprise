#include "eventControl.h"
#include "backend/system/value/valueEvent.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)


//base property for "event"
bool ibEventControl::SetDataValue(const ibValue& varPropVal)
{
	ibValueEvent* event = varPropVal.ConvertToType<ibValueEvent>();
	if (event == nullptr) return false;
	m_propValue = event->GetString();
	return true;
}

bool ibEventControl::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue<ibValueEvent>(m_propValue);
	return true;
}

// A child node with named properties (Name + handler Value) — no opaque bytes.
bool ibEventControl::ReadNodeValue(const ibDataValue& value)
{
	const std::shared_ptr<ibDataNode>& node = value.AsChild();
	if (node) {
		m_propName = node->GetValue<wxString>(wxT("Name"));
		m_propValue = node->GetValue<wxString>(wxT("Value"));
	}
	return true;
}

bool ibEventControl::WriteNodeValue(ibDataValue& value) const
{
	auto node = std::make_shared<ibDataNode>();
	node->SetValue(wxT("Name"), m_propName);
	node->SetValue(wxT("Value"), m_propValue.GetString());
	value = ibDataValue::Child(node);
	return true;
}