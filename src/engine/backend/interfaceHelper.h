#ifndef __INTERFACE_HELPER_H__
#define __INTERFACE_HELPER_H__

#include "backend_core.h"
#include "backend/fileSystem/fs.h"

#include <set>   // m_interfaces — arrived transitively while propgrid was in backend_core's chain

enum ibInterfaceCommandType {

	ibInterfaceCommandType_Default = 100,

	ibInterfaceCommandType_Create = 150,
	ibInterfaceCommandType_List,
	ibInterfaceCommandType_Select,
};

// The interface AREAS an object / command can occupy inside a section (subsystem) — the command-interface panels:
// Important (top, with the main items) + Normal make the navigation panel; Create / Reports / Service make the
// action panel. Combined = a list object that offers both its list (Normal) and a Create.
enum ibInterfaceCommandSection {

	ibInterfaceCommandSection_Default = 100,   // Normal
	ibInterfaceCommandSection_Important = 120, // Important — shown at the top with the main items

	ibInterfaceCommandSection_Create = 150,    // Create
	ibInterfaceCommandSection_Combined,
	ibInterfaceCommandSection_Report,          // Reports
	ibInterfaceCommandSection_Service,         // Service
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

	// MAY THIS BE CHECKED INTO A SECTION? Asked of the object, so the section editor builds
	// its tree by walking the metadata instead of naming every branch by hand — a metatype
	// that arrives later answers for itself.
	//
	// No by default: what a section shows is a navigation surface, and something with no
	// list and no command has nothing to contribute to one.
	virtual bool IsInterfaceAllowed() const { return false; }

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