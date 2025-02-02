////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaData 
////////////////////////////////////////////////////////////////////////////

#include "metaData.h"

#include "backend/compiler/enumFactory.h"
#include "backend/metaCollection/partial/contextManager.h"
#include "backend/systemManager/systemManager.h"
#include "backend/debugger/debugServer.h"
#include "backend/debugger/debugClient.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/appData.h"



//**************************************************************************************************
//*                                          IMetaData											   *
//**************************************************************************************************

//ID's 
meta_identifier_t IMetaData::GenerateNewID() const
{
	IMetaObject* commonObject = GetCommonMetaObject();
	wxASSERT(commonObject);
	meta_identifier_t id = commonObject->GetMetaID() + 1;
	DoGenerateNewID(id, commonObject);
	return id;
}

void IMetaData::DoGenerateNewID(meta_identifier_t& id, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		meta_identifier_t newID = child->GetMetaID() + 1;
		if (newID > id) {
			id = newID;
		}
		DoGenerateNewID(id, child);
	}
}

IMetaObject* IMetaData::CreateMetaObject(const class_identifier_t& clsid, IMetaObject* metaParent)
{
	wxASSERT(clsid != 0);

	CValue* ppParams[] = { metaParent };
	IMetaObject* newMetaObject = nullptr;

	try {
		newMetaObject = CValue::CreateAndConvertObjectRef<IMetaObject>(clsid, ppParams, 1);
		newMetaObject->IncrRef();
	}
	catch (...) {
		return nullptr;
	}

	if (newMetaObject != nullptr) {

		newMetaObject->SetName(
			GetNewName(clsid, metaParent, newMetaObject->GetClassName())
		);

		//always create metabject
		bool success = newMetaObject->OnCreateMetaObject(this);

		//first initialization
		if (!success || !newMetaObject->OnLoadMetaObject(this)) {
			if (metaParent != nullptr) {
				metaParent->RemoveChild(newMetaObject);
			}
			wxDELETE(newMetaObject);
			return nullptr;
		}

		//and running initialization
		if (!success || !newMetaObject->OnBeforeRunMetaObject(newObjectFlag)) {
			if (metaParent != nullptr) {
				metaParent->RemoveChild(newMetaObject);
			}
			wxDELETE(newMetaObject);
			return nullptr;
		}

		Modify(true);

		if (!success || !newMetaObject->OnAfterRunMetaObject(newObjectFlag)) {
			if (metaParent != nullptr) {
				metaParent->RemoveChild(newMetaObject);
			}
			wxDELETE(newMetaObject);
			return nullptr;
		}
	}

	return newMetaObject;
}

wxString IMetaData::GetNewName(const class_identifier_t& clsid, IMetaObject* metaParent, const wxString& prefix, bool forConstructor)
{
	unsigned int countRec = forConstructor ?
		0 : 1;

	wxString currPrefix = prefix.Length() > 0 ?
		prefix : wxT("newItem");

	wxString newName = forConstructor ?
		wxString::Format("%s", currPrefix) :
		wxString::Format("%s%d", currPrefix, countRec);

	while (forConstructor ||
		countRec > 0) {
		bool foundedName = false;

		if (metaParent) {
			for (auto& obj : metaParent->GetObjects(clsid)) {
				if (obj->IsDeleted())
					continue;
				if (newName == obj->GetName()) {
					foundedName = true; break;
				}
			}
		}

		if (!foundedName)
			break;

		newName = wxString::Format("%s%d", currPrefix, ++countRec);
	}

	return newName;
}

std::vector<IMetaObject*> IMetaData::GetMetaObject(const IMetaObject* top) const
{
	std::vector<IMetaObject*> metaObjects;
	DoGetMetaObject(metaObjects, top != nullptr ? top : GetCommonMetaObject());
	return metaObjects;
}

std::vector<IMetaObject*> IMetaData::GetMetaObject(const class_identifier_t& clsid, const IMetaObject* top) const
{
	std::vector<IMetaObject*> metaObjects;
	DoGetMetaObject(clsid, metaObjects, top != nullptr ? top : GetCommonMetaObject());
	return metaObjects;
}

void IMetaData::DoGetMetaObject(std::vector<IMetaObject*>& list, const IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		DoGetMetaObject(list, child);
	}
	if (top->IsDeleted()) return;
	list.push_back(const_cast<IMetaObject*>(top));
}

void IMetaData::DoGetMetaObject(const class_identifier_t& clsid, std::vector<IMetaObject*>& list, const IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		DoGetMetaObject(clsid, list, child);
	}

	if (top->IsDeleted()) return;
	if (clsid == top->GetClassType()) list.push_back(const_cast<IMetaObject*>(top));
}

IMetaObject* IMetaData::FindByName(const wxString& fullName) const
{
	return DoFindByName(fullName, GetCommonMetaObject());
}

IMetaObject* IMetaData::DoFindByName(const wxString& strFileName, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		IMetaObject* foundedMeta = DoFindByName(strFileName, child);
		if (foundedMeta != nullptr)
			return foundedMeta;
	}
	if (top->IsDeleted()) return nullptr;
	if (strFileName == top->GetDocPath()) return top;
	return nullptr;
}

IMetaObject* IMetaData::GetMetaObject(const meta_identifier_t& id, IMetaObject* top) const
{
	if (id == wxNOT_FOUND)
		return nullptr;
	return DoGetMetaObject(id, top != nullptr ? top : GetCommonMetaObject());
}

IMetaObject* IMetaData::GetMetaObject(const Guid& guid, IMetaObject* top) const
{
	if (!guid.isValid())
		return nullptr;
	return DoGetMetaObject(guid, top != nullptr ? top : GetCommonMetaObject());
}

IMetaObject* IMetaData::DoGetMetaObject(const meta_identifier_t& id, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		IMetaObject* foundedMeta = DoGetMetaObject(id, child);
		if (foundedMeta != nullptr)
			return foundedMeta;
	}

	if (id == top->GetMetaID()) return top;
	return nullptr;
}

IMetaObject* IMetaData::DoGetMetaObject(const Guid& guid, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		IMetaObject* foundedMeta = DoGetMetaObject(guid, child);
		if (foundedMeta != nullptr)
			return foundedMeta;
	}

	if (guid == top->GetGuid()) return top;
	return nullptr;
}

bool IMetaData::RenameMetaObject(IMetaObject* metaObject, const wxString& newName)
{
	bool foundedName = false;

	for (auto& obj : GetMetaObject(metaObject->GetClassType())) {
		if (obj->GetParent() != metaObject->GetParent())
			continue;
		if (obj != metaObject &&
			stringUtils::CompareString(newName, obj->GetName())) {
			foundedName = true;
			break;
		}
	}

	if (foundedName) return false;

	if (metaObject->OnRenameMetaObject(newName)) {
		Modify(true); return true;
	}
	return false;
}

void IMetaData::RemoveMetaObject(IMetaObject* metaObject, IMetaObject* parent)
{
	IMetaObject* objParent = parent ?
		parent : metaObject->GetParent();

	if (metaObject->OnAfterCloseMetaObject()) {
		if (metaObject->OnDeleteMetaObject()) {
			for (auto child : metaObject->GetObjects()) {
				RemoveMetaObject(child, metaObject);
			}
			metaObject->MarkAsDeleted();
		}

		metaObject->OnReloadMetaObject();
		Modify(true);
	}
}