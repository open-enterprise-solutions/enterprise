#include "pictureRegistry.h"

#include <algorithm>
#include <mutex>

#include "core/utils/base64.h"   // ibBase64 — a master's text in the source, read into its bytes

////////////////////////////////////////////////////////////////////////////////////////////////////////

// Registered as the DLLs load and read from every thread after — the lock is for a DLL loaded later (a file base's
// server in the thin designer) writing while a window reads.
static std::mutex s_registryLock;

static std::vector<ibPictureEntry>& Registry()
{
	static std::vector<ibPictureEntry> s_registry;
	return s_registry;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibPictureRegistry::RegisterPicture(const ibString& name, ibPictureID id, const ibPicture& picture)
{
	const std::lock_guard<std::mutex> lock(s_registryLock);
	std::vector<ibPictureEntry>& registry = Registry();
	if (std::any_of(registry.begin(), registry.end(), [id](const ibPictureEntry& entry) { return entry.m_id == id; }))
		return false;
	registry.push_back({ name, id, picture });
	return true;
}

bool ibPictureRegistry::IsRegisterPicture(ibPictureID id)
{
	const std::lock_guard<std::mutex> lock(s_registryLock);
	const std::vector<ibPictureEntry>& registry = Registry();
	return std::any_of(registry.begin(), registry.end(), [id](const ibPictureEntry& entry) { return entry.m_id == id; });
}

ibPicture ibPictureRegistry::GetPicture(ibPictureID id)
{
	const std::lock_guard<std::mutex> lock(s_registryLock);
	const std::vector<ibPictureEntry>& registry = Registry();
	const auto found = std::find_if(registry.begin(), registry.end(), [id](const ibPictureEntry& entry) { return entry.m_id == id; });
	return found != registry.end() ? found->m_picture : ibPicture();
}

std::vector<ibPictureEntry> ibPictureRegistry::GetArrayPicture()
{
	const std::lock_guard<std::mutex> lock(s_registryLock);
	return Registry();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterPicture(const ibString name, const ibPictureID& id, const ibString& base64)
{
	ibMemoryBuffer png;
	if (ibBase64::Read(base64, png) && ibPictureRegistry::RegisterPicture(name, id, ibPicture(png)))
		return true;

	wxASSERT_MSG(false, "Picture is register");
	return false;
}
