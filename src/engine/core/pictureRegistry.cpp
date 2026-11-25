#include "pictureRegistry.h"

#include <algorithm>
#include <mutex>

////////////////////////////////////////////////////////////////////////////////////////////////////////

// Registered as the DLLs load and read from every thread after — the lock is for a DLL loaded later (a file base's
// server in the studio) writing while a window reads.
static std::mutex s_registryLock;

static std::vector<ibPictureEntry>& Registry()
{
	static std::vector<ibPictureEntry> s_registry;
	return s_registry;
}

static std::vector<ibPictureEntry>::iterator FindRegistered(ibPictureID id)
{
	std::vector<ibPictureEntry>& registry = Registry();
	return std::find_if(registry.begin(), registry.end(), [id](const ibPictureEntry& entry) { return entry.m_id == id; });
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibPictureRegistry::RegisterPicture(const ibString& name, ibPictureID id, const ibString& svg)
{
	const std::lock_guard<std::mutex> lock(s_registryLock);
	if (FindRegistered(id) != Registry().end())
		return false;
	Registry().push_back({ name, id, ibPicture::FromSvg(svg) });
	return true;
}

bool ibPictureRegistry::IsRegisterPicture(ibPictureID id)
{
	const std::lock_guard<std::mutex> lock(s_registryLock);
	return FindRegistered(id) != Registry().end();
}

ibPicture ibPictureRegistry::GetPicture(ibPictureID id)
{
	const std::lock_guard<std::mutex> lock(s_registryLock);
	const auto found = FindRegistered(id);
	return found != Registry().end() ? found->m_picture : ibPicture();
}

std::vector<ibPictureEntry> ibPictureRegistry::GetArrayPicture()
{
	const std::lock_guard<std::mutex> lock(s_registryLock);
	return Registry();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RegisterPicture(const ibString name, const ibPictureID& id, const ibString& svg)
{
	if (ibPictureRegistry::RegisterPicture(name, id, svg))
		return true;

	ibASSERT_MSG(false, "Picture is register");
	return false;
}
