#ifndef __META_SECTION_OBJECT_H__
#define __META_SECTION_OBJECT_H__

#include "metaObject.h"
#include "metaCommandObject.h"   // ibValueMetaObjectCommand — GetCommandArrayObject returns the real command type

class BACKEND_API ibValueMetaObjectSection : public ibValueMetaObject, public ibBackendCommandSender {
	public:

	// ibBackendCommandSender — a section is command-capable: a section id, when reached, HOPS WITHIN ITSELF to a
	// sub-section, its own command, or an interface item (each itself command-capable / a terminal). Mirror of a
	// source hop — the walk self-describes.
	virtual bool GetCommandByHop(const ibCommandHop& hop, ibValue& out) override;

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

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaSectionCLSID ||
			clsid == g_metaCommonCommandCLSID)   // a section owns its own commands (config-scope, common command)
			return clsid;
		return 0;
	}

	ibValueMetaObjectSection(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

#pragma region __array_h__

	//interface
	std::vector<ibValueMetaObjectSection*> GetInterfaceArrayObject() const {
		std::vector<ibValueMetaObjectSection*> array;
		FillArrayObjectByFilter<ibValueMetaObjectSection>(array, { g_metaSectionCLSID });
		return array;
	}
	std::vector<ibValueMetaObjectSection*> GetInterfaceArrayObject(
		std::vector<ibValueMetaObjectSection*>& array) const {
		FillArrayObjectByFilter<ibValueMetaObjectSection>(array, { g_metaSectionCLSID });
		return array;
	}

	//commands (section scope) — a section owns its own commands, like a business object
	std::vector<ibValueMetaObjectCommand*> GetCommandArrayObject(
		std::vector<ibValueMetaObjectCommand*> array = std::vector<ibValueMetaObjectCommand*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectCommand>(array, { g_metaCommonCommandCLSID });
		return array;
	}

#pragma endregion

	bool GetInterfaceItemArrayObject(ibInterfaceCommandSection page,
		std::vector<ibValueMetaObject*>& array) const;

#pragma region __filter_h__

	//interface
	template <typename _T1>
	ibValueMetaObjectSection* FindInterfaceObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectSection>(id, { g_metaSectionCLSID });
	}

#pragma endregion 

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryContext, wxT("Picture"), _("Picture"));
#pragma region role
	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));
#pragma endregion
};

#endif 