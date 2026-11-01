#include "timeZone.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <map>
#include <mutex>
#include <string>

// THE RULES. MSVC has the C++20 time zone database; GCC before 14 and Apple's libc++ do not, so there the date
// library it came from stands in, reading the system's zoneinfo (USE_OS_TZDB, set by the build). The calls are the
// same; when every toolchain has std::chrono::tzdb, the second branch goes.
#if defined(_MSC_VER)
namespace tzdb = std::chrono;
#else
#include <date/ptz.h>
#include <date/tz.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
namespace tzdb = date;
#endif

namespace {

using Milliseconds = std::chrono::milliseconds;

// 1970-01-01, where the rules count from, as a reading's count from the empty date.
constexpr long long kUnixEpoch = ibDateTime(1970, 1, 1).GetValue();

// ZONES RENAMED in the database, each by its old name and its new: Windows' ICU answers some by the old, a stock
// Linux has the new alone (the old ones are in tzdata-legacy), and a client and its server may be either.
constexpr const char* kRenamed[][2] = {
	{ "Europe/Kiev", "Europe/Kyiv" },                  { "Europe/Uzhgorod", "Europe/Kyiv" },
	{ "Europe/Zaporozhye", "Europe/Kyiv" },            { "Asia/Calcutta", "Asia/Kolkata" },
	{ "Asia/Saigon", "Asia/Ho_Chi_Minh" },             { "Asia/Katmandu", "Asia/Kathmandu" },
	{ "Asia/Rangoon", "Asia/Yangon" },                 { "Asia/Ulan_Bator", "Asia/Ulaanbaatar" },
	{ "Asia/Choibalsan", "Asia/Ulaanbaatar" },         { "Atlantic/Faeroe", "Atlantic/Faroe" },
	{ "Africa/Asmera", "Africa/Asmara" },              { "America/Godthab", "America/Nuuk" },
	{ "Pacific/Enderbury", "Pacific/Kanton" },         { "Pacific/Truk", "Pacific/Chuuk" },
	{ "Pacific/Ponape", "Pacific/Pohnpei" },           { "America/Buenos_Aires", "America/Argentina/Buenos_Aires" },
	{ "America/Catamarca", "America/Argentina/Catamarca" }, { "America/Cordoba", "America/Argentina/Cordoba" },
	{ "America/Jujuy", "America/Argentina/Jujuy" },    { "America/Mendoza", "America/Argentina/Mendoza" },
	{ "America/Indianapolis", "America/Indiana/Indianapolis" }, { "America/Louisville", "America/Kentucky/Louisville" },
	{ "America/Coral_Harbour", "America/Atikokan" },   { "America/Nipigon", "America/Toronto" },
	{ "America/Thunder_Bay", "America/Toronto" },      { "America/Rainy_River", "America/Winnipeg" },
	{ "America/Yellowknife", "America/Edmonton" },     { "America/Pangnirtung", "America/Iqaluit" },
	{ "Australia/Currie", "Australia/Hobart" },
};

// The rules' zone of a name - null for a name they have not.
const tzdb::time_zone* Locate(const std::string& name)
{
	try {
		return tzdb::locate_zone(name);
	}
	catch (const std::exception&) {
		return nullptr;
	}
}

#if !defined(_MSC_VER)
// Where the system keeps its zoneinfo - where the date library reads it from.
std::string ZoneInfoDirectory()
{
#if defined(__APPLE__)
	char link[1024] = {};
	const ssize_t length = readlink("/etc/localtime", link, sizeof(link) - 1);
	const std::string target = length > 0 ? std::string(link, static_cast<std::size_t>(length)) : std::string();
	const std::size_t at = target.find("zoneinfo");
	return at != std::string::npos ? target.substr(0, target.find('/', at)) : std::string("/var/db/timezone/zoneinfo");
#else
	struct stat st {};
	return stat("/usr/share/zoneinfo/uclibc", &st) == 0 && S_ISDIR(st.st_mode) ? "/usr/share/zoneinfo/uclibc"
		: "/usr/share/zoneinfo";
#endif
}

// The rule a zone's file ends with - its last line, in the POSIX TZ form - for the years past its last transition,
// which the date library does not read. Empty when the file has none: a version 1 file has no such line.
std::string FooterOf(const std::string& zone)
{
	std::ifstream file(ZoneInfoDirectory() + '/' + zone, std::ios::binary);
	std::stringstream read;
	read << file.rdbuf();
	const std::string bytes = read.str();
	if (bytes.size() < 6 || bytes.compare(0, 4, "TZif") != 0 || bytes[4] < '2' || bytes.back() != '\n')
		return std::string();
	const std::size_t start = bytes.rfind('\n', bytes.size() - 2);
	return start == std::string::npos ? std::string() : bytes.substr(start + 1, bytes.size() - start - 2);
}
#endif

} // namespace

// A zone's rules, found once: the database's zone and, where the database is the system's files, the rule for the
// years past the last transition they list (a file is written to 2037, or to the last change a slim one knows).
struct ibTimeZone::Rules {
	const tzdb::time_zone* m_zone = nullptr;
#if !defined(_MSC_VER)
	std::optional<Posix::time_zone> m_tail;
	tzdb::sys_seconds m_tailFrom{};   // the last transition: from it on, the tail answers
#endif

	long long ToLocal(long long unixMs) const
	{
		const tzdb::sys_time<Milliseconds> instant{ Milliseconds(unixMs) };
#if !defined(_MSC_VER)
		if (m_tail && instant >= m_tailFrom)
			return m_tail->to_local(instant).time_since_epoch().count();
#endif
		return m_zone->to_local(instant).time_since_epoch().count();
	}

	long long ToUniversal(long long localMs) const
	{
		const tzdb::local_time<Milliseconds> reading{ Milliseconds(localMs) };
		const auto instant = m_zone->to_sys(reading, tzdb::choose::earliest);
#if !defined(_MSC_VER)
		if (m_tail && instant >= m_tailFrom)
			return m_tail->to_sys(reading, tzdb::choose::earliest).time_since_epoch().count();
#endif
		return instant.time_since_epoch().count();
	}

	// The rules of a name the database has - kept for the process, never torn down; null for none. Only a name
	// found is kept: a name is what a client says at login, and names that are no zone are not to fill the process.
	static const Rules* Of(const std::string& name)
	{
		if (name.empty() || name.size() > kLongestName)
			return nullptr;
		static auto* const s_lock = new std::mutex();   // never torn down, as what it guards
		static auto* const s_found = new std::map<std::string, const Rules*>();
		std::lock_guard<std::mutex> lock(*s_lock);
		const auto kept = s_found->find(name);
		if (kept != s_found->end())
			return kept->second;
		const tzdb::time_zone* zone = Locate(name);
		if (zone == nullptr) {
			// The first rename that names it, the other way: a zone merged into another (Uzhgorod into Kyiv) is not
			// to be found by the one it was merged into.
			for (const auto& [before, after] : kRenamed) {
				if (name == before) {
					zone = Locate(after);
					break;
				}
				if (name == after) {
					zone = Locate(before);
					break;
				}
			}
		}
		if (zone == nullptr)
			return nullptr;
		Rules* const made = new Rules();
		{
			made->m_zone = zone;
#if !defined(_MSC_VER)
			try {
				const std::string footer = FooterOf(std::string(zone->name()));
				if (!footer.empty()) {
					made->m_tail.emplace(footer);
					made->m_tailFrom = zone->get_info(tzdb::sys_days{ tzdb::year{ 9999 } / 12 / 31 }).begin;
				}
			}
			catch (const std::exception&) {
				// A rule the library does not read: hours outside 0..24 (Jerusalem, Nuuk, Gaza), which the file
				// format allows. The listed transitions alone - to 2037.
				made->m_tail.reset();
			}
#endif
		}
		(*s_found)[name] = made;
		return made;
	}

	// The longest name a zone has is under 32; anything much longer is no zone.
	static constexpr std::size_t kLongestName = 64;
};

bool ibTimeZone::FromName(const ibString& name)
{
	if (name.IsEmpty())
		return false;
	const Rules* const rules = Rules::Of(name.ToUtf8());
	if (rules == nullptr)
		return false;
	m_rules = rules;
	m_name = name;
	return true;
}

ibTimeZone ibTimeZone::Machine()
{
	ibTimeZone zone;
#if !defined(_MSC_VER)
	// TZ first when it names a zone: a name ("Europe/Kyiv", ":Europe/Kyiv") or a file under zoneinfo (its posix/ or
	// right/ copy too). A rule written out (UTC0, EET-2EEST,…) names none, and the system's zone answers.
	if (const char* const tz = std::getenv("TZ"); tz != nullptr && *tz != '\0') {
		std::string named(tz[0] == ':' ? tz + 1 : tz);
		const std::size_t under = named.find("zoneinfo/");
		if (under != std::string::npos)
			named = named.substr(under + 9);
		for (const char* copy : { "posix/", "right/" })
			if (named.rfind(copy, 0) == 0)
				named = named.substr(std::char_traits<char>::length(copy));
		if (zone.FromName(ibString::FromUTF8(named)))
			return zone;
	}
#endif
	try {
		if (const tzdb::time_zone* const current = tzdb::current_zone())
			zone.FromName(ibString::FromUTF8(std::string(current->name())));
	}
	catch (const std::exception&) {
		// A machine that names no zone the rules have - a container with no /etc/localtime - runs in UTC.
	}
	return zone;
}

namespace {

// A reading the zone put before year 1 - or onto the empty date - is the first millisecond of year 1.
ibDateTime WithinTheCalendar(long long value)
{
	return ibDateTime(value > 0 ? value : 1);
}

} // namespace

ibDateTime ibTimeZone::ToLocal(const ibDateTime& universal) const
{
	if (universal.IsEmpty() || m_rules == nullptr)
		return universal;
	return WithinTheCalendar(m_rules->ToLocal(universal.GetValue() - kUnixEpoch) + kUnixEpoch);
}

ibDateTime ibTimeZone::ToUniversal(const ibDateTime& local) const
{
	if (local.IsEmpty() || m_rules == nullptr)
		return local;
	return WithinTheCalendar(m_rules->ToUniversal(local.GetValue() - kUnixEpoch) + kUnixEpoch);
}
