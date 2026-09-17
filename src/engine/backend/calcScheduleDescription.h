#ifndef __CALC_SCHEDULE_DESCRIPTION_H__
#define __CALC_SCHEDULE_DESCRIPTION_H__

#include "backend/backend_core.h"   // ibMetaID

#include <vector>

// ONE LINK OF A SCHEDULE — which dimension of the schedule register a field of the calculation register answers
// for. A record finds its schedule rows through its links; a complete schedule links every dimension of the
// schedule register but its date (the register refuses to save one that does not).
struct ibCalcScheduleLink {
	ibMetaID m_dimension = 0;   // of the schedule register
	ibMetaID m_field = 0;       // of the calculation register: a dimension

	bool operator==(const ibCalcScheduleLink& other) const {
		return m_dimension == other.m_dimension && m_field == other.m_field;
	}
};

// ⭐ THE SCHEDULE OF A CALCULATION REGISTER, AS A DESCRIPTION — the same shape as ibTypeDescription and
// ibSourceDescription: a structure, a variant that holds it (ibVariantDataCalcSchedule), a property that hands it
// out by reference (ibPropertyCalcSchedule::GetValueAsScheduleDesc), and a Memory class that saves it.
//
// WHICH information register holds the schedule, WHICH of its numeric resources is the value (hours, days),
// WHICH of its date dimensions is the date, and HOW a record finds its rows — the links. One description, because
// every part of it is a field OF the register named first. The links live here and not on the register's fields
// themselves: all a schedule binding says is in one place, and nothing about it widens the attribute classes.
//
// Held by metaID, as every binding inside the tree is (ibMetaDescriptionMemory): an id survives a rename; a name
// does not.
struct ibCalcScheduleDescription {

	ibMetaID m_register = 0;
	ibMetaID m_value = 0;
	ibMetaID m_date = 0;
	std::vector<ibCalcScheduleLink> m_links;

	bool IsOk() const { return m_register != 0; }

	ibMetaID GetRegister() const { return m_register; }
	ibMetaID GetValue() const { return m_value; }
	ibMetaID GetDate() const { return m_date; }

	void SetSchedule(ibMetaID scheduleRegister, ibMetaID value, ibMetaID date) {
		if (scheduleRegister != m_register)
			m_links.clear();   // links name dimensions of the register they were made for
		m_register = scheduleRegister;
		m_value = value;
		m_date = date;
	}
	void ClearSchedule() { SetSchedule(0, 0, 0); m_links.clear(); }

	const std::vector<ibCalcScheduleLink>& GetLinks() const { return m_links; }

	// The field linked to a schedule dimension, or 0.
	ibMetaID GetLinkedField(ibMetaID dimension) const {
		for (const ibCalcScheduleLink& link : m_links)
			if (link.m_dimension == dimension)
				return link.m_field;
		return 0;
	}

	// One link per schedule dimension: set replaces, 0 removes.
	void SetLink(ibMetaID dimension, ibMetaID field) {
		for (auto it = m_links.begin(); it != m_links.end(); ++it) {
			if (it->m_dimension != dimension)
				continue;
			if (field == 0) m_links.erase(it);
			else it->m_field = field;
			return;
		}
		if (dimension != 0 && field != 0)
			m_links.push_back({ dimension, field });
	}

	bool operator==(const ibCalcScheduleDescription& other) const {
		return m_register == other.m_register && m_value == other.m_value && m_date == other.m_date
			&& m_links == other.m_links;
	}
	bool operator!=(const ibCalcScheduleDescription& other) const { return !(*this == other); }
};

// node form: a Child {register, value, date, links: [{dimension, field}]} of raw metaIDs — metaID and not guid
// for the reason ibMetaDescriptionMemory gives: a binding lives INSIDE the metadata tree, and a guid resolved at
// load time would meet objects not loaded yet.
class BACKEND_API ibCalcScheduleDescriptionMemory {
public:
	static bool ReadNode(const class ibDataValue& value, ibCalcScheduleDescription& scheduleDesc);
	static bool WriteNode(class ibDataValue& value, const ibCalcScheduleDescription& scheduleDesc);
};

#endif
