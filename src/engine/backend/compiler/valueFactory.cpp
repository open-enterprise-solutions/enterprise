////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common factory module 
////////////////////////////////////////////////////////////////////////////

#include <atomic>   // std::atomic — MSVC supplied this transitively
#include "value.h"
#include "backend/backend_exception.h"
#include "backend/ctorRegistry.h"
#include "backend/utils/debugTrace.h"

#ifdef DEBUG
// OFF unless OES_TRACE_TYPES says otherwise — see utils/debugTrace.h.
static const bool s_traceTypes = ibDebugTraceEnabled("OES_TRACE_TYPES");
#endif

// Single owner of the registered value-ctors + the clsid / type_info / name
// lookups. Hot keys (clsid, type_info) are O(1); name stays linear (see header).
//
// The registry OWNS ITS LIFETIME instead of having it inferred from its contents. It used to be
// a raw pointer newed by whichever value_register happened to register first, and deleted again
// the moment the map went empty — a guess about the future, and a wrong one: unregistration runs
// from the static teardown of several modules, so one ctor that never comes back leaves the whole
// registry alive to the end of the process, with nobody left to free it.
//
// A function-local static needs neither the new nor the guess. It is built by the first caller
// and destroyed by the CRT at exit, and the ORDER is right by construction: MSVC keeps static
// destructors in one LIFO list, so an object that registers its destructor before every registrar
// that had to build it is destroyed after all of them. Registrars unregister first, the registry
// dies last.
static bool s_registryAlive = false;   // trivially destructible, so it stays readable after the
                                       // holder below is gone — see CtorRegistry()

struct ibCtorRegistryHolder {
	ibCtorRegistry<ibCtorAbstractType> m_registry;
	ibCtorRegistryHolder() { s_registryAlive = true; }
	~ibCtorRegistryHolder() { s_registryAlive = false; }
};

// nullptr once teardown has passed the registry — a late query then answers "not registered"
// rather than touching a destroyed map. The pointer form got this for free (it simply leaked
// instead); paying for it explicitly is the price of having a destructor at all.
static ibCtorRegistry<ibCtorAbstractType>* CtorRegistry()
{
	static ibCtorRegistryHolder s_holder;
	return s_registryAlive ? &s_holder.m_registry : nullptr;
}

static std::atomic<unsigned int> s_factoryCtorCountChanges = 0;

//*******************************************************************************
//*                      Support dynamic object                                 *
//*******************************************************************************

ibValue* ibValue::CreateObjectRef(const ibClassID& clsid, ibValue** paParams, const long lSizeArray)
{
	const ibCtorAbstractType* typeCtor = GetAvailableCtor(clsid);

	if (typeCtor != nullptr) {
		ibValue* created_value = typeCtor->CreateObject();
		wxASSERT(created_value);
		if (typeCtor->GetObjectTypeCtor() != ibCtorObjectType::ibCtorObjectType_object_system) {
			bool succes = true;
			if (lSizeArray > 0)
				succes = created_value->Init(paParams, lSizeArray);
			else
				succes = created_value->Init();
			if (!succes) {
				wxDELETE(created_value);
				ibBackendCoreException::Error(_("Error initializing object '%s'"), typeCtor->GetClassName());
			}
			// Name surface builds lazily on first GetPMethods() — no eager populate.
		}
		return created_value;
	}
	else {
		ibBackendCoreException::Error(_("Error creating object '%llu'"), clsid);
	}

	return nullptr;
}

void ibValue::RegisterCtor(ibCtorAbstractType* typeCtor)
{
	if (typeCtor != nullptr) {

		if (ibValue::IsRegisterCtor(typeCtor->GetClassType())) {
			ibBackendCoreException::Error(_("Object '%s' is exist"), typeCtor->GetClassName());
		}
		else if (ibValue::IsRegisterCtor(typeCtor->GetClassName())) {
			ibBackendCoreException::Error(_("Object '%s' is exist"), typeCtor->GetClassName());
		}

#ifdef DEBUG
		if (s_traceTypes && wxTheApp != NULL)
			wxLogDebug(wxT("* Register class '%s' with clsid '%s:%llu' "), typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif

		s_factoryCtorCountChanges++;

		typeCtor->CallEvent(ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register);
		if (ibCtorRegistry<ibCtorAbstractType>* registry = CtorRegistry())
			registry->Register(typeCtor);
	}
}

void ibValue::UnRegisterCtor(ibCtorAbstractType*& typeCtor)
{
	if (typeCtor != nullptr && IsRegisterCtor(typeCtor->GetClassType())) {

		typeCtor->CallEvent(ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister);

#ifdef DEBUG
		// The wxTheApp guard is NOT about noise: this also runs from static teardown, where a log
		// call would ask wx to build a log target nobody can then delete (see OnExit's
		// wxLog::DontCreateOnDemand note).
		if (s_traceTypes && wxTheApp != NULL)
			wxLogDebug(wxT("* Unregister class '%s' with clsid '%s:%llu' "), typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif
		// Registry owns the ctor via shared_ptr — Unregister FREES it; null the caller's
		// (by-ref) pointer so the now-dangling ctor is never dereferenced (replaces the
		// old wxDELETE that nulled it; value_register's dtor relies on this).
		CtorRegistry()->Unregister(typeCtor);
		s_factoryCtorCountChanges++;
		typeCtor = nullptr;
	}
	else if (typeCtor != nullptr) {
		ibBackendCoreException::Error(_("Object '%s' is not register"), typeCtor->GetClassName());
	}
	// No "delete it once it is empty" here any more: empty is not the same as finished, and the
	// registry's own destructor already covers the finished case.
}

void ibValue::UnRegisterCtor(const wxString& className)
{
	ibCtorAbstractType* typeCtor = GetAvailableCtor(className);

	if (typeCtor == nullptr) {
		ibBackendCoreException::Error(_("Object '%s' is not exist"), className);
		return;
	}

	UnRegisterCtor(typeCtor);
}

bool ibValue::IsRegisterCtor(const wxString& className)
{
	if (CtorRegistry() == nullptr || className.IsEmpty())
		return false;
	return CtorRegistry()->Find(className) != nullptr;
}

bool ibValue::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType)
{
	if (CtorRegistry() == nullptr)
		return false;
	// Names are unique (RegisterCtor rejects duplicates), so the single
	// name-match is the one to check the object-type against.
	const ibCtorAbstractType* typeCtor = CtorRegistry()->Find(className);
	return typeCtor != nullptr && objectType == typeCtor->GetObjectTypeCtor();
}

bool ibValue::IsRegisterCtor(const ibClassID& clsid)
{
	return CtorRegistry() != nullptr && CtorRegistry()->Find(clsid) != nullptr;
}

ibClassID ibValue::GetTypeIDByRef(const std::type_info& typeInfo)
{
	const ibCtorAbstractType* typeCtor = GetAvailableCtor(typeInfo);
	wxASSERT(typeCtor);
	return typeCtor != nullptr ?
		typeCtor->GetClassType() : 0;
}

ibClassID ibValue::GetTypeIDByRef(const ibValue* objectRef)
{
	// The object-reference tags resolve through the C++ type-id (typeid(*objectRef),
	// matched against the registry's type_info key — the replacement for the old
	// wxDECLARE_DYNAMIC_CLASS / wxClassInfo path). This is the ONLY caller path: base
	// GetClassType() reaches here exactly for a non-reference object tag (primitives
	// went through GetIDByVT, references delegated to m_pRef, Category-B metaobjects
	// override GetClassType and never arrive).
	//
	// A tag NOT in this set is a programming error — a new object-shaped ibValueTypes
	// was added without extending this switch. We must NOT fall back to
	// objectRef->GetClassType(): that re-enters GetTypeIDByRef and spins into infinite
	// recursion. Fail loudly in Debug, return 0 (Release-safe) instead.
	switch (objectRef->m_typeClass) {
	case ibValueTypes::TYPE_VALUE:
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_FUNCTION:
	case ibValueTypes::TYPE_ITERATOR:
		return GetTypeIDByRef(typeid(*objectRef));
	default:
		wxFAIL_MSG(wxString::Format(
			wxT("GetTypeIDByRef: unhandled object tag %d - add it to the typeid switch"),
			static_cast<int>(objectRef->m_typeClass)));
		return 0;
	}
}

ibClassID ibValue::GetIDObjectFromString(const wxString& className)
{
	const ibCtorAbstractType* typeCtor = GetAvailableCtor(className);
	if (typeCtor != nullptr)
		return typeCtor->GetClassType();
	ibBackendCoreException::Error(_("Object '%s' is not exist"), className);
	return 0;
}

wxString ibValue::GetNameObjectFromID(const ibClassID& clsid, bool upper)
{
	const ibCtorAbstractType* typeCtor = GetAvailableCtor(clsid);
	if (typeCtor != nullptr) {
		return upper ? typeCtor->GetClassName().Upper() :
			typeCtor->GetClassName();
	}
	ibBackendCoreException::Error(_("Object with id '%llu' is not exist"), clsid);
	return wxEmptyString;
}

wxString ibValue::GetNameObjectFromVT(ibValueTypes valueType, bool upper)
{
	if (valueType > ibValueTypes::TYPE_REFFER || CtorRegistry() == nullptr)
		return wxEmptyString;
	wxString result;
	CtorRegistry()->ForEach([&](ibCtorAbstractType* typeCtor) {
		if (!result.IsEmpty())
			return;
		const ibCtorSingleType* simpleSingleObject = dynamic_cast<ibCtorSingleType*>(typeCtor);
		if (simpleSingleObject != nullptr &&
			valueType == simpleSingleObject->GetValueType())
			result = upper ? typeCtor->GetClassName().Upper() : typeCtor->GetClassName();
	});
	return result;
}

ibValueTypes ibValue::GetVTByID(const ibClassID& clsid)
{
	if (clsid == g_valueUndefinedCLSID)
		return ibValueTypes::TYPE_EMPTY;
	else if (clsid == g_valueBooleanCLSID)
		return ibValueTypes::TYPE_BOOLEAN;
	else if (clsid == g_valueNumberCLSID)
		return ibValueTypes::TYPE_NUMBER;
	else if (clsid == g_valueDateCLSID)
		return ibValueTypes::TYPE_DATE;
	else if (clsid == g_valueStringCLSID)
		return ibValueTypes::TYPE_STRING;
	else if (clsid == g_valueNullCLSID)
		return ibValueTypes::TYPE_NULL;

	const ibCtorAbstractType* typeCtor = GetAvailableCtor(clsid);

	if (typeCtor != nullptr && typeCtor->GetObjectTypeCtor() == ibCtorObjectType_object_enum)
		return ibValueTypes::TYPE_ENUM;
	else if (typeCtor != nullptr)
		return ibValueTypes::TYPE_VALUE;

	return ibValueTypes::TYPE_EMPTY;
}

ibClassID ibValue::GetIDByVT(const ibValueTypes& valueType)
{
	if (valueType == ibValueTypes::TYPE_EMPTY)
		return g_valueUndefinedCLSID;
	else if (valueType == ibValueTypes::TYPE_BOOLEAN)
		return g_valueBooleanCLSID;
	else if (valueType == ibValueTypes::TYPE_NUMBER)
		return g_valueNumberCLSID;
	else if (valueType == ibValueTypes::TYPE_DATE)
		return g_valueDateCLSID;
	else if (valueType == ibValueTypes::TYPE_STRING)
		return g_valueStringCLSID;
	else if (valueType == ibValueTypes::TYPE_NULL)
		return g_valueNullCLSID;

	return 0;
}

ibCtorAbstractType* ibValue::GetAvailableCtor(const wxString& className)
{
	return CtorRegistry() != nullptr ? CtorRegistry()->Find(className) : nullptr;
}

ibCtorAbstractType* ibValue::GetAvailableCtor(const ibClassID& clsid)
{
	return CtorRegistry() != nullptr ? CtorRegistry()->Find(clsid) : nullptr;
}

ibCtorAbstractType* ibValue::GetAvailableCtor(const std::type_info& typeInfo)
{
	return CtorRegistry() != nullptr ? CtorRegistry()->Find(typeInfo) : nullptr;
}

std::vector<ibCtorAbstractType*> ibValue::GetListCtorsByType(ibCtorObjectType objectType)
{
	std::vector<ibCtorAbstractType*> retVector;
	if (CtorRegistry() != nullptr)
		CtorRegistry()->ForEach([&](ibCtorAbstractType* t) {
			if (objectType == t->GetObjectTypeCtor()) retVector.push_back(t);
		});
	std::sort(retVector.begin(), retVector.end(),
		[](ibCtorAbstractType* a, ibCtorAbstractType* b) { return a->GetClassName() > b->GetClassName(); }
	);
	return retVector;
}

//*******************************************************************************

unsigned int ibValue::GetFactoryCountChanges()
{
	return s_factoryCtorCountChanges;
}

//*******************************************************************************
