#include "eventControl.h"
#include "backend/system/value/valueEvent.h"
#include "backend/compiler/procUnitValues.h"   // AsFunction / ibValueFunction — a lambda value (IS-A dispatcher)
#include "backend/eventDispatcher.h"           // ibEventDispatcher — the facet GetDispatcher vends
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)


// THE control event's dispatcher = the held value as its ibEventDispatcher facet. A function assigned from script is
// its own (lambda) dispatcher; otherwise the classic named event, materialised ONCE from the stored name (kept alive
// in m_dispatcherValue). The upcast is IMPLICIT (both value types inherit ibEventDispatcher) — no cast at the fire site.
ibEventDispatcher* ibEventControl::GetDispatcher() const
{
	if (ibValueFunction* fn = AsFunction(&m_dispatcherValue))
		return fn;   // a lambda -> its own dispatcher (implicit upcast ibValueFunction* -> ibEventDispatcher*)

	// Nothing assigned in the designer -> the event is UNDEFINED -> no dispatcher. nullptr IS the "not set"
	// signal the fire site reads (CallAsEvent: null -> no-op, the event just proceeds). We must NOT fabricate
	// an empty named event here — that's what made GetDispatcher never return null (leaning on a later IsEmpty).
	if (IsEmptyProperty())
		return nullptr;

	// A named event set in the designer: materialise it ONCE from the stored name, cached in m_dispatcherValue.
	// A non-throwing probe (dynamic_cast on the resolved ref), NOT ConvertToType — the controlled cast THROWS
	// ERROR_TYPE_OPERATION on the empty first-fire value; "not built yet" must read as nullptr, not an exception.
	ibValueEvent* ev = dynamic_cast<ibValueEvent*>(m_dispatcherValue.GetRef());
	if (ev == nullptr) {   // not built yet (or the name changed) -> materialise the named event, kept alive here
		m_dispatcherValue = ibValue::CreateObjectValue<ibValueEvent>(m_propValue.GetString());
		ev = dynamic_cast<ibValueEvent*>(m_dispatcherValue.GetRef());
	}
	return ev;   // named event -> its own dispatcher (implicit upcast)
}

// A value (name) edit drops the cached dispatcher value so GetDispatcher rebuilds the named one from the new name.
void ibEventControl::DoSetValue(const wxVariant& val)
{
	m_propValue = val;
	m_dispatcherValue = ibValue();
}


//base property for "event"
bool ibEventControl::SetDataValue(const ibValue& varPropVal)
{
	// A LAMBDA (function value) assigned from script (ThisForm.OnX = Function(...) EndFunction) -> hold it directly;
	// it IS its own dispatcher. Transient: NOT serialised (the name path below is the persisted state).
	if (AsFunction(&varPropVal) != nullptr) {
		m_dispatcherValue = varPropVal;
		return true;
	}
	// CLASSIC named event -> store the name; the named dispatcher (an ibValueEvent) rebuilds from it lazily.
	ibValueEvent* event = varPropVal.ConvertToType<ibValueEvent>();
	if (event == nullptr) return false;
	m_propValue = event->GetString();
	m_dispatcherValue = ibValue();   // invalidate so GetDispatcher rebuilds the named dispatcher from the new name
	return true;
}

bool ibEventControl::GetDataValue(ibValue& pvarPropVal) const
{
	// Symmetric with SetDataValue: if a LAMBDA is assigned, hand the function value back so the runtime reads a real
	// callable (you can SEE it is a lambda, and re-invoke it). Otherwise the classic named-event value from the name.
	if (AsFunction(&m_dispatcherValue) != nullptr) {
		pvarPropVal = m_dispatcherValue;   // the assigned lambda
		return true;
	}
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