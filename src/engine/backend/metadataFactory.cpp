#include "metaData.h"
#include "backend/objCtor.h"
#include "backend/utils/debugTrace.h"   // ibDebugTraceEnabled — class-registration tracing is opt-in
#include "backend/query/queryableFactory.h"   // ibMetaQueryableFactory — the per-config source factory (complete here)

// The image's designer infrastructure (compile cache + its module-manager) is built by the owner metadata's
// CreateDesignerCache() — each kind decides (designer-only, nullptr otherwise). Dropping the image later releases
// the manager (RAII → DestroyMainModule), so there's no separate teardown to mirror.
ibMetaImage::ibMetaImage(ibMetaData* owner)
{
	if (owner != nullptr)
		m_compileCache = owner->CreateDesignerCache();   // nullptr ⇒ no designer infra for this kind
	// Every open snapshot gets its OWN source factory — metadata-backed queryables register into it, and resolve
	// descends to the global factory on a miss. Dropping the image frees it (RAII), same lifecycle as the compile cache.
	m_sourceFactory = std::make_unique<ibMetaQueryableFactory>();
}

// Out of line (see the header): m_sourceFactory's unique_ptr deleter needs the complete ibQueryableFactory, present here.
ibMetaImage::~ibMetaImage() = default;

// --- ibMetaImage ctor-factory facade (out of line: these deref ibCtorMetaValueType,
//     complete here via objCtor.h; the clsid/name lookups + ForEachCtor are inline) ---

void ibMetaImage::RegisterCtor(ibCtorMetaValueType* typeCtor)
{
	typeCtor->CallEvent(ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register);
	m_factoryCtors.Register(typeCtor);   // registry takes ownership (shared_ptr)
}

void ibMetaImage::UnregisterCtor(ibCtorMetaValueType* typeCtor)
{
	typeCtor->CallEvent(ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister);
	m_factoryCtors.Unregister(typeCtor);   // registry owns it → freed here; typeCtor dangles after
}

ibCtorMetaValueType* ibMetaImage::FindCtor(const ibValueMetaObject* metaValue, ibCtorObjectMetaType refType) const
{
	// (metaValue, refType) key — metadata-specific, kept linear.
	ibCtorMetaValueType* result = nullptr;
	m_factoryCtors.ForEach([&](ibCtorMetaValueType* typeCtor) {
		if (result == nullptr && refType == typeCtor->GetMetaTypeCtor() && metaValue == typeCtor->GetMetaObject())
			result = typeCtor;
	});
	return result;
}

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

		// Name surface builds lazily on first GetPMethods() — no eager populate.
		return newObject;
	}

	return ibValue::CreateObjectRef(clsid, paParams, lSizeArray);
}

void ibMetaData::RegisterCtor(ibCtorMetaValueType* typeCtor)
{
	if (typeCtor == nullptr)   // checked BEFORE the assert below dereferences it
		return;

	wxASSERT(typeCtor->GetClassType() > 0);

	{
		if (ibMetaData::IsRegisterCtor(typeCtor->GetClassType())) {
			// The register*() macros hand us a raw `new`; the raise leaves nobody to free it.
			// (ibValue's path is covered by value_register's catch — the metadata macros have none.)
			const wxString className = typeCtor->GetClassName();
			wxDELETE(typeCtor);
			ibBackendCoreException::Error(_("Object '%s' is exist"), className);
			return;
		}

		// OFF unless asked for — see the note on OES_TRACE_METAIDS in metaObject.cpp. One line per
		// registered class is a hundred lines of "as expected" before anything interesting happens.
		static const bool s_traceTypes = ibDebugTraceEnabled("OES_TRACE_TYPES");
		if (s_traceTypes)
			ibJournalInfo(wxT("metadata"), wxT("register %s -> %s"),
				typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()));

		wxASSERT(m_image);                 // registration happens only while open (image live)
		m_image->RegisterCtor(typeCtor);   // facade: CallEvent(Register) + register
		m_factoryCtorCountChanges++;       // factory changed → advance the invalidation version
	}
}

void ibMetaData::UnRegisterCtor(ibCtorMetaValueType*& typeCtor)
{
	if (typeCtor != nullptr && ibMetaData::IsRegisterCtor(typeCtor->GetClassType())) {

		// Behind the same gate as its twin above, and it should have been from the start: closing a
		// configuration unregisters every class it declared, so this was a hundred lines of "as
		// expected" at every shutdown while the REGISTER side of the same pair stayed quiet.
		static const bool s_traceTypes = ibDebugTraceEnabled("OES_TRACE_TYPES");
		if (s_traceTypes)
			ibJournalInfo(wxT("metadata"), wxT("unregister %s -> %s"),
				typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()));

		// Facade: CallEvent(UnRegister) + unregister. The registry owns the ctor via
		// shared_ptr, so this FREES it — null the caller's (by-ref) pointer so nobody
		// dereferences the now-dangling ctor (replaces the old wxDELETE that nulled it).
		wxASSERT(m_image);                 // unregistration happens only while open
		m_image->UnregisterCtor(typeCtor);
		m_factoryCtorCountChanges++;       // factory changed → advance the invalidation version
		typeCtor = nullptr;
	}
	else {
		// typeCtor may BE the null here — say so instead of dereferencing it to build the message.
		ibBackendCoreException::Error(_("Object '%s' is not exist"),
			typeCtor != nullptr ? typeCtor->GetClassName() : wxString(wxT("<null>")));
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

void ibMetaData::UnRegisterCtor(const ibClassID& clsid)
{
	ibCtorMetaValueType* typeCtor = GetTypeCtor(clsid);
	if (typeCtor == nullptr) {
		ibBackendCoreException::Error(_("Object with id '%llu' is not exist"), clsid);
		return;
	}

	UnRegisterCtor(typeCtor);
}

void ibMetaData::InvalidateCtorNames() const
{
	// No image ⇒ the configuration is closed ⇒ nothing of this metadata is registered, so there is
	// no cache to bring up to date. (IsConfigOpen() is exactly `m_image != nullptr`.)
	if (m_image)
		m_image->InvalidateCtorNames();
}

bool ibMetaData::IsRegisterCtor(const wxString& className) const
{
	if (className.IsEmpty())
		return false;
	if (m_image && m_image->HasCtor(className))
		return true;
	return ibValue::IsRegisterCtor(className);
}

bool ibMetaData::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType) const
{
	if (className.IsEmpty())
		return false;
	// Names are unique within the factory, so the single name-match decides.
	const ibCtorMetaValueType* typeCtor = m_image ? m_image->FindCtor(className) : nullptr;
	if (typeCtor != nullptr && objectType == typeCtor->GetObjectTypeCtor())
		return true;
	return ibValue::IsRegisterCtor(className, objectType);
}

bool ibMetaData::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType, ibCtorObjectMetaType refType) const
{
	if (className.IsEmpty())
		return false;
	const ibCtorMetaValueType* typeCtor = m_image ? m_image->FindCtor(className) : nullptr;
	if (typeCtor != nullptr
		&& ibCtorObjectType::ibCtorObjectType_object_meta_value == typeCtor->GetObjectTypeCtor()
		&& refType == typeCtor->GetMetaTypeCtor())
		return true;
	return ibValue::IsRegisterCtor(className, objectType);
}

bool ibMetaData::IsRegisterCtor(const ibClassID& clsid) const
{
	if (m_image && m_image->HasCtor(clsid))
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
	if (m_image) m_image->ForEachCtor([&](const ibCtorMetaValueType* typeCtor) {
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
	return m_image ? m_image->FindCtor(className) : nullptr;   // linear by name (see ctorRegistry.h)
}

ibCtorMetaValueType* ibMetaData::GetTypeCtor(const ibClassID& clsid) const
{
	return m_image ? m_image->FindCtor(clsid) : nullptr;       // hot — O(1)
}

ibCtorMetaValueType* ibMetaData::GetTypeCtor(const ibValueMetaObject* metaValue, ibCtorObjectMetaType refType) const
{
	return m_image ? m_image->FindCtor(metaValue, refType) : nullptr;   // linear (metaValue,refType) lookup
}

ibCtorAbstractType* ibMetaData::GetAvailableCtor(const wxString& className) const
{
	if (m_image)
		if (ibCtorMetaValueType* typeCtor = m_image->FindCtor(className))
			return typeCtor;
	return ibValue::GetAvailableCtor(className);
}

ibCtorAbstractType* ibMetaData::GetAvailableCtor(const ibClassID& clsid) const
{
	if (m_image)
		if (ibCtorMetaValueType* typeCtor = m_image->FindCtor(clsid))
			return typeCtor;
	return ibValue::GetAvailableCtor(clsid);
}

std::vector<ibCtorMetaValueType*> ibMetaData::GetListCtorsByType() const
{
	std::vector<ibCtorMetaValueType*> retVector;
	if (m_image) m_image->ForEachCtor([&](ibCtorMetaValueType* t) { retVector.push_back(t); });
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
	if (m_image) m_image->ForEachCtor([&](ibCtorMetaValueType* t) {
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
	if (m_image) m_image->ForEachCtor([&](ibCtorMetaValueType* t) {
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