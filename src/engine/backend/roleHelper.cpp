#include "roleHelper.h"

#define roleBlock 0x200000

////////////////////////////////////////////////////////////////////////

void ibRole::InitRole(ibAccessObject* obj, const bool& value)
{
	m_owner->AddRole(this);
	m_defValue = value;
}

////////////////////////////////////////////////////////////////////////

#pragma region role 

ibAccessObject::~ibAccessObject()
{
	for (auto& role : m_roles) { wxDELETE(role.second); }
	m_roles.clear();
}

void ibAccessObject::AddRole(ibRole* role)
{
	m_roles.emplace_back(role->GetName(), role);
}

bool ibAccessObject::AccessRight(const ibRole* role, const ibRoleUserInfo& roleInfo) const
{
	if (role == nullptr || !DoAccessRight(role))
		return false;

	// UNSET is not the same as DENIED, and the answer for it is not hard-wired here: every right
	// DECLARES its own default where it is created (CreateRole(name, label, defValue) — allowed
	// unless the declaration says otherwise). That default is what a user with no roles gets, and
	// what a role that never had this particular right flipped in the editor contributes. Reading
	// it off the role is also what makes the designer's checkbox and this gate agree: the editor
	// paints an unset right by calling right here.
	const bool defValue = role->GetDefValue();
	bool access = defValue;

	// What THIS role says about the right, or nothing at all. "Never flipped in the editor" has no
	// entry, and that silence is what both folds read as neutral.
	const auto statedBy = [&](const ibRoleID& rid, bool& stated) -> bool {
		stated = false;
		const auto iterator_role = m_valRoles.find(rid);
		if (iterator_role == m_valRoles.end())
			return false;
		const auto iterator = std::find_if(iterator_role->second.begin(), iterator_role->second.end(),
			[role](const auto& pair) { return stringUtils::CompareString(role->GetName(), pair.first); });
		if (iterator == iterator_role->second.end())
			return false;
		stated = true;
		return iterator->second;
	};

	// The UNION pass — any permitting role that grants the right wins, and a role silent about it
	// falls back to the right's default rather than to whatever the previously examined role said.
	// A restricting role takes no part: it grants nothing (see the second pass).
	for (const auto& userRole : roleInfo.m_arrayRole) {
		if (!userRole.IsUnion())
			continue;
		bool stated = false;
		const bool value = statedBy(userRole.m_id, stated);
		access = stated ? value : defValue;
		if (access) break;
	}

	// The INTERSECTION pass — restricting roles SUBTRACT from whatever the union granted, which is
	// the object-level twin of the RLS fold (docs/access-policy-rls.md, § Multi-role). An explicit
	// True here means "I do not object", never "I allow": only an explicit False is an answer that
	// changes anything, and silence stays neutral — otherwise a separator role would strip every
	// right in the configuration the moment it was created.
	//
	// Order is irrelevant: one denial is enough and denial is terminal, so the sequence a user's
	// roles were assigned in cannot change the answer — the property an ACL with DENY does not have.
	for (const auto& userRole : roleInfo.m_arrayRole) {
		if (userRole.IsUnion())
			continue;
		bool stated = false;
		const bool value = statedBy(userRole.m_id, stated);
		if (stated && !value) {
			access = false;                               // an explicit denial — terminal
			break;
		}
	}

	return access;
}

bool ibAccessObject::SetRight(const ibRole* role, const ibRoleID& id, const bool& set)
{
	if (role == nullptr)
		return false;
	m_valRoles[id].insert_or_assign(role->GetName(), set);
	DoSetRight(role, set);
	return true;
}

ibRole* ibAccessObject::GetRole(const wxString& nameParam) const
{
	auto iterator = std::find_if(m_roles.begin(), m_roles.end(),
		[nameParam](const std::pair<wxString, ibRole*>& pair) { return stringUtils::CompareString(nameParam, pair.first); });

	if (iterator != m_roles.end())
		return &(*iterator->second);

	return nullptr;
}

ibRole* ibAccessObject::GetRole(unsigned int idx) const
{
	assert(idx < m_roles.size());

	auto iterator = m_roles.begin();
	unsigned int i = 0;
	while (i < idx && iterator != m_roles.end()) {
		i++;
		iterator++;
	}

	if (iterator != m_roles.end())
		return &(*iterator->second);

	return nullptr;
}

bool ibAccessObject::LoadRole(ibReaderMemory& dataReader)
{
	wxMemoryBuffer buf;
	if (dataReader.r_chunk(roleBlock, buf)) {
		std::shared_ptr<ibReaderMemory> dataRoleReader(new ibReaderMemory(buf));
		unsigned int countRole = dataRoleReader->r_u32(); m_valRoles.clear();
		for (unsigned int idx = 0; idx < countRole; idx++) {
			unsigned int countData = dataRoleReader->r_u32();
			ibRoleID id = dataRoleReader->r_s32();
			for (unsigned int idc = 0; idc < countData; idc++) {
				wxString roleName = dataRoleReader->r_stringZ();
				bool roleValue = dataRoleReader->r_u8();
				m_valRoles[id].insert_or_assign(roleName, roleValue);
			}
		}

		return true;
	}

	return false;
}

bool ibAccessObject::SaveRole(ibWriterMemory& dataWritter) const
{
	ibWriterMemory dataRoleWritter;
	dataRoleWritter.w_u32(m_valRoles.size());
	for (auto role_id : m_valRoles) {
		dataRoleWritter.w_u32(role_id.second.size());
		dataRoleWritter.w_s32(role_id.first); // role id
		for (auto role_data : role_id.second) {
			dataRoleWritter.w_stringZ(role_data.first); //strName role
			dataRoleWritter.w_u8(role_data.second); // value true/false
		}
	}

	dataWritter.w_chunk(roleBlock, dataRoleWritter.buffer());
	return true;
}

#pragma endregion