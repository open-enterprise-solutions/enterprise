#ifndef __ROLE_HELPER_H__
#define __ROLE_HELPER_H__

#include "backend_core.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibAccessObject;

//********************************************************************************************
//*                                  Role composition                                        *
//********************************************************************************************

// How a role COMBINES with the user's other roles. It lives HERE, at the top of the role layer,
// because both users of it are below the metadata: the runtime role list (ibRoleUserInfo) and the
// rights fold (ibAccessObject::AccessRight). The Role METAOBJECT only carries it as a property.
//
// Named after the SET operation rather than the boolean one, because that is what the fold does:
//
//   Union        - the role ADDS to what is permitted; one of them granting is enough. An ordinary
//                  right, and the DEFAULT — anything that does not say otherwise is one, so an
//                  existing configuration behaves exactly as it did.
//   Intersection - the role GRANTS NOTHING and SUBTRACTS: what it denies stays denied whatever the
//                  union roles granted, and no further role can widen past it. This is what a data
//                  SEPARATOR is (organisation / division / clearance): declared once instead of
//                  copied into every role, so rights and separators ADD (N+M) instead of
//                  multiplying (N×M).
//
// The verdict is (union1 OR union2 …) AND intersection1 AND intersection2 …, so the ORDER roles are
// assigned in never changes the answer — unlike an ACL with DENY, where it decides the outcome. A
// third "deny" mode would add no expressive power: it is an intersection with a negated condition.
//
// A role that says NOTHING participates in neither fold. Without that neutrality a separator would
// strip every right / hide every table it was never told about.
enum ibRoleCompositionMode {
	ibRoleCompositionMode_Union,
	ibRoleCompositionMode_Intersection
};

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
	bool m_defValue;  // the answer when nothing was ever set for this right
};

// ONE of the user's roles at run time: which role, and how it combines. A single list of these —
// splitting them into two would state the same fact twice and let the halves drift.
struct ibUserRoleEntry {

	ibUserRoleEntry() {}
	ibUserRoleEntry(const ibRoleID& id, const ibRoleCompositionMode& mode = ibRoleCompositionMode_Union)
		: m_id(id), m_mode(mode) {}

	bool IsUnion() const { return m_mode == ibRoleCompositionMode_Union; }

	ibRoleID m_id = wxNOT_FOUND;
	ibRoleCompositionMode m_mode = ibRoleCompositionMode_Union;
};

struct ibRoleUserInfo {

	ibRoleUserInfo() {}
	ibRoleUserInfo(const ibRoleID& id) : m_arrayRole({ ibUserRoleEntry(id) }) {}
	ibRoleUserInfo(const std::vector<ibUserRoleEntry>& array) : m_arrayRole(array) {}

	bool IsSetRole() const { return m_arrayRole.size() > 0; }

	// The user's roles, each carrying its own composition mode (see ibRoleCompositionMode above).
	// A restricting role participates only where it SAYS something: "never flipped in the editor" is
	// already distinguishable from "cleared" — the value map holds an entry only for a right that was
	// actually set (see ibAccessObject::AccessRight) — so neutrality needs no new stored state.
	std::vector<ibUserRoleEntry> m_arrayRole;
};

#include "backend/fileSystem/fs.h"

class BACKEND_API ibAccessObject {
public:

	virtual ~ibAccessObject();

	// How THIS object combines when it is used as a role. Union for everything that is not a role —
	// asked through the base so a caller collecting the user's roles never has to know the Role
	// metatype (the Role metaobject overrides it from its own property).
	virtual ibRoleCompositionMode GetRoleCompositionMode() const { return ibRoleCompositionMode_Union; }

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

	// Do this object's rights control a ROW, or the whole TABLE? A property of the OBJECT — one
	// answer for all of its rights, not something an individual right entry carries.
	//
	// ROW is the DEFAULT, because that is what data normally is: a catalog element, a document, a
	// constant — each exists as itself, and a write that touched nothing about it is worth a second
	// question. A REGISTER says otherwise: its set is addressed by its recorder and may be
	// legitimately empty, so it overrides this to control the table instead.
	virtual bool IsAccessPerRecord() const { return true; }

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