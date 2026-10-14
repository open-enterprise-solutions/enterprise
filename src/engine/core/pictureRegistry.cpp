#include "pictureRegistry.h"

#include <algorithm>
#include <mutex>

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

bool ibPictureRegistry::RegisterPicture(const wxString& name, ibPictureID id, const ibPicture& picture)
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

bool RegisterPicture(const wxString name, const ibPictureID& id, const wxString& base64)
{
	if (ibPictureRegistry::RegisterPicture(name, id, ibPicture(base64)))
		return true;

	wxASSERT_MSG(false, "Picture is register");
	return false;
}
