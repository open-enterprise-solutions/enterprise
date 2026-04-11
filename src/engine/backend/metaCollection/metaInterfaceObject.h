#ifndef __META_INTERFACE_H__
#define __META_INTERFACE_H__

#include "metaObject.h"

class BACKEND_API ibValueMetaObjectInterface : public ibValueMetaObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectInterface);
protected:

	enum
	{
		ID_METATREE_OPEN_INTERFACE = 19000,
	};

public:

#pragma region access
	bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }
#pragma endregion

	wxBitmap GetPictureAsBitmap() const {
		if (!m_propertyPicture->IsEmptyProperty())
			return m_propertyPicture->GetValueAsBitmap();
		return ibBackendPicture::CreatePicture(g_metaCommonMetadataCLSID);
	}

	virtual bool FilterChild(const ibClassID& clsid) const {

		if (
			clsid == g_metaInterfaceCLSID
			)
			return true;

		return false;
	}

	ibValueMetaObjectInterface(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

#pragma region __array_h__

	//interface
	std::vector<ibValueMetaObjectInterface*> GetInterfaceArrayObject() const {
		std::vector<ibValueMetaObjectInterface*> array;
		FillArrayObjectByFilter<ibValueMetaObjectInterface>(array, { g_metaInterfaceCLSID });
		return array;
	}
	std::vector<ibValueMetaObjectInterface*> GetInterfaceArrayObject(
		std::vector<ibValueMetaObjectInterface*>& array) const {
		FillArrayObjectByFilter<ibValueMetaObjectInterface>(array, { g_metaInterfaceCLSID });
		return array;
	}

#pragma endregion

	bool GetInterfaceItemArrayObject(ibInterfaceCommandSection page,
		std::vector<ibValueMetaObject*>& array) const;

#pragma region __filter_h__

	//interface
	template <typename _T1>
	ibValueMetaObjectInterface* FindInterfaceObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectInterface>(id, { g_metaInterfaceCLSID });
	}

#pragma endregion 

protected:

	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryContext, wxT("Picture"), _("Picture"));
#pragma region role
	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));
#pragma endregion
};

#endif 