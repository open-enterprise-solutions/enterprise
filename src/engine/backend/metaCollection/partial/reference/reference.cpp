////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "backend/system/value/valuePointInTime.h"   // the moment a reference can be asked for
#include "backend/metaData.h"
#include "backend/objCtor.h"   // ibCtorMetaValueType::GetMetaTypeCtor / ibCtorObjectMetaType_Reference — ConvertToMetaIds
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

#include <vector>
#include <algorithm>
#include <utility>

// Re-entrancy guard for the eager reference read. A self / cyclic reference (a document
// field pointing at the same record, or A -> B -> A) makes ReadData recurse: it reads a
// reference field, whose eager PrepareRef calls ReadData again, forever -> stack overflow.
// While a ref identity (type + guid) is already being read up the stack, the nested re-read
// is skipped (the field keeps its key, loads lazily on demand). A self-reference is a LEGAL
// config, so this must terminate rather than crash.
//
// A thread-local stack of identities (each worker reads on its own call stack): push on
// entry, pop on exit (RAII — correct across the FB exceptions ReadData can throw). A linear
// scan over a depth that is tiny in practice — negligible next to the DB query the read
// itself issues.
//
// A FIXED array, not a vector, and the difference is not micro-optimisation: a thread-local
// vector allocates once per thread and hands the block back only when the thread ends
// normally. A thread still running at process exit is killed without TLS teardown, so it
// keeps the block — measured 2026-07-30 as six leaked buffers, one per surviving thread.
// A fixed buffer has nothing to hand back. It is also trivially destructible, so no TLS
// destructor is registered at all.
namespace {
	// Depth beyond this means a reference graph nested deeper than any real config; the guard
	// then stops tracking rather than growing. Cycle detection degrades to "not detected" for
	// those levels, which is the same answer it gave before any of them were pushed.
	constexpr std::size_t kRefReadDepthMax = 64;

	thread_local std::pair<ibMetaID, ibGuid> g_refReadStack[kRefReadDepthMax];
	thread_local std::size_t                 g_refReadDepth = 0;

	struct ibRefReadGuard {
		bool m_cycle = false;
		bool m_pushed = false;

		ibRefReadGuard(const ibMetaID& metaId, const ibGuid& guid) {
			const std::pair<ibMetaID, ibGuid> key{ metaId, guid };
			m_cycle = std::find(g_refReadStack, g_refReadStack + g_refReadDepth, key)
				!= g_refReadStack + g_refReadDepth;
			if (g_refReadDepth < kRefReadDepthMax) {
				g_refReadStack[g_refReadDepth++] = key;
				m_pushed = true;
			}
		}
		~ibRefReadGuard() { if (m_pushed) --g_refReadDepth; }
		bool Cycle() const { return m_cycle; }   // true == this identity is already being read up the stack
	};
}


//**********************************************************************************************
//*                                     reference                                              *        
//**********************************************************************************************
//static std::vector <ibValueReferenceDataObject*> gs_references;
//**********************************************************************************************

void ibValueReferenceDataObject::PrepareRef(bool createData)
{
	wxASSERT(m_metaObject != nullptr);

	if (m_initializedRef)
		return;

	if (ibValueReferenceDataObject::IsEmpty()) {
		//attrbutes can refValue 
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!m_metaObject->IsDataReference(object->GetMetaID())) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
			}
		}
		// table is collection values
		for (const auto object : m_metaObject->GetTableArrayObject()) {
			if (object->IsDeleted())
				continue;
			m_listObjectValue.insert_or_assign(object->GetMetaID(),
				new ibValueTabularSectionDataObjectRef(this, object));
		}
	}
	else {
		// Break self / cyclic references: read this identity only if it is not already
		// being read up the stack (otherwise leave it lazy — no infinite recursion).
		ibRefReadGuard guard(m_metaObject->GetMetaID(), m_objGuid);
		if (!guard.Cycle() && ibValueReferenceDataObject::ReadData(createData)) {
			m_foundedRef = true; m_newObject = false;
		}
	}

	if (createData) {
		m_initializedRef = true;
	}
	// Name surface is lazy (FillMembers built on first GetPMethods).
}

ibValueReferenceDataObject::ibValueReferenceDataObject(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), ibValueDataObject(objGuid, !objGuid.isValid()),
m_initializedRef(false), m_metaObject(metaObject), m_reference_impl(nullptr), m_foundedRef(false)
{
	m_members.Bind(this, &ibValueReferenceDataObject::FillMembers);
	// The stored key (_RRRef) is the pure object guid; the type is carried separately (metaObject / _RTRef).
	// An unset reference is simply an empty guid — no normalization needed.
	m_reference_impl = new ibReference(m_objGuid);
	//gs_references.emplace_back(this);
}

// Identity key = the DB reference key (metaID + guid), aligning runtime grouping / join / dedup
// over a reference with the database. See the declaration in reference.h.
wxString ibValueReferenceDataObject::GetHashKey() const
{
	return m_metaObject != nullptr
		? wxString::Format(wxT("%i:%s"), m_metaObject->GetMetaID(), wxString(GetGuid()))
		: wxString(GetGuid());
}

// Ordering: GUID first, then TYPE (metaID) as the tiebreak — see the header. m_metaObject is complete in this
// TU, so GetMetaID() resolves. The tiebreak only fires on equal guids (in practice: empty references, which all
// share the all-zero guid), keeping LS==0 exactly when CompareValueEQ is true.
int ibValueReferenceDataObject::CompareValueLS(const ibValue& cParam) const
{
	ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
	if (rhs == nullptr)
		return 0;
	if (m_objGuid < rhs->m_objGuid) return -1;
	if (rhs->m_objGuid < m_objGuid) return 1;
	const ibMetaID lm = m_metaObject      != nullptr ? m_metaObject->GetMetaID()      : 0;
	const ibMetaID rm = rhs->m_metaObject != nullptr ? rhs->m_metaObject->GetMetaID() : 0;
	if (lm < rm) return -1;
	if (rm < lm) return 1;
	return 0;   // same guid AND same type -> the same reference
}

ibValueReferenceDataObject::~ibValueReferenceDataObject()
{
	wxDELETE(m_reference_impl);
	//gs_references.erase(
	//	std::remove_if(gs_references.begin(), gs_references.end(),
	//		[this](ibValueReferenceDataObject* ref) { return ref == this;}), gs_references.end()
	//);
}

// THE UPCAST, WHERE BOTH TYPES ARE COMPLETE. In the header they are not (see the note on the
// declaration), and a C-style cast there was a reinterpret_cast wearing a plainer suit — no base
// adjustment, correct only while RecordData stays the FIRST base of RecordDataRef. Here the compiler
// computes the offset, so the base order is free to change without silently returning a wrong object.
const ibValueMetaObjectRecordData* ibValueReferenceDataObject::GetMetaObject() const
{
	return static_cast<const ibValueMetaObjectRecordData*>(m_metaObject);
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibMetaData* metaData, const ibMetaID& id, const ibGuid& objGuid)
{
	const ibValueMetaObjectRecordDataRef* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataRef>(id);
	if (metaObject != nullptr) {
		//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, objGuid](ibValueReferenceDataObject* ref) {
		//	return metaObject == ref->GetMetaObject() && objGuid == ref->GetGuid(); }
		//);
		//if (it != gs_references.end())
		//	return *it;
		ibValueReferenceDataObject* refData = new ibValueReferenceDataObject(metaObject, objGuid);
		if (refData != nullptr)
			refData->PrepareRef(true);
		return refData;
	}
	return nullptr;
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid)
{
	//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, objGuid](ibValueReferenceDataObject* ref) {
	//	return metaObject == ref->GetMetaObject() && objGuid == ref->GetGuid(); }
	//);
	//if (it != gs_references.end())
	//	return *it;
	ibValueReferenceDataObject* refData = new ibValueReferenceDataObject(metaObject, objGuid);
	if (refData != nullptr)
		refData->PrepareRef(true);
	return refData;
}

ibValueReferenceDataObject* ibValueReferenceDataObject::CreateRaw(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid)
{
	// Construct without PrepareRef (deferred to first use): the value-ctor
	// registry path can't prepare eagerly without recursing. See header note.
	return new ibValueReferenceDataObject(metaObject, objGuid);
}

// clsid (the _RTRef target type) -> its reference metaObject, through the class factory. The type comes
// from the column, never from the key bytes (the _RRRef blob is pure identity now).
static const ibValueMetaObjectRecordDataRef* MetaObjectFromClsid(const ibMetaData* metaData, const ibClassID& clsid)
{
	const ibCtorMetaValueType* typeCtor = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
	if (typeCtor == nullptr || typeCtor->GetMetaTypeCtor() != ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
		return nullptr;
	return dynamic_cast<const ibValueMetaObjectRecordDataRef*>(typeCtor->GetMetaObject());
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibMetaData* metaData, const ibClassID& refClsid, void* ptr)
{
	ibReference* reference = static_cast<ibReference*>(ptr);
	if (reference != nullptr) {
		const ibValueMetaObjectRecordDataRef* metaObject = MetaObjectFromClsid(metaData, refClsid);
		if (metaObject != nullptr) {
			//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, reference](ibValueReferenceDataObject* ref) {
			//	return metaObject == ref->GetMetaObject() && ref->GetGuid() == reference->m_guid; }
			//);
			//if (it != gs_references.end())
			//	return *it;
			return new ibValueReferenceDataObject(metaObject, reference->m_guid);
		}
	}
	return nullptr;
}

ibValueReferenceDataObject* ibValueReferenceDataObject::CreateFromPtr(const ibMetaData* metaData, const ibClassID& refClsid, void* ptr)
{
	ibReference* reference = static_cast<ibReference*>(ptr);
	if (reference != nullptr) {
		const ibValueMetaObjectRecordDataRef* metaObject = MetaObjectFromClsid(metaData, refClsid);
		if (metaObject != nullptr) {
			//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, reference](ibValueReferenceDataObject* ref) {
			//	return metaObject == ref->GetMetaObject() && ref->GetGuid() == reference->m_guid; }
			//);
			//if (it != gs_references.end())
			//	return *it;
			ibValueReferenceDataObject* refData = new ibValueReferenceDataObject(metaObject, reference->m_guid);
			if (refData != nullptr)
				refData->PrepareRef(false);
			return refData;
		}
	}
	return nullptr;
}

bool ibValueReferenceDataObject::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	return false;
}

bool ibValueReferenceDataObject::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (m_metaObject->IsDataReference(id)) {
		if (!ibValueReferenceDataObject::IsEmpty()) {
			pvarMetaVal = ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
			return true;
		}
		pvarMetaVal = ibValueReferenceDataObject::Create(m_metaObject);
		return true;
	}
	auto it = m_listObjectValue.find(id);
	//wxASSERT(it != m_listObjectValue.end());
	if (it != m_listObjectValue.end()) {
		pvarMetaVal = it->second;
		return true;
	}
	return false;
}

// The reference's own hop gate — OUT-OF-LINE because it needs the referenced metaobject COMPLETE to look up the
// field's live type. Reads the id, then validates the pin against that type via CoerceHopType — a composite
// field's UNDEFINED resolves to the pinned twin, a field retyped away from the pin would not.
bool ibValueReferenceDataObject::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	const bool got = GetValueByMetaID(hop.m_id, out);
	const ibValueMetaObjectAttributeBase* attribute = GetMetaObject()->FindAnyAttributeObjectByFilter(hop.m_id);
	return CoerceHopType(hop, out, attribute != nullptr ? attribute->GetTypeDesc() : ibTypeDescription(), GetSourceMetaData()) || got;
}

// The pinned-type twin materialiser — STATIC (see the header). `metaData` comes from the CALLER's own source
// (GetSourceMetaData), so ibSourceDataObject stays metadata-free; the reference — already metadata-bound —
// owns the creation. A live value already of the pinned type passes through untouched.
bool ibValueReferenceDataObject::CoerceHopType(const ibSourceHop& hop, ibValue& out, const ibTypeDescription& filter, const ibMetaData* metaData)
{
	if (!::IsReference(hop.m_type))
		return false;   // no pinned reference branch — keep whatever the id primitive gave
	// STALE-pin guard: if the field carries a type filter, the pin must be among its clsids. A value-table
	// column RETYPED in the designer leaves an OLD pin on a bound path — do NOT fabricate the old twin (else the
	// dead path keeps resolving as a phantom reference). An EMPTY filter (metadata-fixed field) skips the check.
	if (filter.GetClsidCount() > 0 && !filter.ContainType(hop.m_type))
		return false;
	ibSourceDataObject* live = nullptr;
	out.ConvertToValue<ibSourceDataObject>(live);
	if (live != nullptr && live->GetSourceClassType() == hop.m_type)
		return false;   // already the pinned type — never fabricate over a real value
	const std::vector<ibMetaID> pin = ConvertToMetaIds({ hop.m_type }, metaData);   // metadata decode, NOT a body-mask
	if (pin.empty())
		return false;   // pin is not a resolvable reference (no metaData / bad pin)
	ibValue twin = ibValueReferenceDataObject::Create(metaData, pin.front());
	ibSourceDataObject* tw = nullptr;
	twin.ConvertToValue<ibSourceDataObject>(tw);
	if (tw == nullptr)
		return false;   // couldn't build the twin
	out = twin;
	return true;
}

// Reference clsids → their TARGET metaobject ids, resolved through the class factory: a clsid's type ctor must
// be a REFERENCE ctor (GetMetaTypeCtor == _Reference); its metaobject's metaID is the target. metaData-driven —
// the kind-byte shortcut mis-classified composite branches (a clsid that is not a constructive reference id).
// Non-reference clsids (a list / object / primitive branch) are skipped. Pickers call it to enumerate a
// COMPOSITE reference's branches.
std::vector<ibMetaID> ibValueReferenceDataObject::ConvertToMetaIds(const std::vector<ibClassID>& clsids, const ibMetaData* metaData)
{
	std::vector<ibMetaID> targets;
	if (metaData == nullptr)
		return targets;
	for (const ibClassID& clsid : clsids) {
		const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
		if (typeCtor == nullptr || typeCtor->GetMetaTypeCtor() != ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
			continue;
		const ibValueMetaObject* metaObj = typeCtor->GetMetaObject();
		if (metaObj != nullptr)
			targets.push_back(metaObj->GetMetaID());
	}
	return targets;
}


void ibValueReferenceDataObject::ShowValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {
		ibValueRecordDataObject* objValue = nullptr;
		if (metaObject != nullptr && m_objGuid.isValid())
			objValue = metaObject->CreateObjectValue(m_objGuid);
		else
			objValue = metaObject->CreateObjectValue();
		if (objValue != nullptr)
			objValue->ShowFormValue();
	}
}

ibValueRecordDataObjectRef* ibValueReferenceDataObject::GetObject() const
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {
		if (m_newObject)
			return metaObject->CreateObjectValue();
		return metaObject->CreateObjectValue(m_objGuid);
	}
	return nullptr;
}

#include "backend/objCtor.h"

ibClassID ibValueReferenceDataObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

const ibValueMetaObjectGenericData* ibValueReferenceDataObject::GetSourceMetaObject() const
{
	return GetMetaObject();   // RecordData* -> GenericData* (RecordData : GenericData; complete types in this TU)
}

const ibMetaData* ibValueReferenceDataObject::GetSourceMetaData() const
{
	const ibValueMetaObjectRecordData* mo = GetMetaObject();
	return mo != nullptr ? mo->GetMetaData() : nullptr;
}

// A reference vends its TARGET type's columns into the inherited owner-bound m_sourceExplorer — the
// recursion FUEL: hop into a reference VALUE, get THIS explorer, descend by id. Built from the
// referenced metaobject (no DB read; the referenced record's own attributes — scalar dot-walk targets).
const ibSourceExplorer* ibValueReferenceDataObject::GetSourceExplorer() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	if (metaObject == nullptr)
		return nullptr;   // unresolved / empty reference — no target type to describe, so the hop stops here
	m_sourceExplorer.Reset(wxT("Ref"), _("Ref"), metaObject->GetMetaID(), GetClassType(), false, false);
	for (const auto object : metaObject->GetGenericAttributeArrayObject())
		m_sourceExplorer.AppendColumn(object);
	return &m_sourceExplorer;
}

wxString ibValueReferenceDataObject::GetString() const
{
	if (m_newObject)
		return wxEmptyString;
	else if (!m_foundedRef)
		return wxString::Format(wxT("%s <%i:%s>"), _("Not found"), m_metaObject->GetMetaID(), m_objGuid.str());

	wxASSERT(m_metaObject);
	return m_metaObject->GetDataPresentation(this);
}

wxString ibValueReferenceDataObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

enum Func {
	enIsEmpty = 0,
	enGetMetadata,
	enGetObject,
	enGetGuid,
	enPointInTime
};

void ibValueReferenceDataObject::FillMembers(ibMemberTable& helper) const
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		helper.AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
		helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
		helper.AppendFunc(wxT("GetObject"), wxT("GetObject()"));
		helper.AppendFunc(wxT("GetGuid"), wxT("GetGuid()"));
		// ⭐ THE MOMENT COMES WITH THE REFERENCE, for the families that can HAVE one: a catalog
		// element, a document, a chart. It arrives already assembled, so nobody writes
		// `New PointInTime(doc.Date, doc.Ref)` by hand and gets the pair wrong. An enumeration value
		// is not offered it at all — it has no place in the data's history to point at.
		helper.AppendFunc(wxT("PointInTime"), wxT("PointInTime()"));

		wxString objectName;

		//fill custom attributes
		for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
				objectName,
				true,
				false,
				object->GetMetaID(),
				eProperty
			);
		}

		//fill custom tables
		for (const auto object : metaObject->GetTableArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
				objectName,
				true,
				false,
				object->GetMetaID(),
				eTable
			);
		}
	}
	else {
		helper.AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
		helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
	}
}

bool ibValueReferenceDataObject::SetPropVal(const long lPropNum, const ibValue& value)
{
	return false;
}

bool ibValueReferenceDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	ibValueReferenceDataObject::PrepareRef();
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	if (!m_metaObject->IsDataReference(id)) {
		if (lPropAlias == eTable && !GetValueByMetaID(id, pvarPropVal)) {
			m_listObjectValue.insert_or_assign(id,
				new ibValueTabularSectionDataObjectRef(this, m_metaObject->FindTableObjectByFilter(id), !m_newObject)
			);
		}
		if (lPropAlias == eTable && GetValueByMetaID(id, pvarPropVal)) {
			ibValueTabularSectionDataObjectRef* tabularSection = nullptr;
			if (pvarPropVal.ConvertToValue(tabularSection)) {
				if (tabularSection->IsReadAfter()) {
					if (!tabularSection->LoadData(m_objGuid, true)) {
						pvarPropVal.Reset();
						return false;
					}
				}
				return true;
			}
			pvarPropVal.Reset();
			return false;
		}
		return GetValueByMetaID(id, pvarPropVal);
	}

	if (!ibValueReferenceDataObject::IsEmpty()) {
		pvarPropVal = ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
		return true;
	}

	pvarPropVal = ibValueReferenceDataObject::Create(m_metaObject);
	return true;
}

bool ibValueReferenceDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enIsEmpty:
		pvarRetValue = IsEmptyRef();
		return true;
	case enGetMetadata:
		pvarRetValue = GetMetaObject();
		return true;
	case enGetObject:
		pvarRetValue = GetObject();
		return true;
	case enGetGuid:
		pvarRetValue = new ibValueGuid(m_objGuid);
		return true;
	case enPointInTime: {
		// A REFERENCE'S MOMENT IS ITS IDENTITY; the DATE is added by the family that has one (the
		// document overrides this method with its own date). No virtual asking every class "do you
		// have a date" and no list of the ones that do -- the two classes each answer for themselves.
		//
		// It carries THIS reference, not a copy: `GetValue(true)` is the verb for handing out this
		// very value, where a second ibValueReferenceDataObject over the same (type, guid) would be
		// a second object for one identity -- the thing a reference exists to prevent.
		pvarRetValue = new ibValuePointInTime(wxDateTime(), GetValue(true));
		return true;
	}
	}

	return false;
}
////////////////////////////////////////////////////////////////////////////
// Serialization — a reference travels as IDENTITY
////////////////////////////////////////////////////////////////////////////
//
// The header already carries the type, so the contents are just the guid. NOT
// the object: an object belongs to a session and a connection, and the far side
// re-reads it under its OWN rights — which is exactly why a job is handed a
// reference rather than a loaded object.
//
// An empty reference writes an empty guid and comes back as an empty reference
// OF THAT TYPE, not as an untyped nothing: a reference that has not been filled
// in is still a reference, and the far side must be able to compare and assign
// it without a type error.

#include "backend/serialize/dataBuilder.h"

bool ibValueReferenceDataObject::DoSerialize(ibDataNode& node) const
{
	// THE metaID, not only the class id.
	//
	// A reference's class id is DERIVED from the metaobject's metaID, so within
	// one configuration either identifies the type. Across configurations they
	// part company: the same catalog can carry a different id in a copy of the
	// base, and a stored setting or an exchange parcel then restores a reference
	// pointing at whatever type happens to hold that id — silently, and to the
	// wrong table.
	//
	// Writing the metaID as well makes the identity say what it means: this type,
	// of this metaobject. The class id stays in the header for the fast path;
	// this is what a reader consults when the fast path is not enough.
	if (m_metaObject != nullptr)
		node.SetValue(wxT("m"), (s32)m_metaObject->GetMetaID());

	node.SetValue(wxT("g"), m_reference_impl != nullptr
		? ibGuid(m_reference_impl->m_guid).str()
		: wxString());
	return true;
}

bool ibValueReferenceDataObject::DoDeserialize(const ibDataNode& node)
{
	if (m_reference_impl == nullptr)
		return false;

	// THE metaID DECIDES, when it was written. We were created from the class id
	// in the header, which is the fast path and is enough within one base; this
	// is the check that the type we were created as is the type that was stored.
	// A mismatch means the id landed on a different metaobject in this base —
	// refuse rather than hand back a reference into the wrong table.
	//
	// Absent (0) is not a mismatch: it is a value written before the metaID went
	// into the payload, and the class id is all it ever had.
	const ibMetaID storedMetaId = (ibMetaID)node.GetValue<s32>(wxT("m"));
	if (storedMetaId != 0 && m_metaObject != nullptr
		&& storedMetaId != m_metaObject->GetMetaID())
		return false;

	// A malformed guid reads as the empty one rather than raising: the header
	// was well formed, so this is data from a base that knew something we do
	// not — degrade, do not fail the whole read.
	const ibGuid restored(node.GetValue<wxString>(wxT("g")));
	m_reference_impl->m_guid = restored;

	// AND THE IDENTITY THE REST OF THE CLASS READS. Writing only the impl left the object
	// still calling itself NEW (it was constructed empty, from the class id in the header,
	// and m_newObject was decided there): the guid was right, so the reference filtered,
	// compared and saved correctly — and presented as an EMPTY STRING, because GetString
	// answers "" for a new object before it ever looks anything up. A stored list filter
	// came back with its value invisible while plainly still in force.
	//
	// The init flags are cleared first: this object may already have "prepared" itself as the
	// empty one it was a moment ago, and PrepareRef returns early on that flag. Then it prepares
	// for real — the same step Create(metaObject, guid) takes, and the step that decides whether
	// the identity is FOUND. Without it the value would read "Not found" instead of its name,
	// which is the same defect wearing different clothes.
	m_objGuid = restored;
	m_newObject = !restored.isValid();
	m_initializedRef = false;
	m_foundedRef = false;
	PrepareRef(true);
	return true;
}