#ifndef _FRMCLIENT_BACKEND_META_DATA_H__
#define _FRMCLIENT_BACKEND_META_DATA_H__

#include <initializer_list>
#include <vector>

#include "core/clsid.h"   // ibClassID

class ibCtorAbstractType;
class ibValueMetaObject;

// THE CONFIGURATION — the engine's ibMetaData: the server's, which the client reaches through the tab that shows it
// (a tree's, a form's, a module's — the server finds which, ibSchemaMetaData::FindMetaData); tab 0 — the session's.
// A value: the tab's document gives it (ibTabDocument::GetMetaData), a widget keeps it as a field, and it is handed on
// as the desktop's was — what a configuration lists, it lists on the server, asked with this tab. The client's objects
// have none (GetMetaData answers null).
class ibMetaData {
public:
	explicit ibMetaData(long long tabId = 0) : m_tabId(tabId) {}

	// The tab a question to the server names — its configuration's.
	long long GetTabId() const { return m_tabId; }

	void GetOwner(ibMetaData*& owner) const { owner = nullptr; }

	ibCtorAbstractType* GetAvailableCtor(const ibClassID& WXUNUSED(clsid)) const { return nullptr; }

	std::vector<const ibValueMetaObject*> GetAnyArrayObject(const std::initializer_list<ibClassID> WXUNUSED(classes)) const { return {}; }
	template <class T>
	std::vector<const T*> GetAnyArrayObject(const ibClassID& WXUNUSED(clsid)) const { return {}; }

private:
	long long m_tabId = 0;
};

#endif
