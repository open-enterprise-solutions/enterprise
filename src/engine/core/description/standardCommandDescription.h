#ifndef __STANDARD_COMMAND_DESCRIPTION_H__
#define __STANDARD_COMMAND_DESCRIPTION_H__

#include "core/core.h"   // CORE_API, ibActionID, wxNOT_FOUND

// The action a tool runs — a standard command by its system id, or a custom handler by name. Which ids exist and
// what they do is the side's (backend/standardCommand.h — ibStandardCommandSource); the schema keeps the choice.
struct CORE_API ibStandardCommandDescription {

	ibActionID m_lAction;
	ibString m_strAction;

	ibStandardCommandDescription(const ibActionID& lAction) : m_lAction(lAction), m_strAction() {}
	ibStandardCommandDescription(const ibString& strAction) : m_lAction(wxNOT_FOUND), m_strAction(strAction) {}

	const ibString& GetCustomAction() const { return m_strAction; }
	ibActionID GetSystemAction() const { return m_lAction; }

	bool operator == (const ibStandardCommandDescription& rhs) const {
		if (m_lAction == wxNOT_FOUND)
			return m_strAction == rhs.m_strAction;
		return m_lAction == rhs.m_lAction;
	}

	friend class ibActionDescriptionMemory;
};

class CORE_API ibActionDescriptionMemory {
public:
	// node form
	static bool ReadNode(const class ibDataValue& value, ibStandardCommandDescription& actionDesc);
	static bool WriteNode(class ibDataValue& value, const ibStandardCommandDescription& actionDesc);
};

#endif // !__STANDARD_COMMAND_DESCRIPTION_H__
