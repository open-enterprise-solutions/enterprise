////////////////////////////////////////////////////////////////////////////
//	Description : the feature option gate - is this available in this base
////////////////////////////////////////////////////////////////////////////

#include "featureOptionGate.h"
#include "core/fiber/fiberLocals.h"
#include "backend/metaCollection/metaFeatureOptionObject.h"

#include "backend/appData.h"
#include "backend/metaData.h"

#include <mutex>

bool ibFeatureOptionGate::IsMemberAvailable(const std::set<ibMetaID>& memberOptions, const std::map<ibMetaID, bool>& optionValues)
{
	bool belongsToAny = false;
	for (const ibMetaID& option : memberOptions) {
		const auto found = optionValues.find(option);
		if (found == optionValues.end())
			continue;   // an option the application no longer has
		if (found->second)
			return true;
		belongsToAny = true;
	}
	return !belongsToAny;
}

std::map<ibMetaID, bool> ibFeatureOptionGate::EffectiveValues(const std::map<ibMetaID, bool>& own,
	const std::map<ibMetaID, ibMetaID>& required)
{
	std::map<ibMetaID, bool> effective;
	for (const auto& option : own) {
		bool on = true;
		std::set<ibMetaID> seen;
		// Up the chain of requirements, each one ANDed in, until it ends, leaves the application, or
		// comes back to an option already walked.
		for (ibMetaID step = option.first; step != ibNOT_FOUND && seen.insert(step).second; ) {
			const auto value = own.find(step);
			if (value == own.end())
				break;
			on = on && value->second;
			const auto next = required.find(step);
			step = next != required.end() ? next->second : ibNOT_FOUND;
		}
		effective[option.first] = on;
	}
	return effective;
}

namespace {

// What was read about one open application: every option's value, and whether any is off at all. The
// fingerprint is the application's own — an entry left over from another application opened at the
// same address never answers for this one.
//
// ⭐ READ ONCE, WHEN THE PROCESS FIRST ASKS — and after that only when the write door says so. An option
// switched in THIS process drops the answer at once (OnAfterValueWrite); another client reads the new value
// at its next start. No timer: a rule that depends on when a minute happened to tick cannot be reproduced,
// and a switch that is rare and deliberate is better answered by a moment everyone can name.
struct ibFeatureOptionValues {
	ibString m_applicationMD5;
	std::map<ibMetaID, bool> m_values;
	bool m_anyOff = false;
};

std::mutex s_valuesLock;
std::map<const ibMetaData*, ibFeatureOptionValues> s_values;

// Open AsApplication scopes on this thread — see the header.
thread_local int s_asApplication = 0;

// A composition parked mid-question must not leave the studio's
// "as application" view on for the next session on this thread.
struct ibRegisterAsApplicationLocal {
	ibRegisterAsApplicationLocal()
	{
		ibFiberLocals::RegisterTrivial<int>(
			[](void* dst) { *static_cast<int*>(dst) = s_asApplication; },
			[](const void* src) { s_asApplication = *static_cast<const int*>(src); });
	}
} s_registerAsApplicationLocal;

// Only the running application makes anything unavailable: the studio shows what it edits — but for a
// composition's run, which prints what a person sees (AsApplication) — and a process with no application (a
// test) has nothing to withhold.
bool IsRuntime()
{
	return appData != nullptr && (!appData->StudioMode() || s_asApplication > 0);
}

// The values of this application's options — read once and kept. Called under s_valuesLock.
const ibFeatureOptionValues& ValuesOf(const ibMetaData* metaData)
{
	const ibString configMD5 = metaData->GetApplicationMD5();

	auto found = s_values.find(metaData);
	if (found != s_values.end() && found->second.m_applicationMD5 == configMD5)
		return found->second;

	std::map<ibMetaID, bool> own;
	std::map<ibMetaID, ibMetaID> required;
	for (const ibValueMetaObjectFeatureOption* option :
			metaData->GetAnyArrayObject<ibValueMetaObjectFeatureOption>(g_metaFeatureOptionCLSID)) {
		own[option->GetMetaID()] = option->IsOn();
		const ibMetaID parent = option->GetRequired();
		if (parent != ibNOT_FOUND)
			required[option->GetMetaID()] = parent;
	}

	ibFeatureOptionValues& read = s_values[metaData];
	read.m_applicationMD5 = configMD5;
	read.m_values = ibFeatureOptionGate::EffectiveValues(own, required);
	read.m_anyOff = false;
	for (const auto& value : read.m_values)
		read.m_anyOff = read.m_anyOff || !value.second;
	return read;
}

// Available under these values — the object and everything it stands inside. Called under s_valuesLock.
bool IsAvailableUnder(const ibValueMetaObject* object, const ibFeatureOptionValues& read)
{
	for (const ibValueMetaObject* step = object; step != nullptr; step = step->GetParent()) {
		if (!ibFeatureOptionGate::IsMemberAvailable(step->GetFeatureOptions(), read.m_values))
			return false;
	}
	return true;
}

}

bool ibFeatureOptionGate::IsAvailable(const ibValueMetaObject* object)
{
	if (object == nullptr || !IsRuntime())
		return true;

	const ibMetaData* metaData = object->GetMetaData();
	if (metaData == nullptr)
		return true;

	std::lock_guard<std::mutex> lock(s_valuesLock);
	const ibFeatureOptionValues& read = ValuesOf(metaData);

	// The object itself, and everything it stands inside: a field of an unavailable catalog is unavailable too.
	return !read.m_anyOff || IsAvailableUnder(object, read);
}

bool ibFeatureOptionGate::IsTypeAvailable(const ibMetaData* metaData, const ibTypeDescription& typeDesc)
{
	const std::vector<ibClassID>& types = typeDesc.GetClsidList();
	if (metaData == nullptr || types.empty() || !IsRuntime())
		return true;

	std::lock_guard<std::mutex> lock(s_valuesLock);
	const ibFeatureOptionValues& read = ValuesOf(metaData);
	if (!read.m_anyOff)
		return true;

	for (const ibClassID& clsid : types) {
		// A type that names no single object of this application keeps the field: a primitive, a family
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

bool ibFeatureOptionGate::IsAvailable(const ibMetaData* metaData, const std::set<ibMetaID>& options)
{
	if (metaData == nullptr || options.empty() || !IsRuntime())
		return true;

	std::lock_guard<std::mutex> lock(s_valuesLock);
	const ibFeatureOptionValues& read = ValuesOf(metaData);
	return !read.m_anyOff || IsMemberAvailable(options, read.m_values);
}

bool ibFeatureOptionGate::AnyUnavailable(const ibMetaData* metaData)
{
	if (metaData == nullptr || !IsRuntime())
		return false;

	std::lock_guard<std::mutex> lock(s_valuesLock);
	return ValuesOf(metaData).m_anyOff;
}

void ibFeatureOptionGate::Forget(const ibMetaData* metaData)
{
	std::lock_guard<std::mutex> lock(s_valuesLock);
	s_values.erase(metaData);
}

// ⭐ THE STUDIO KEEPS NO APPLICATION'S MEMORY. A running application reads the values once and keeps them
// (its forms were built by them); the studio has nothing built by them, so the application's view it is
// asked for is the base as it stands NOW — read afresh when a run opens its outermost scope, and kept for that
// run's own questions. Without it a switch made in the application since was seen here only after the
// studio restarted, and compose_run said so in its description (2026-09-28).
ibFeatureOptionGate::AsApplication::AsApplication()
{
	if (s_asApplication++ == 0 && appData != nullptr && appData->StudioMode()) {
		std::lock_guard<std::mutex> lock(s_valuesLock);
		s_values.clear();
	}
}

ibFeatureOptionGate::AsApplication::~AsApplication() { --s_asApplication; }
