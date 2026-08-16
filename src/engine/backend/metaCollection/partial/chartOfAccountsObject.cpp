////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of accounts object
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccounts.h"
#include "backend/system/value/valuePointInTime.h"   // the moment an object can be asked for
#include "backend/metaData.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "reference/reference.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/system/systemManager.h"
#include "backend/fileSystem/fs.h"

ibValueRecordDataObjectChartOfAccounts::ibValueRecordDataObjectChartOfAccounts(const ibValueMetaObjectChartOfAccounts* metaObject, const ibGuid& objGuid, ibObjectMode objMode) :
	ibValueRecordDataObjectHierarchyRef(metaObject, objGuid, objMode) {
	m_members.Bind(this, &ibValueRecordDataObjectChartOfAccounts::FillMethods);
}

ibValueRecordDataObjectChartOfAccounts::ibValueRecordDataObjectChartOfAccounts(const ibValueRecordDataObjectChartOfAccounts& source) :
	ibValueRecordDataObjectHierarchyRef(source) {
	m_members.Bind(this, &ibValueRecordDataObjectChartOfAccounts::FillMethods);
}

bool ibValueRecordDataObjectChartOfAccounts::SaveData()
{
	ibValueMetaObjectChartOfAccounts* metaRef = nullptr;
	if (m_metaObject->ConvertToValue(metaRef) && metaRef != nullptr) {

		ibValueMetaObjectTableData* kindsTable = metaRef->GetAccountDimensionKindsTable();
		if (kindsTable != nullptr && !kindsTable->IsDeleted()) {

			ibValue tableValue;
			if (GetValueByMetaID(kindsTable->GetMetaID(), tableValue)) {

				ibValueModel* rows = nullptr;
				if (tableValue.ConvertToValue(rows) && rows != nullptr) {

					// The ceiling is SCHEMA — declared by this chart, and the register builds that
					// many columns from it. Refusing here is the only place that cannot be walked
					// around, and saying the number keeps the message actionable: what the author
					// has to change is either this table or the declaration.
					const unsigned int maxCount = metaRef->GetMaxAccountDimensionCount();
					if (rows->GetRowCount() > static_cast<long>(maxCount))
						ibBackendCoreException::Error(
							_("Account \"%s\" declares %i analytics, but the chart of accounts allows %i"),
							GetString(), (int)rows->GetRowCount(), (int)maxCount);

					// A KIND MAY BE NAMED ONCE. Two rows with the same kind are not two analytics —
					// they are one, declared twice, and nothing downstream can tell which of them a
					// movement meant: the register addresses a slot BY ITS KIND, so a repeat makes
					// the address ambiguous and one of the two silently unreachable. Cheapest here,
					// where the rows are already in hand and an import cannot go round it.
					ibValueMetaObjectAttributeBase* kindAttr =
						metaRef->GetAccountDimensionKindsTable()->GetAccountDimensionKind();
					if (kindAttr != nullptr) {
						std::vector<ibValue> seen;
						for (long line = 0; line < rows->GetRowCount(); line++) {
							ibValue kind;
							if (!rows->GetValueByMetaID(rows->GetItem(line), kindAttr->GetMetaID(), kind))
								continue;
							if (kind.IsEmpty())
								continue;   // emptiness is the FILL check's complaint, not this one
							for (const ibValue& earlier : seen) {
								if (earlier.CompareValueEQ(kind))
									ibBackendCoreException::Error(
										_("Account \"%s\": the analytics kind \"%s\" is declared more than once"),
										GetString(), kind.GetString());
							}
							seen.push_back(kind);
						}

						// ⭐ THE SECTION, UNFOLDED INTO THE ACCOUNT'S OWN COLUMNS — written here, where
						// the rows are already in hand and already checked.
						//
						// Column N takes row N: the position IS the correspondence, the same rule the
						// register's slots follow. Filled on every save rather than maintained
						// incrementally, because the section is small and a rewrite cannot drift —
						// rows removed leave their columns EMPTY rather than holding a stale kind.
						for (unsigned int idx = 0; idx < metaRef->GetAccountDimensionKindColumnCount(); idx++) {
							ibValueMetaObjectAttributePredefined* column = metaRef->GetAccountDimensionKindColumn(idx);
							if (column == nullptr || column->IsDeleted())
								continue;

							ibValue kind;
							if (static_cast<long>(idx) < rows->GetRowCount())
								rows->GetValueByMetaID(rows->GetItem(idx), kindAttr->GetMetaID(), kind);

							SetValueByMetaID(column->GetMetaID(), kind);
						}
					}
				}
			}
		}
	}

	return ibValueRecordDataObjectHierarchyRef::SaveData();
}

const ibSourceExplorer* ibValueRecordDataObjectChartOfAccounts::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(), false);
	ibValueMetaObjectChartOfAccounts* metaRef = nullptr;

	if (m_metaObject->ConvertToValue(metaRef)) {
		// THE CODE IS TYPED IN, not handed out. In a catalog the code is a serial number the system
		// mints, so it is shown and not edited; in a chart of accounts the code IS the account — "51",
		// "60.01" — and it is the first thing a person writes when adding one. Locking it made an
		// account impossible to name at all.
		m_sourceExplorer.AppendColumn(metaRef->GetDataCode());
		m_sourceExplorer.AppendColumn(metaRef->GetDataDescription());
		m_sourceExplorer.AppendColumn(metaRef->GetDataParent());
		// WHAT KIND OF ACCOUNT THIS IS. It was missing here, so a generated form showed an account as
		// if it were a plain catalog item — code, description, parent — with no way to say whether it
		// is active, passive or both. It is a declared TYPE (the AccountType enumeration), so the form
		// builds the editor from the attribute itself; nothing here spells the three members out.
		m_sourceExplorer.AppendColumn(metaRef->GetAccountType());
		// Off-balance belongs beside it: it is not a way of KEEPING the account, it is a statement
		// about the account itself — this one stands outside the balance — and it is answered once,
		// per account, like the kind is.
		m_sourceExplorer.AppendColumn(metaRef->GetOffBalance());

		// Quantitative / Currency are deliberately NOT here, and they are not standard attributes of
		// an account at all. They are two booleans spelling out ONE fact — how the account is KEPT —
		// which is an accounting KIND: declared once on the chart and then ticked per account, the
		// same shape the analytics kinds already have. Putting them on the form as two checkboxes
		// would fix the bit-encoded form in the interface before that mechanism exists.
		// (The analytics-kinds section is NOT appended by hand here. It used to be, because it was the
		//  only way to reach it: its clsid was missing from the tabular-section filter, so the general
		//  loop below walked straight past it. Now that the filter knows it, adding it here as well
		//  put the section into the source TWICE — and a generated form grew two identical tableboxes.
		//  A special case that outlives the gap it patched becomes a duplicate.)
	}
	
	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item || attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) m_sourceExplorer.AppendColumn(object);
			}
		} else {
			if (attrUse == ibItemMode::ibItemMode_Folder || attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) m_sourceExplorer.AppendColumn(object);
			}
		}
	}

	for (const auto object : m_metaObject->GetTableArrayObject()) {
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item || tableUse == ibItemMode::ibItemMode_Folder_Item) {
				if (object != nullptr && !object->IsDeleted()) {
					ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
					for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol);
				}
			}
		} else {
			if (tableUse == ibItemMode::ibItemMode_Folder || tableUse == ibItemMode::ibItemMode_Folder_Item) {
				if (object != nullptr && !object->IsDeleted()) {
					ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
					for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol);
				}
			}
		}
	}
	
	return &m_sourceExplorer;
}

// ShowFormValue / GetFormValue moved up to HierarchyRef.

// WriteObject / DeleteObject inherited from
// ibValueRecordDataObjectHierarchyRef — see commonObjectRefQuery.cpp.

enum Func { enPointInTime, enIsNew, enCopy, enFill, enWrite, enDelete, enModified, enGetForm, enGetTemplate, enGetMetadata, enLock, enUnlock };

void ibValueRecordDataObjectChartOfAccounts::FillMethods(ibMemberTable& helper) const
{
	// Own methods; the data members come from the base FillDataMembers. Order is
	// load-bearing — CallAsFunc switches on the method index (enIsNew = 0 …).
	// ⭐ THE MOMENT, ON EVERY REFERENCE-BASED FAMILY. Being addressed by a reference is the whole
	// qualification: an element of this kind has a place in the data's history, so it can be named
	// as a moment -- for a period boundary, for an ordering, for "everything up to THIS one". The
	// families with a date of their own add it; the rest carry the reference alone, which is an
	// identity with no point on a timeline rather than a date invented to fill the slot.
	helper.AppendFunc(wxT("PointInTime"), wxT("PointInTime()"));
	helper.AppendFunc(wxT("IsNew"), wxT("IsNew()"));
	helper.AppendFunc(wxT("Copy"), wxT("Copy()"));
	helper.AppendFunc(wxT("Fill"), 1, wxT("Fill(object)"));
	helper.AppendFunc(wxT("Write"), wxT("Write()"));
	helper.AppendFunc(wxT("Delete"), wxT("Delete()"));
	helper.AppendFunc(wxT("Modified"), wxT("Modified()"));
	helper.AppendFunc(wxT("GetFormObject"), 3, wxT("GetFormObject(name : string, owner : any , id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
	helper.AppendProc(wxT("Lock"),   wxT("Lock()"));
	helper.AppendProc(wxT("Unlock"), wxT("Unlock()"));
}

bool ibValueRecordDataObjectChartOfAccounts::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) { if (m_procUnit != nullptr) return m_procUnit->SetPropVal(GetPropName(lPropNum), varPropVal); }
	else if (lPropAlias == eProperty) return SetValueByMetaID(m_members.GetPropData(lPropNum), varPropVal);
	return false;
}

bool ibValueRecordDataObjectChartOfAccounts::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) { if (m_procUnit != nullptr) return m_procUnit->GetPropVal(GetPropName(lPropNum), pvarPropVal); }
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (m_metaObject->IsDataReference(lPropData)) { pvarPropVal = GetReference(); return true; }
		return GetValueByMetaID(lPropData, pvarPropVal);
	}
	return false;
}

bool ibValueRecordDataObjectChartOfAccounts::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enIsNew: pvarRetValue = m_newObject; return true;
	case enCopy: pvarRetValue = CopyObject(); return true;
	case enFill: FillObject(*paParams[0]); return true;
	case enWrite: WriteObject(); return true;
	case enDelete: DeleteObject(); return true;
	case enPointInTime:
		pvarRetValue = new ibValuePointInTime(wxDateTime(), GetReference());
		return true;
	case enModified: pvarRetValue = m_objModified; return true;
	case Func::enGetForm: pvarRetValue = GetFormValue(lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString), lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr); return true;
	case Func::enGetTemplate: pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString()); return true;
	case Func::enGetMetadata: pvarRetValue = m_metaObject; return true;
	case Func::enLock:   TryAcquireFormLock(); return true;
	case Func::enUnlock: ReleaseFormLock();    return true;
	}
	return ibRuntimeModuleDataObject::ExecAsFunc(GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray);
}
