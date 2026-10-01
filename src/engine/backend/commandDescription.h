#ifndef __COMMAND_DESCRIPTION_H__
#define __COMMAND_DESCRIPTION_H__

#include "backend/backend_core.h"   // ibMetaID, wxNOT_FOUND
#include "backend/interfaceHelper.h" // ibInterfaceCommandType — an object item's command TYPE (open list / create)
#include <vector>

// Command description — the command-door parallel of ibSourceDescription. Where a source description addresses
// a VALUE inside a data source as an ordered id path (Ref.Ref.Field), a command description addresses a COMMAND
// as an ordered id path. Index 0 is the entry (a form-own / general command, or a SOURCE id — a table); the leaf
// is the command run. A one-id path binds straight to a form / general command; a longer path steps THROUGH a
// source (a table id) to a command reached via it. The hops are just IDS — the command OBJECT
// (ibFrontendCommandReceiver) resolves what each id is at WALK time (a source vs a command), exactly as the source
// explorer resolves a source id. The KEY difference from a source path: the walk EXECUTES the leaf command, it
// does not fetch a value.

// One hop of a command path — just an ID (the command-door parallel of ibSourceHop, id-only).
struct ibCommandHop {
	ibMetaID m_id = wxNOT_FOUND;
	bool operator==(const ibCommandHop& o) const { return m_id == o.m_id; }
	bool operator!=(const ibCommandHop& o) const { return !(*this == o); }
};

// Mirrors ibSourceDescription (a plain id path with its own *Memory serialiser).
struct ibCommandDescription {
	std::vector<ibCommandHop> m_listCommand;
	// The command TYPE for an OBJECT item (a catalog / register included in a section): the leaf id is a business
	// object, not a command, and this says WHICH standard command it stands for — open the list (Default) vs create
	// (Create). Set from the section AREA. Default for real commands / actions (they carry their own action).
	ibInterfaceCommandType m_commandType = ibInterfaceCommandType_Default;
public:
	ibCommandDescription() {}
	ibCommandDescription(const ibMetaID& id) { AppendCommand(id); }
	ibCommandDescription(const std::vector<ibMetaID>& ids) { for (const ibMetaID& id : ids) AppendCommand(id); }
public:

	bool IsOk() const { return GetHopCount() != 0; }

	// IDENTITY equality — the id path PLUS the command type (so the SAME object in a section's Normal vs Create area
	// — same leaf id, different type — are DISTINCT bindings). The walk resolves each hop uniformly against the
	// frame it lands on (form command / metaobject command / OBJECT item by type / source descend / standard action).
	bool operator==(const ibCommandDescription& o) const {
		return m_listCommand == o.m_listCommand && m_commandType == o.m_commandType;
	}
	bool operator!=(const ibCommandDescription& o) const { return !(*this == o); }

	ibInterfaceCommandType GetCommandType() const { return m_commandType; }
	void SetCommandType(ibInterfaceCommandType type) { m_commandType = type; }

	// True when the path steps THROUGH a source (a table id) before the command, rather than binding straight.
	bool IsHop() const { return GetHopCount() > 1; }

	void SetDefaultCommand(const ibMetaID& id) { ClearCommand(); AppendCommand(id); }
	void AppendCommand(const ibMetaID& id) { m_listCommand.push_back({ id }); }
	void ClearCommand() { m_listCommand.clear(); }

	// The first hop is the entry point (form / source id); the leaf is the command actually run.
	ibMetaID GetFirst() const { return m_listCommand.empty() ? wxNOT_FOUND : m_listCommand.front().m_id; }
	ibMetaID GetLeaf()  const { return m_listCommand.empty() ? wxNOT_FOUND : m_listCommand.back().m_id; }
	ibMetaID GetByIdx(unsigned int idx) const { return idx < m_listCommand.size() ? m_listCommand[idx].m_id : wxNOT_FOUND; }

	// Number of hops (0 = empty, 1 = bind straight, >1 = steps through a source).
	size_t GetHopCount() const { return m_listCommand.size(); }

	// The full id path, BY REF — the command object walks it, resolving each id as it goes.
	const std::vector<ibCommandHop>& GetPath() const { return m_listCommand; }
};

// A DUMB serializer for a command id path — writes / reads the ids VERBATIM ([count : u32][ id : u32 ] per hop),
// no metadata, no copy awareness (the OWNER's contract, exactly like ibSourceDescriptionMemory).
class BACKEND_API ibCommandDescriptionMemory {
public:
	static bool LoadData(class ibReaderMemory& reader, ibCommandDescription& cmdDesc);
	static bool SaveData(class ibWriterMemory& writer, const ibCommandDescription& cmdDesc);

	// node form — a Binary blob of the id path; the byte reader / writer is contained here, not the property.
	static bool ReadNode(const class ibDataValue& value, ibCommandDescription& cmdDesc);
	static bool WriteNode(class ibDataValue& value, const ibCommandDescription& cmdDesc);
};

#endif // !__COMMAND_DESCRIPTION_H__
