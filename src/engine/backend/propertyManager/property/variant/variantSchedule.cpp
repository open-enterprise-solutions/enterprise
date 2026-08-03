#include "variantSchedule.h"

// The sentence a grid row shows — built by the RULES, not by this class: the same text a settings
// list and a log line use, so the two cannot disagree about what a schedule means.
wxString ibVariantDataSchedule::MakeString() const
{
	return ibJobScheduleRules::Describe(m_schedule);
}
