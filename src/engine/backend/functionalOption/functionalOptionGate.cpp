////////////////////////////////////////////////////////////////////////////
//	Description : the functional option gate - is this available in this base
////////////////////////////////////////////////////////////////////////////

#include "functionalOptionGate.h"
#include "backend/metaCollection/metaFunctionalOptionObject.h"

#include "backend/appData.h"
#include "backend/metaData.h"

#include <mutex>

bool ibFunctionalOptionGate::IsMemberAvailable(const std::set<ibMetaID>& memberOptions, const std::map<ibMetaID, bool>& optionValues)
{
	bool belongsToAny = false;
	for (const ibMetaID& option : memberOptions) {
		const auto found = optionValues.find(option);
		if (found == optionValues.end())
			continue;   // an option the configuration no longer has
		if (found->second)
			return true;
		belongsToAny = true;
	}
	return !belongsToAny;
}

std::map<ibMetaID, bool> ibFunctionalOptionGate::EffectiveValues(const std::map<ibMetaID, bool>& own,
	const std::map<ibMetaID, ibMetaID>& required)
{
	std::map<ibMetaID, bool> effective;
	for (const auto& option : own) {
		bool on = true;
		std::set<ibMetaID> seen;
		// Up the chain of requirements, each one ANDed in, until it ends, leaves the configuration, or
		// comes back to an option already walked.
		for (ibMetaID step = option.first; step != wxNOT_FOUND && seen.insert(step).second; ) {
			const auto value = own.find(step);
			if (value == own.end())
				break;
			on = on && value->second;
			const auto next = required.find(step);
			step = next != required.end() ? next->second : wxNOT_FOUND;
		}
		effective[option.first] = on;
	}
	return effective;
}

namespace {

// What was read about one open configuration: every option's value, and whether any is off at all. The
// fingerprint is the configuration's own — an entry left over from another configuration opened at the
// same address never answers for this one.
//
// ⭐ READ ONCE, WHEN THE PROCESS FIRST ASKS — and after that only when the write door says so. An option
// switched in THIS process drops the answer at once (OnAfterValueWrite); another client reads the new value
// at its next start. No timer: a rule that depends on when a minute happened to tick cannot be reproduced,
// and a switch that is rare and deliberate is better answered by a moment everyone can name.
struct ibFunctionalOptionValues {
	wxString m_configMD5;
	std::map<ibMetaID, bool> m_values;
	bool m_anyOff = false;
};

std::mutex s_valuesLock;
std::map<const ibMetaData*, ibFunctionalOptionValues> s_values;

// Open AsApplication scopes on this thread — see the header.
thread_local int s_asApplication = 0;

// Only the running application makes anything unavailable: the designer shows what it edits — but for a
// composition's run, which prints what a person sees (AsApplication) — and a process with no application (a
// test) has nothing to withhold.
bool IsRuntime()
{
	return appData != nullptr && (!appData->DesignerMode() || s_asApplication > 0);
}

// The values of this configuration's options — read once and kept. Called under s_valuesLock.
const ibFunctionalOptionValues& ValuesOf(const ibMetaData* metaData)
{
	const wxString configMD5 = metaData->GetConfigMD5();

	auto found = s_values.find(metaData);
	if (found != s_values.end() && found->second.m_configMD5 == configMD5)
		return found->second;

	std::map<ibMetaID, bool> own;
	std::map<ibMetaID, ibMetaID> required;
	for (const ibValueMetaObjectFunctionalOption* option :
			metaData->GetAnyArrayObject<ibValueMetaObjectFunctionalOption>(g_metaFunctionalOptionCLSID)) {
		own[option->GetMetaID()] = option->IsOn();
		const ibMetaID parent = option->GetRequired();
		if (parent != wxNOT_FOUND)
			required[option->GetMetaID()] = parent;
	}

	ibFunctionalOptionValues& read = s_values[metaData];
	read.m_configMD5 = configMD5;
	read.m_values = ibFunctionalOptionGate::EffectiveValues(own, required);
	read.m_anyOff = false;
	for (const auto& value : read.m_values)
		read.m_anyOff = read.m_anyOff || !value.second;
	return read;
}

// Available under these values — the object and everything it stands inside. Called under s_valuesLock.
bool IsAvailableUnder(const ibValueMetaObject* object, const ibFunctionalOptionValues& read)
{
	for (const ibValueMetaObject* step = object; step != nullptr; step = step->GetParent()) {
		if (!ibFunctionalOptionGate::IsMemberAvailable(step->GetFunctionalOptions(), read.m_values))
			return false;
	}
	return true;
}

}

bool ibFunctionalOptionGate::IsAvailable(const ibValueMetaObject* object)
{
	if (object == nullptr || !IsRuntime())
		return true;

	const ibMetaData* metaData = object->GetMetaData();
	if (metaData == nullptr)
		return true;

	std::lock_guard<std::mutex> lock(s_valuesLock);
	const ibFunctionalOptionValues& read = ValuesOf(metaData);

	// The object itself, and everything it stands inside: a field of an unavailable catalog is unavailable too.
	return !read.m_anyOff || IsAvailableUnder(object, read);
}

bool ibFunctionalOptionGate::IsTypeAvailable(const ibMetaData* metaData, const ibTypeDescription& typeDesc)
{
	const std::vector<ibClassID>& types = typeDesc.GetClsidList();
	if (metaData == nullptr || types.empty() || !IsRuntime())
		return true;

	std::lock_guard<std::mutex> lock(s_valuesLock);
	const ibFunctionalOptionValues& read = ValuesOf(metaData);
	if (!read.m_anyOff)
		return true;

	for (const ibClassID& clsid : types) {
		// A type that names no single object of this configuration keeps the field: a primitive, a family
		// (`CatalogRef` — any catalog), a characteristic (any of its chart's types), an external object
		// (its id is its own file's). The id of the others carries the metaID of the object it is OF.
		const ibClassKind kind = clsid_kind(clsid);
		if (!IsMetaValue(clsid) || clsid_is_any(clsid) || IsCharacteristic(clsid)
			|| kind == ibClassKind_ExternalObject || kind == ibClassKind_ExternalManager)
			return true;
		const ibValueMetaObject* object = metaData->FindAnyObjectByFilter(static_cast<ibMetaID>(clsid_metaID(clsid)));
		if (object == nullptr || IsAvailableUnder(object, read))
			return true;
	}
	return false;   // every type it may hold is an object this base does not use
}

bool ibFunctionalOptionGate::IsAvailable(const ibMetaData* metaData, const std::set<ibMetaID>& options)
{
	if (metaData == nullptr || options.empty() || !IsRuntime())
		return true;

	std::lock_guard<std::mutex> lock(s_valuesLock);
	const ibFunctionalOptionValues& read = ValuesOf(metaData);
	return !read.m_anyOff || IsMemberAvailable(options, read.m_values);
}

bool ibFunctionalOptionGate::AnyUnavailable(const ibMetaData* metaData)
{
	if (metaData == nullptr || !IsRuntime())
		return false;

	std::lock_guard<std::mutex> lock(s_valuesLock);
	return ValuesOf(metaData).m_anyOff;
}

void ibFunctionalOptionGate::Forget(const ibMetaData* metaData)
{
	std::lock_guard<std::mutex> lock(s_valuesLock);
	s_values.erase(metaData);
}

// ⭐ THE DESIGNER KEEPS NO APPLICATION'S MEMORY. A running application reads the values once and keeps them
// (its forms were built by them); the designer has nothing built by them, so the application's view it is
// asked for is the base as it stands NOW — read afresh when a run opens its outermost scope, and kept for that
// run's own questions. Without it a switch made in the application since was seen here only after the
// designer restarted, and compose_run said so in its description (2026-09-28).
ibFunctionalOptionGate::AsApplication::AsApplication()
{
	if (s_asApplication++ == 0 && appData != nullptr && appData->DesignerMode()) {
		std::lock_guard<std::mutex> lock(s_valuesLock);
		s_values.clear();
	}
}

ibFunctionalOptionGate::AsApplication::~AsApplication() { --s_asApplication; }
