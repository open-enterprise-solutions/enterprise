////////////////////////////////////////////////////////////////////////////
//	Description : ibJobScheduleDescription — the calendar half of "is this job due?"
////////////////////////////////////////////////////////////////////////////

#include "jobSchedule.h"

#include "backend/serialize/dataBuilder.h"   // ibDataNode — the storage door (ReadData / WriteData)

#include <type_traits>

bool ibJobScheduleDescription::IsInsideWindow(int startMinute, int endMinute, int nowMinute)
{
	if (startMinute < 0 || endMinute < 0)
		return true;   // no window declared — always inside

	// start <= end is the plain daytime range. start > end WRAPS midnight, which
	// is what a night window (22:00–05:00) means; read as a plain range it would
	// match no minute at all.
	if (startMinute <= endMinute)
		return nowMinute >= startMinute && nowMinute < endMinute;
	return nowMinute >= startMinute || nowMinute < endMinute;
}

bool ibJobScheduleDescription::IsValid() const
{
	if (m_intervalSeconds <= 0)
		return false;

	// A half-declared window is not a window — but a window with an out-of-range
	// bound is a mistake, and one that would quietly gate the job forever.
	const bool hasWindow = m_startMinute >= 0 || m_endMinute >= 0;
	if (hasWindow) {
		if (m_startMinute < 0 || m_endMinute < 0)
			return false;
		if (m_startMinute >= 24 * 60 || m_endMinute > 24 * 60)
			return false;
		if (m_startMinute == m_endMinute)
			return false;   // empty range — matches no minute
	}

	// A day-of-month mask that names only days beyond 31 can never match.
	if (m_daysOfMonth != 0 && (m_daysOfMonth & 0x7FFFFFFFu) == 0)
		return false;
	if (m_daysOfMonthFromEnd != 0 && (m_daysOfMonthFromEnd & 0x7FFFFFFFu) == 0)
		return false;

	// An ordinal counts occurrences of ONE weekday; with several named there is nothing to count,
	// and with none the ordinal has no subject at all.
	if (m_weekdayOrdinal != ibJobOrdinal_None) {
		if (m_weekdayOrdinal > ibJobOrdinal_Fifth && m_weekdayOrdinal != ibJobOrdinal_Last)
			return false;
		const std::uint8_t days = (m_daysOfWeek == 0) ? ibJobWeekDay_Any : m_daysOfWeek;
		const bool exactlyOne = days != 0 && (days & (days - 1)) == 0;
		if (!exactlyOne)
			return false;
	}

	// "Every N" with a stop-after that precedes the window start would gate the job forever.
	if (m_stopAfterMinute >= 0) {
		if (m_stopAfterMinute > 24 * 60)
			return false;
		if (m_startMinute >= 0 && m_stopAfterMinute <= m_startMinute)
			return false;
	}

	// Months beyond December, likewise.
	if (m_months != 0 && (m_months & 0x0FFFu) == 0)
		return false;

	// An inverted validity range names no moment at all.
	if (m_activeFrom.IsValid() && m_activeTo.IsValid() && m_activeTo < m_activeFrom)
		return false;

	return true;
}


bool ibJobScheduleDescription::operator==(const ibJobScheduleDescription& o) const
{
	return m_intervalSeconds    == o.m_intervalSeconds
	    && m_startMinute        == o.m_startMinute
	    && m_endMinute          == o.m_endMinute
	    && m_stopAfterMinute    == o.m_stopAfterMinute
	    && m_daysOfWeek         == o.m_daysOfWeek
	    && m_daysOfMonth        == o.m_daysOfMonth
	    && m_daysOfMonthFromEnd == o.m_daysOfMonthFromEnd
	    && m_months             == o.m_months
	    && m_weekdayOrdinal     == o.m_weekdayOrdinal
	    && m_everyNWeeks        == o.m_everyNWeeks
	    && m_everyNMonths       == o.m_everyNMonths
	    && m_periodAnchor       == o.m_periodAnchor
	    && m_activeFrom         == o.m_activeFrom
	    && m_activeTo           == o.m_activeTo;
}

// ---------------------------------------------------------------------------
// ibJobScheduleDescriptionMemory — the serialiser. Node shape documented at the declaration.
// ---------------------------------------------------------------------------

bool ibJobScheduleDescriptionMemory::WriteNode(ibDataValue& value, const ibJobScheduleDescription& s)
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("IntervalSeconds"),    (s32)s.m_intervalSeconds);
	root->SetValue(wxT("StartMinute"),        (s32)s.m_startMinute);
	root->SetValue(wxT("EndMinute"),          (s32)s.m_endMinute);
	root->SetValue(wxT("StopAfterMinute"),    (s32)s.m_stopAfterMinute);
	root->SetValue(wxT("DaysOfWeek"),         (s32)s.m_daysOfWeek);
	root->SetValue(wxT("DaysOfMonth"),        (s32)s.m_daysOfMonth);
	root->SetValue(wxT("DaysOfMonthFromEnd"), (s32)s.m_daysOfMonthFromEnd);
	root->SetValue(wxT("Months"),             (s32)s.m_months);
	root->SetValue(wxT("WeekdayOrdinal"),     (s32)s.m_weekdayOrdinal);
	root->SetValue(wxT("EveryNWeeks"),        (s32)s.m_everyNWeeks);
	root->SetValue(wxT("EveryNMonths"),       (s32)s.m_everyNMonths);
	root->SetValue(wxT("PeriodAnchor"),       s.m_periodAnchor);
	root->SetValue(wxT("ActiveFrom"),         s.m_activeFrom);
	root->SetValue(wxT("ActiveTo"),           s.m_activeTo);
	value = ibDataValue::Child(root);
	return true;
}

bool ibJobScheduleDescriptionMemory::ReadNode(const ibDataValue& value, ibJobScheduleDescription& s)
{
	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (!root)
		return false;

	// FindField before reading, so an absent name leaves the member at its default. That is the
	// whole forward-compatibility story: a blob written before a field existed must load as "not
	// restricted", never as "restricted to nothing".
	const auto readInt = [&root](const wxString& name, auto& target) {
		if (root->FindField(name) != nullptr)
			target = static_cast<typename std::remove_reference<decltype(target)>::type>(root->GetValue<s32>(name));
	};

	readInt(wxT("IntervalSeconds"),    s.m_intervalSeconds);
	readInt(wxT("StartMinute"),        s.m_startMinute);
	readInt(wxT("EndMinute"),          s.m_endMinute);
	readInt(wxT("StopAfterMinute"),    s.m_stopAfterMinute);
	readInt(wxT("DaysOfWeek"),         s.m_daysOfWeek);
	readInt(wxT("DaysOfMonth"),        s.m_daysOfMonth);
	readInt(wxT("DaysOfMonthFromEnd"), s.m_daysOfMonthFromEnd);
	readInt(wxT("Months"),             s.m_months);
	readInt(wxT("WeekdayOrdinal"),     s.m_weekdayOrdinal);
	readInt(wxT("EveryNWeeks"),        s.m_everyNWeeks);
	readInt(wxT("EveryNMonths"),       s.m_everyNMonths);

	root->GetValue<wxDateTime>(wxT("PeriodAnchor"), s.m_periodAnchor);
	root->GetValue<wxDateTime>(wxT("ActiveFrom"),   s.m_activeFrom);
	root->GetValue<wxDateTime>(wxT("ActiveTo"),     s.m_activeTo);
	return true;
}

//***********************************************************************
//*        the COLUMN form — the same schedule, written as a blob        *
//***********************************************************************

namespace {

// One version byte in front. It is not decoration: a job ROW written by a later build must load
// in an earlier one rather than fail, so the reader stops where its knowledge ends and every
// unread field keeps its default — and every default here reads as "not restricted".
constexpr std::uint8_t kScheduleBufferVersion = 1;

void PutU32(wxMemoryBuffer& out, std::uint32_t value)
{
	const std::uint8_t bytes[4] = {
		static_cast<std::uint8_t>(value & 0xFF),
		static_cast<std::uint8_t>((value >> 8) & 0xFF),
		static_cast<std::uint8_t>((value >> 16) & 0xFF),
		static_cast<std::uint8_t>((value >> 24) & 0xFF),
	};
	out.AppendData(bytes, sizeof(bytes));
}

void PutS64(wxMemoryBuffer& out, std::int64_t value)
{
	PutU32(out, static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) & 0xFFFFFFFFull));
	PutU32(out, static_cast<std::uint32_t>((static_cast<std::uint64_t>(value) >> 32) & 0xFFFFFFFFull));
}

// A date the blob can hold: milliseconds since the wxDateTime epoch, 0 for an invalid one. An
// invalid date is the struct's own "unbounded", so it survives the round trip as itself.
std::int64_t DateToTicks(const wxDateTime& date)
{
	return date.IsValid() ? date.GetValue().GetValue() : 0;
}

wxDateTime TicksToDate(std::int64_t ticks)
{
	return ticks != 0 ? wxDateTime(wxLongLong(ticks)) : wxDateTime();
}

// The cursor the reader walks with — every Take* refuses past the end, so a truncated blob
// degrades to defaults instead of reading somebody else's memory.
struct BufferCursor {
	const std::uint8_t* m_data = nullptr;
	size_t              m_size = 0;
	size_t              m_pos  = 0;

	bool Take(std::uint32_t& value) {
		if (m_pos + 4 > m_size) return false;
		value = static_cast<std::uint32_t>(m_data[m_pos])
		      | (static_cast<std::uint32_t>(m_data[m_pos + 1]) << 8)
		      | (static_cast<std::uint32_t>(m_data[m_pos + 2]) << 16)
		      | (static_cast<std::uint32_t>(m_data[m_pos + 3]) << 24);
		m_pos += 4;
		return true;
	}

	bool Take(std::int64_t& value) {
		std::uint32_t low = 0, high = 0;
		if (!Take(low) || !Take(high)) return false;
		value = static_cast<std::int64_t>((static_cast<std::uint64_t>(high) << 32) | low);
		return true;
	}
};

} // namespace

void ibJobScheduleDescriptionMemory::WriteBuffer(wxMemoryBuffer& out, const ibJobScheduleDescription& s)
{
	out.SetDataLen(0);
	out.AppendByte(static_cast<char>(kScheduleBufferVersion));

	PutU32(out, static_cast<std::uint32_t>(s.m_intervalSeconds));
	PutU32(out, static_cast<std::uint32_t>(s.m_startMinute));
	PutU32(out, static_cast<std::uint32_t>(s.m_endMinute));
	PutU32(out, static_cast<std::uint32_t>(s.m_stopAfterMinute));
	PutU32(out, s.m_daysOfWeek);
	PutU32(out, s.m_daysOfMonth);
	PutU32(out, s.m_daysOfMonthFromEnd);
	PutU32(out, s.m_months);
	PutU32(out, s.m_weekdayOrdinal);
	PutU32(out, s.m_everyNWeeks);
	PutU32(out, s.m_everyNMonths);

	PutS64(out, DateToTicks(s.m_periodAnchor));
	PutS64(out, DateToTicks(s.m_activeFrom));
	PutS64(out, DateToTicks(s.m_activeTo));
}

bool ibJobScheduleDescriptionMemory::ReadBuffer(const void* data, size_t length, ibJobScheduleDescription& s)
{
	// NO BYTES IS NO OPINION — and it says so by refusing, without touching the caller's value.
	//
	// It used to overwrite `s` with a default description and report success, which is how an empty
	// cell (a row written before the requisite existed, a folder, a NULL column) could quietly
	// replace a schedule that was already in hand with one whose interval is zero — a schedule that
	// can never run. What "empty" MEANS belongs to the caller: the column builds a fresh default
	// and keeps it, the register keeps the declaration's own. Neither wants this function guessing.
	if (data == nullptr || length == 0)
		return false;

	BufferCursor cursor{ static_cast<const std::uint8_t*>(data), length, 1 };
	if (cursor.m_data[0] != kScheduleBufferVersion)
		return false;   // a version this build does not know — leave the caller's value alone

	// Read into a FRESH description: a partial blob then yields defaults for what it did not
	// carry, rather than a mix of the caller's previous value and this one.
	ibJobScheduleDescription read;

	const auto takeInt = [&cursor](auto& target) {
		std::uint32_t raw = 0;
		if (!cursor.Take(raw)) return false;
		target = static_cast<typename std::remove_reference<decltype(target)>::type>(raw);
		return true;
	};

	const auto takeDate = [&cursor](wxDateTime& target) {
		std::int64_t ticks = 0;
		if (!cursor.Take(ticks)) return false;
		target = TicksToDate(ticks);
		return true;
	};

	// The RESULT IS CHECKED — a truncated blob stops the chain, and a half-read schedule must not
	// be handed back as if it were the whole one. (It was: the chain's value was discarded and the
	// partial read assigned anyway, so a blob cut short by one field produced a job with a
	// plausible interval and no calendar.)
	const bool complete =
		takeInt(read.m_intervalSeconds)
		&& takeInt(read.m_startMinute)
		&& takeInt(read.m_endMinute)
		&& takeInt(read.m_stopAfterMinute)
		&& takeInt(read.m_daysOfWeek)
		&& takeInt(read.m_daysOfMonth)
		&& takeInt(read.m_daysOfMonthFromEnd)
		&& takeInt(read.m_months)
		&& takeInt(read.m_weekdayOrdinal)
		&& takeInt(read.m_everyNWeeks)
		&& takeInt(read.m_everyNMonths)
		&& takeDate(read.m_periodAnchor)
		&& takeDate(read.m_activeFrom)
		&& takeDate(read.m_activeTo);

	if (!complete)
		return false;   // the caller keeps whatever it had — a partial answer is worse than none

	s = read;
	return true;
}

ibJobScheduleDescription ibJobScheduleDescription::EverySeconds(int seconds)
{
	ibJobScheduleDescription s;
	s.m_intervalSeconds = seconds;
	return s;
}

ibJobScheduleDescription ibJobScheduleDescription::Nightly(int startHour, int endHour)
{
	ibJobScheduleDescription s;
	// Once a day, inside the window — the interval is what stops it from
	// re-running every tick for the whole window.
	s.m_intervalSeconds = 24 * 3600;
	s.m_startMinute     = AtTime(startHour);
	s.m_endMinute       = AtTime(endHour);
	return s;
}
