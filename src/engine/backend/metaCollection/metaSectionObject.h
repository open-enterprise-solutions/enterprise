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
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

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

	// The items a platform group shows — a command filed under a DECLARED group is not among them (see below).
	bool GetInterfaceItemArrayObject(ibInterfaceCommandSection page,
		std::vector<ibValueMetaObject*>& array) const;

	// …and the items filed under a group the configuration DECLARES — a command that names it in its Group.
	// Such a command sits in no platform area, so the overload above leaves it out and this one takes it in.
	bool GetInterfaceItemArrayObject(const class ibValueMetaObjectCommandGroup* group,
		std::vector<ibValueMetaObject*>& array) const;

	// ⭐ THE SAME QUESTION WITHOUT AN AREA: everything the section shows, once each — its Important, Default,
	// Create, Report and Service items in that order, then those of the declared groups, an object listed the
	// first time it appears.
	//
	// 🛑 THE FOUR AREAS ARE NOT FOUR DISJOINT LISTS. A catalog is "combined" — it is both a list and a
	// create — and the overload above answers it for BOTH Default and Create (see there). A page that asks
	// the four areas one after another into one array therefore holds every catalog TWICE: the section page
	// of the running application drew each of them twice, under one heading, both leading to the same list.
	// First appearance wins, so a combined object is the list entry and not a second "create" that opens the
	// list again.
	std::vector<ibValueMetaObject*> GetInterfaceItemArrayObject() const;

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
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryContext, wxT("Picture"), _("Picture"),
		_("The section's icon in the navigation panel. Empty: the configuration's own icon is shown."));
#pragma region role
	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));
#pragma endregion
};

#endif 