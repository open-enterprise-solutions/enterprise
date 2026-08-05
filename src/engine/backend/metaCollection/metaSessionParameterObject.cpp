#include "metaSessionParameterObject.h"

#include "backend/metaData.h"
#include "backend/metadataConfiguration.h"   // activeMetaData
#include "backend/session/session.h"
#include "backend/backend_exception.h"

//***********************************************************************
//*              ONE parameter — the unit that owns the value           *
//***********************************************************************

ibValueSessionParameter::ibValueSessionParameter(ibValueMetaObjectSessionParameter* metaObject)
	: ibValue(ibValueTypes::TYPE_VALUE, false), m_metaObject(metaObject)
{
}

ibValue ibValueSessionParameter::GetValue() const
{
	const ibSession* const session = ibSession::Current();
	if (!m_metaObject || session == nullptr)
		return ibValue();

	// STRAIGHT THROUGH THE DECLARATION — no test for "was it ever set". An unset
	// parameter hands AdjustValue an empty value and gets back what the TYPE calls
	// empty: an empty reference of that kind rather than a bare Undefined, so a
	// comparison against it narrows the rows to none. One path, whatever is stored.
	return m_metaObject->AdjustValue(session->GetSessionParameter(m_metaObject->GetName()));
}

void ibValueSessionParameter::SetValue(const ibValue& value)
{
	if (!m_metaObject)
		return;

	ibSession* const session = ibSession::Current();
	if (session == nullptr)
		ibBackendCoreException::Error(_("There is no session to set a session parameter in"));

	// THROUGH THE DECLARATION. What may be stored is what the metaobject's type
	// says, and AdjustValue is the one door that decides it — a scale, a date
	// truncation, a reference of the wrong kind. None of those rules is restated
	// here, because the declaration already carries them.
	//
	// The session refuses the write outside its own module, by raising.
	session->SetSessionParameter(m_metaObject->GetName(), m_metaObject->AdjustValue(value));
}

//***********************************************************************
//*        SessionParameters — the manager reached from the context     *
//***********************************************************************

ibValueSessionParameters::ibValueSessionParameters(ibMetaData* metaData)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false), m_metaData(metaData)
{
	m_members.Bind(this, &ibValueSessionParameters::FillMembers);
}

ibValueMetaObjectSessionParameter* ibValueSessionParameters::ParameterAt(long index) const
{
	// The one handed over, or the active one — a value created by the type registry
	// has no metadata of its own to point at.
	ibMetaData* const metaData = m_metaData != nullptr ? m_metaData : activeMetaData;
	if (metaData == nullptr || index < 0)
		return nullptr;

	const auto declarations =
		metaData->GetAnyArrayObject<ibValueMetaObjectSessionParameter>(g_metaSessionParameterCLSID);
	return index < (long)declarations.size() ? declarations[index] : nullptr;
}

void ibValueSessionParameters::FillMembers(ibMemberTable& helper) const
{
	// FROM THE METADATA, every time it is asked. A cached list would go stale the
	// moment a parameter is added in the designer, and the editor would complete a
	// name the runtime refuses — or refuse one the designer shows.
	ibMetaData* const metaData = m_metaData != nullptr ? m_metaData : activeMetaData;
	if (metaData == nullptr)
		return;

	for (auto* parameter :
			metaData->GetAnyArrayObject<ibValueMetaObjectSessionParameter>(g_metaSessionParameterCLSID)) {
		if (parameter != nullptr && !parameter->IsDeleted())
			helper.AppendProp(parameter->GetName());
	}
}

bool ibValueSessionParameters::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	ibValueMetaObjectSessionParameter* const parameter = ParameterAt(lPropNum);
	if (parameter == nullptr)
		return false;

	// THE UNIT, not a loose value. It carries its declaration, so whoever holds it
	// can still be told what type it is — and it reads as its value, so a comparison
	// against a field compares what is in it.
	pvarPropVal = new ibValueSessionParameter(parameter);
	return true;
}

bool ibValueSessionParameters::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	ibValueMetaObjectSessionParameter* const parameter = ParameterAt(lPropNum);
	if (parameter == nullptr)
		return false;

	ibSession* const session = ibSession::Current();
	if (session == nullptr)
		ibBackendCoreException::Error(_("There is no session to set a session parameter in"));

	// THROUGH THE DECLARATION: what may be stored is what the metaobject's type says,
	// and AdjustValue is the door that decides it. The session refuses the write
	// outside its own module, by raising.
	session->SetSessionParameter(parameter->GetName(), parameter->AdjustValue(varPropVal));
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectSessionParameter, "SessionParameter", g_metaSessionParameterCLSID);

// SYSTEM, not a value type — and the difference is not bookkeeping. A value type is
// something a script CREATES (`New Array`, `New Structure`); this one cannot be,
// because it has nothing to be without the metadata: its members ARE the
// declarations, and a second instance would describe the same session twice. The
// platform vends the single one through the global context, exactly as it vends
// Iterator and LinqQuery.
SYSTEM_TYPE_REGISTER(ibValueSessionParameters, "SessionParameters");

// The UNIT is a value like any other, so it needs a ctor entry too — missing it is
// not a missing feature but a crash waiting for the first question about the value's
// type: GetClassName walks GetTypeIDByRef, which resolves through this registry and
// asserts on a type that is not in it. The debugger asks exactly that question about
// every value it shows, which is where it surfaced.
//
// Its own name, not the metatype's: the metatype describes the DECLARATION, this is
// what a script holds. Different kinds, so the ids could not collide either way —
// but one name answering two things makes a lookup by name ambiguous.
SYSTEM_TYPE_REGISTER(ibValueSessionParameter, "SessionParameterValue");
