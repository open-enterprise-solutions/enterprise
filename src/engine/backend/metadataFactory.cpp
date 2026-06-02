#include "metaData.h"
#include "backend/objCtor.h"

ibValue* ibMetaData::CreateObjectRef(const ibClassID& clsid, ibValue** paParams, const long lSizeArray) const
{
	const ibCtorMetaValueType* typeCtor = GetTypeCtor(clsid);

	if (typeCtor != nullptr) {

		ibValue* newObject = typeCtor->CreateObject();
		wxASSERT(newObject);

		if (newObject == nullptr) return nullptr;

		bool succes = true;
		if (lSizeArray > 0)
			succes = newObject->Init(paParams, lSizeArray);
		else
			succes = newObject->Init();

		if (!succes) {
			wxDELETE(newObject);
			ibBackendCoreException::Error(_("Error initializing object '%s'"), typeCtor->GetClassName());
			return nullptr;
		}

		newObject->PrepareNames();
		return newObject;
	}

	return ibValue::CreateObjectRef(clsid, paParams, lSizeArray);
}

void ibMetaData::RegisterCtor(ibCtorMetaValueType* typeCtor)
{
	wxASSERT(typeCtor->GetClassType() > 0);

	if (typeCtor != nullptr) {

		if (ibMetaData::IsRegisterCtor(typeCtor->GetClassType())) {
			ibBackendCoreException::Error(_("Object '%s' is exist"), typeCtor->GetClassName());
		}

#ifdef DEBUG
		wxLogDebug("* Register class '%s' with clsid '%s:%llu' ", typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif

		typeCtor->CallEvent(ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register);

		m_factoryCtors.Register(typeCtor);
		m_factoryCtorCountChanges++;
	}
}

void ibMetaData::UnRegisterCtor(ibCtorMetaValueType*& typeCtor)
{
	if (typeCtor != nullptr && ibMetaData::IsRegisterCtor(typeCtor->GetClassType())) {

		typeCtor->CallEvent(ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister);

#ifdef DEBUG
		wxLogDebug("* Unregister class '%s' with clsid '%s:%llu' ", typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif

		// Erase by clsid key (unique per ctor) — no dangling-tail hazard the old
		// erase(remove(...)) had to guard against.
		m_factoryCtors.Unregister(typeCtor);
		m_factoryCtorCountChanges++;

		wxDELETE(typeCtor);
	}
	else {
		ibBackendCoreException::Error(_("Object '%s' is not exist"), typeCtor->GetClassName());
	}
}

void ibMetaData::UnRegisterCtor(const wxString& className)
{
	ibCtorMetaValueType* typeCtor = GetTypeCtor(className);
	if (typeCtor == nullptr) {
		ibBackendCoreException::Error(_("Object '%s' is not exist"), className);
		return;
	}

	UnRegisterCtor(typeCtor);
}

bool ibMetaData::IsRegisterCtor(const wxString& className) const
{
	if (className.IsEmpty())
		return false;
	if (m_factoryCtors.Find(className) != nullptr)
		return true;
	return ibValue::IsRegisterCtor(className);
}

bool ibMetaData::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType) const
{
	if (className.IsEmpty())
		return false;
	// Names are unique within the factory, so the single name-match decides.
	const ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(className);
	if (typeCtor != nullptr && objectType == typeCtor->GetObjectTypeCtor())
		return true;
	return ibValue::IsRegisterCtor(className, objectType);
}

bool ibMetaData::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType, ibCtorObjectMetaType refType) const
{
	if (className.IsEmpty())
		return false;
	const ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(className);
	if (typeCtor != nullptr
		&& ibCtorObjectType::ibCtorObjectType_object_meta_value == typeCtor->GetObjectTypeCtor()
		&& refType == typeCtor->GetMetaTypeCtor())
		return true;
	return ibValue::IsRegisterCtor(className, objectType);
}

bool ibMetaData::IsRegisterCtor(const ibClassID& clsid) const
{
	if (m_factoryCtors.Find(clsid) != nullptr)
		return true;
	return ibValue::IsRegisterCtor(clsid);
}

ibClassID ibMetaData::GetIDObjectFromString(const wxString& className) const
{
	const ibCtorMetaValueType* typeCtor = GetTypeCtor(className);
	if (typeCtor != nullptr)
		return typeCtor->GetClassType();
	return ibValue::GetIDObjectFromString(className);
}

wxString ibMetaData::GetNameObjectFromID(const ibClassID& clsid, bool upper) const
{
	const ibCtorMetaValueType* typeCtor = GetTypeCtor(clsid);
	if (typeCtor != nullptr)
		return upper ? typeCtor->GetClassName().Upper() : typeCtor->GetClassName();
	return ibValue::GetNameObjectFromID(clsid, upper);
}

ibMetaID ibMetaData::GetVTByID(const ibClassID& clsid) const
{
	const ibCtorMetaValueType* typeCtor = GetTypeCtor(clsid);
	if (typeCtor != nullptr) {
		const ibValueMetaObject* metaValue = typeCtor->GetMetaObject();
		wxASSERT(metaValue);
		return metaValue->GetMetaID();
	}
	return ibValue::GetVTByID(clsid);
}

ibClassID ibMetaData::GetIDByVT(const ibMetaID& valueType, ibCtorObjectMetaType refType) const
{
	// (metaID, refType) key — metadata-specific, kept linear (not the hot clsid path).
	ibClassID result = 0;   // RegisterCtor guarantees GetClassType() > 0, so 0 = not-found
	m_factoryCtors.ForEach([&](const ibCtorMetaValueType* typeCtor) {
		if (result != 0) return;
		const ibValueMetaObject* metaValue = typeCtor->GetMetaObject();
		wxASSERT(metaValue);
		if (refType == typeCtor->GetMetaTypeCtor() && valueType == metaValue->GetMetaID())
			result = typeCtor->GetClassType();
	});
	if (result != 0)
		return result;
	return ibValue::GetIDByVT(static_cast<ibValueTypes>(valueType));
}

ibCtorMetaValueType* ibMetaData::GetTypeCtor(const wxString& className) const
{
	return m_factoryCtors.Find(className);   // linear by name (see ctorRegistry.h)
}

ibCtorMetaValueType* ibMetaData::GetTypeCtor(const ibClassID& clsid) const
{
	return m_factoryCtors.Find(clsid);       // hot — O(1)
}

ibCtorMetaValueType* ibMetaData::GetTypeCtor(const ibValueMetaObject* metaValue, ibCtorObjectMetaType refType) const
{
	// (metaValue, refType) key — metadata-specific, kept linear.
	ibCtorMetaValueType* result = nullptr;
	m_factoryCtors.ForEach([&](ibCtorMetaValueType* typeCtor) {
		if (result == nullptr && refType == typeCtor->GetMetaTypeCtor() && metaValue == typeCtor->GetMetaObject())
			result = typeCtor;
	});
	return result;
}

ibCtorAbstractType* ibMetaData::GetAvailableCtor(const wxString& className) const
{
	if (ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(className))
		return typeCtor;
	return ibValue::GetAvailableCtor(className);
}

ibCtorAbstractType* ibMetaData::GetAvailableCtor(const ibClassID& clsid) const
{
	if (ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(clsid))
		return typeCtor;
	return ibValue::GetAvailableCtor(clsid);
}

std::vector<ibCtorMetaValueType*> ibMetaData::GetListCtorsByType() const
{
	std::vector<ibCtorMetaValueType*> retVector;
	m_factoryCtors.ForEach([&](ibCtorMetaValueType* t) { retVector.push_back(t); });
	std::sort(retVector.begin(), retVector.end(), [](const ibCtorMetaValueType* a, const ibCtorMetaValueType* b) {
		const ibValueMetaObject* ma = a->GetMetaObject(); const ibValueMetaObject* mb = b->GetMetaObject();
		// Lexicographic strict-weak-ordering — name first, metaType as tiebreak. The
		// old `name > name && metaType > metaType` conjunction is NOT a valid ordering
		// (breaks antisymmetry/transitivity) → UB in std::sort / MSVC debug "invalid
		// comparator" assert. Direction preserved (descending).
		if (ma->GetName() != mb->GetName())
			return ma->GetName() > mb->GetName();
		return a->GetMetaTypeCtor() > b->GetMetaTypeCtor();
		}
	);

	return retVector;
}

std::vector<ibCtorMetaValueType*> ibMetaData::GetListCtorsByType(const ibClassID& clsid, ibCtorObjectMetaType refType) const
{
	std::vector<ibCtorMetaValueType*> retVector;
	// Note: clsid here is the metaObject's id (m->GetClassType()), NOT the ctor's —
	// shared across refType variants, so it's a separate (non-unique) key kept linear.
	m_factoryCtors.ForEach([&](ibCtorMetaValueType* t) {
		const ibValueMetaObject* const m = t->GetMetaObject();
		if (refType == t->GetMetaTypeCtor() && clsid == m->GetClassType())
			retVector.push_back(t);
	});
	std::sort(retVector.begin(), retVector.end(), [](const ibCtorMetaValueType* a, const ibCtorMetaValueType* b) {
		const ibValueMetaObject* ma = a->GetMetaObject(); const ibValueMetaObject* mb = b->GetMetaObject();
		// Lexicographic strict-weak-ordering (see GetListCtorsByType() above).
		if (ma->GetName() != mb->GetName())
			return ma->GetName() > mb->GetName();
		return a->GetMetaTypeCtor() > b->GetMetaTypeCtor();
		}
	);
	return retVector;
}

std::vector<ibCtorMetaValueType*> ibMetaData::GetListCtorsByType(ibCtorObjectMetaType refType) const
{
	std::vector<ibCtorMetaValueType*> retVector;
	m_factoryCtors.ForEach([&](ibCtorMetaValueType* t) {
		if (refType == t->GetMetaTypeCtor()) retVector.push_back(t);
	});
	std::sort(retVector.begin(), retVector.end(), [](const ibCtorMetaValueType* a, const ibCtorMetaValueType* b) {
		const ibValueMetaObject* ma = a->GetMetaObject(); const ibValueMetaObject* mb = b->GetMetaObject();
		// Lexicographic strict-weak-ordering (see GetListCtorsByType() above).
		if (ma->GetName() != mb->GetName())
			return ma->GetName() > mb->GetName();
		return a->GetMetaTypeCtor() > b->GetMetaTypeCtor();
		}
	);
	return retVector;
}