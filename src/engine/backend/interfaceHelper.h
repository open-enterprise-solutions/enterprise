#ifndef __INTERFACE_HELPER_H__
#define __INTERFACE_HELPER_H__

#include "backend_core.h"
#include "backend/fileSystem/fs.h"

enum ibInterfaceCommandType {

	ibInterfaceCommandType_Default = 100,

	ibInterfaceCommandType_Create = 150,
	ibInterfaceCommandType_List,
	ibInterfaceCommandType_Select,
};

enum ibInterfaceCommandSection {

	ibInterfaceCommandSection_Default = 100,

	ibInterfaceCommandSection_Create = 150,
	ibInterfaceCommandSection_Combined,
	ibInterfaceCommandSection_Report,
	ibInterfaceCommandSection_Service,
};

//********************************************************************************************
//*										 Sybsystem - interface								 *
//********************************************************************************************

class BACKEND_API ibInterfaceObject {
public:

	void SetInterface(const ibMetaID& id, const bool& set = true) {
		if (set) m_interfaces.emplace(id);
		else m_interfaces.erase(id);
		DoSetInterface(id, set);
	}

	bool IsSetInterface(const ibMetaID& id) const { return m_interfaces.find(id) != m_interfaces.end(); }

	virtual ~ibInterfaceObject() {}
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Default; }

protected:

	virtual void DoSetInterface(const ibMetaID& id, const bool& set = true) {}

	//load & save subsystem in metaobject 
	bool LoadInterface(ibReaderMemory& reader);
	bool SaveInterface(ibWriterMemory& writer) const;

	std::set<ibMetaID> m_interfaces;
};

#endif