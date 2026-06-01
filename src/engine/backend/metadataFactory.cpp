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


		m_factoryCtors.emplace_back(typeCtor);
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

		// Full erase-remove — the old single-iterator erase(remove(...)) dropped only
		// one position and left any shifted duplicate as a dangling pointer in the
		// tail (then wxDELETE'd below). erase(remove(...), end()) removes the whole
		// matched range.
		m_factoryCtors.erase(std::remove(m_factoryCtors.begin(), m_factoryCtors.end(), typeCtor), m_factoryCtors.end());
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
	for (auto& typeCtor : m_factoryCtors)
		if (stringUtils::CompareString(className, typeCtor->GetClassName()))
			return true;
	return ibValue::IsRegisterCtor(className);
}

bool ibMetaData::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType) const
{
	if (className.IsEmpty())
		return false;
	for (auto& typeCtor : m_factoryCtors)
		if (stringUtils::CompareString(className, typeCtor->GetClassName()) && (objectType == typeCtor->GetObjectTypeCtor()))
			return true;
	return ibValue::IsRegisterCtor(className, objectType);
}

bool ibMetaData::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType, ibCtorObjectMetaType refType) const
{
	if (className.IsEmpty())
		return false;
	for (auto& typeCtor : m_factoryCtors)
		if (stringUtils::CompareString(className, typeCtor->GetClassName())
			&& (ibCtorObjectType::ibCtorObjectType_object_meta_value == typeCtor->GetObjectTypeCtor()
				&& refType == typeCtor->GetMetaTypeCtor()))
			return true;
	return ibValue::IsRegisterCtor(className, objectType);
}

bool ibMetaData::IsRegisterCtor(const ibClassID& clsid) const
{
	for (auto& typeCtor : m_factoryCtors)
		if (clsid == typeCtor->GetClassType())
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
	for (auto& typeCtor : m_factoryCtors) {
		const ibValueMetaObject* metaValue = typeCtor->GetMetaObject();
		wxASSERT(metaValue);
		if (refType == typeCtor->GetMetaTypeCtor() && valueType == metaValue->GetMetaID())
			return typeCtor->GetClassType();
	}
	return ibValue::GetIDByVT(static_cast<ibValueTypes>(valueType));
}

ibCtorMetaValueType* ibMetaData::GetTypeCtor(const wxString& className) const
{
	for (auto& typeCtor : m_factoryCtors)
		if (stringUtils::CompareString(className, typeCtor->GetClassName()))
			return typeCtor;
	return nullptr;
}

ibCtorMetaValueType* ibMetaData::GetTypeCtor(const ibClassID& clsid) const
{
	for (auto& typeCtor : m_factoryCtors)
		if (clsid == typeCtor->GetClassType())
			return typeCtor;
	return nullptr;
}

ibCtorMetaValueType* ibMetaData::GetTypeCtor(const ibValueMetaObject* metaValue, ibCtorObjectMetaType refType) const
{
	for (auto& typeCtor : m_factoryCtors)
		if (refType == typeCtor->GetMetaTypeCtor() && metaValue == typeCtor->GetMetaObject())
			return typeCtor;
	return nullptr;
}

ibCtorAbstractType* ibMetaData::GetAvailableCtor(const wxString& className) const
{
	for (auto& typeCtor : m_factoryCtors)
		if (stringUtils::CompareString(className, typeCtor->GetClassName()))
			return typeCtor;

	return ibValue::GetAvailableCtor(className);
}

ibCtorAbstractType* ibMetaData::GetAvailableCtor(const ibClassID& clsid) const
{
	for (auto& typeCtor : m_factoryCtors)
		if (clsid == typeCtor->GetClassType())
			return typeCtor;

	return ibValue::GetAvailableCtor(clsid);
}

std::vector<ibCtorMetaValueType*> ibMetaData::GetListCtorsByType() const
{
	std::vector<ibCtorMetaValueType*> retVector;
	std::copy(m_factoryCtors.begin(), m_factoryCtors.end(), std::back_inserter(retVector));
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
	std::copy_if(m_factoryCtors.begin(), m_factoryCtors.end(), std::back_inserter(retVector), [clsid, refType](ibCtorMetaValueType* t) {
		const ibValueMetaObject* const m = t->GetMetaObject();
		return refType == t->GetMetaTypeCtor() &&
			clsid == m->GetClassType(); }
	);
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
	std::copy_if(m_factoryCtors.begin(), m_factoryCtors.end(), std::back_inserter(retVector), [refType](const ibCtorMetaValueType* t) { return refType == t->GetMetaTypeCtor(); });
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