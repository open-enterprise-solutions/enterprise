#ifndef __ROLE_HELPER_H__
#define __ROLE_HELPER_H__

#include "backend_core.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibAccessObject;

//********************************************************************************************
//*										 Role												 *
//********************************************************************************************

class BACKEND_API ibRole {
public:

	wxString GetName() const { return m_roleName; }
	wxString GetLabel() const { return m_roleLabel.IsEmpty() ? m_roleName : m_roleLabel; }

	ibAccessObject* GetRoleObject() const { return m_owner; }

	bool GetDefValue() const { return m_defValue; }

protected:

	ibRole(ibAccessObject* metaObject, const wxString& roleName, const wxString& roleLabel,
		const bool& value = true) :
		m_roleName(roleName),
		m_roleLabel(roleLabel),
		m_owner(metaObject),
		m_defValue(value)
	{
		InitRole(metaObject, value);
	}

private:

	friend class ibAccessObject;

	void InitRole(ibAccessObject* obj, const bool& value = true);

	wxString m_roleName;
	wxString m_roleLabel;
	ibAccessObject* m_owner; // pointer to the owner object
	bool m_defValue;  // handler function name
};

struct ibRoleUserInfo {

	ibRoleUserInfo() {}
	ibRoleUserInfo(const ibRoleID& id) : m_arrayRole({ id }) {}
	ibRoleUserInfo(const std::vector<ibRoleID>& array) : m_arrayRole(array) {}

	bool IsSetRole() const { return m_arrayRole.size() > 0; }

	std::vector<ibRoleID> m_arrayRole;
};

#include "backend/fileSystem/fs.h"

class BACKEND_API ibAccessObject {
public:

	virtual ~ibAccessObject();

	bool AccessRight(const ibRole* role) const { return AccessRight(role, GetUserRoleInfo()); }
	bool AccessRight(const wxString& strRoleName) const { return AccessRight(strRoleName, GetUserRoleInfo()); }

	bool AccessRight(const ibRole* role, const ibRoleUserInfo& roleInfo) const;
	bool AccessRight(const wxString& strRoleName, const ibRoleUserInfo& roleInfo) const {
		if (!strRoleName.IsEmpty()) {
			auto iterator = std::find_if(m_roles.begin(), m_roles.end(),
				[strRoleName](const auto& pair) { return stringUtils::CompareString(strRoleName, pair.first); });
			if (iterator != m_roles.end()) return AccessRight(iterator->second, roleInfo);
		}
		return false;
	}

	bool SetRight(const ibRole* role, const ibRoleID& id, const bool& val);
	bool SetRight(const wxString& strRoleName, const ibRoleID& id, const bool& val) {
		if (!strRoleName.IsEmpty()) {
			auto iterator = std::find_if(m_roles.begin(), m_roles.end(),
				[strRoleName](const auto& pair) { return stringUtils::CompareString(strRoleName, pair.first); });
			if (iterator != m_roles.end()) return SetRight(iterator->second, id, val);
		}
		return false;
	}

	/**
	* Obtiene la propiedad identificada por el nombre.
	*
	* @note Notar que no existe el método SetProperty, ya que la modificación
	*       se hace a través de la referencia.
	*/
	ibRole* GetRole(const wxString& nameParam) const;
	ibRole* GetRole(unsigned int idx) const; // throws ...;

	/**
	* Obtiene el número de propiedades del objeto.
	*/
	unsigned int GetRoleCount() const {
		return (unsigned int)m_roles.size();
	}

	friend class ibRole;

protected:

	virtual bool DoAccessRight(const ibRole* role) const { return true; }
	virtual void DoSetRight(const ibRole* role, const bool& set) {}

	//Check is full access 
	virtual bool IsFullAccess() const { return false; }

	//Create user info
	virtual ibRoleUserInfo GetUserRoleInfo() const = 0;

	//load & save role in metaobject 
	bool LoadRole(ibReaderMemory& reader);
	bool SaveRole(ibWriterMemory& writer) const;

	/**
	* Añade una propiedad al objeto.
	*
	* Este método será usado por el registro de descriptores para crear la
	* instancia del objeto.
	* Los objetos siempre se crearán a través del registro de descriptores.
	*/
	void AddRole(ibRole* value);

	template <typename... Args>
	inline ibRole* CreateRole(Args&&... args) {
		return new ibRole(this, std::forward<Args>(args)...);
	}

	std::map<ibRoleID,
		std::map<wxString, bool>
	> m_valRoles;

	std::vector<std::pair<wxString, ibRole*>> m_roles;
};

#endif