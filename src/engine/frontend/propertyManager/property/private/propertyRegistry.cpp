#include "propertyRegistry.h"

#include <algorithm>
#include <typeinfo>

// Function-local static: the makers register from static initialisers across the DLL, so
// the vector must exist on first touch regardless of translation-unit order.
std::vector<ibPropertyRegistry::ibPropertyEntry>& ibPropertyRegistry::GetEntries()
{
	static std::vector<ibPropertyEntry> s_entries;
	return s_entries;
}

std::vector<ibPropertyRegistry::ibPropertySilent>& ibPropertyRegistry::GetSilent()
{
	static std::vector<ibPropertySilent> s_silent;
	return s_silent;
}

wxPGProperty* ibPropertyRegistry::Create(ibBackendProperty* property)
{
	if (property == nullptr)
		return nullptr;

	std::vector<ibPropertyEntry>& entries = GetEntries();

	// Sorted on demand, not at registration: every maker is in place by the time the first
	// property is rendered. stable_sort keeps registration order within one priority, so
	// only the base-after-derived intent reorders anything.
	static bool s_sorted = false;
	if (!s_sorted) {
		std::stable_sort(entries.begin(), entries.end(),
			[](const ibPropertyEntry& lhs, const ibPropertyEntry& rhs) { return lhs.m_priority < rhs.m_priority; });
		s_sorted = true;
	}

	for (const ibPropertyEntry& entry : entries) {
		if (wxPGProperty* pgProperty = entry.m_maker(property))
			return pgProperty;
	}

	// Declared editor-less on purpose? Then a null is the right answer, not a bug.
	for (const ibPropertySilent& silent : GetSilent()) {
		if (silent(property))
			return nullptr;
	}

	// Nothing matched. If the front is loaded at all, this is a property type nobody taught
	// the inspector to draw — it would silently vanish from the grid, and the missing
	// Register is the bug. Say so loudly instead of returning a quiet null.
	wxFAIL_MSG(wxString::Format(
		wxT("ibPropertyRegistry: no frontend property registered for '%s' (property '%s'). ")
		wxT("Add a Register() for it in frontend/propertyManager/property/advprop/."),
		wxString::FromAscii(typeid(*property).name()), property->GetName()));

	return nullptr;
}
