#ifndef _FRMCLIENT_DATECONV_H__
#define _FRMCLIENT_DATECONV_H__

// A DATE ACROSS TO wx's AND BACK — the pickers' wxDateTime. wx's base only, no widget header: the tests that hold
// the core's date against wx's build without wx's GUI.

#include <wx/datetime.h>

#include "core/fdatetime.h"   // ibDateTime

namespace typeConv
{
	// A date crosses by its LOCAL PARTS and by nothing else: the reading 10:30 is the wxDateTime whose local time is
	// 10:30, on whichever machine, and back. The empty date and an invalid wxDateTime are each other's - "no date" on
	// either side - so a NULL column, an unset bound or a cleared picker crosses both ways as itself. The one place
	// this is not an identity is a local time the machine does not have (02:30 on the morning its clocks go forward):
	// wx moves that instant an hour on, and a reading carried across and back has moved with it.
	inline wxDateTime DateTimeToWxDateTime(const ibDateTime& date) {
		if (date.IsEmpty())
			return wxDateTime();
		ibDateTimeParts parts;
		date.ToParts(parts);
		return wxDateTime(static_cast<wxDateTime::wxDateTime_t>(parts.m_day), static_cast<wxDateTime::Month>(parts.m_month - 1),
			parts.m_year, static_cast<wxDateTime::wxDateTime_t>(parts.m_hour), static_cast<wxDateTime::wxDateTime_t>(parts.m_minute),
			static_cast<wxDateTime::wxDateTime_t>(parts.m_second), static_cast<wxDateTime::wxDateTime_t>(parts.m_millisecond));
	}

	inline ibDateTime WxDateTimeToDateTime(const wxDateTime& moment) {
		if (!moment.IsValid())
			return ibDateTime();
		const wxDateTime::Tm tm = moment.GetTm();
		return ibDateTime(tm.year, static_cast<unsigned>(tm.mon) + 1, tm.mday, tm.hour, tm.min, tm.sec, tm.msec);
	}
}

#endif
