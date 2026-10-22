#ifndef _FRMCLIENT_BACKEND_META_DATA_H__
#define _FRMCLIENT_BACKEND_META_DATA_H__

#include <initializer_list>
#include <vector>

#include "core/clsid.h"   // ibClassID
#include "frmclient/frmclient.h"   // FRMCLIENT_API

class ibCtorAbstractType;
class ibValueMetaObject;

// THE CONFIGURATION — the engine's ibMetaData: the server's, which the client reaches through the tab that shows it
// (a tree's, a form's, a module's — the server finds which, ibSchemaMetaData::FindMetaData); tab 0 — the session's.
// A value: the tab's document gives it (ibTabDocument::GetMetaData), a widget keeps it as a field, and it is handed on
// as the desktop's was — what a configuration lists, it lists on the server, asked with this tab. The client's objects
// have none (GetMetaData answers null).
class FRMCLIENT_API ibMetaData {
public:
	explicit ibMetaData(long long tabId = 0) : m_tabId(tabId) {}

	// The tab a question to the server names — its configuration's.
	long long GetTabId() const { return m_tabId; }

	void GetOwner(ibMetaData*& owner) const { owner = nullptr; }

	ibCtorAbstractType* GetAvailableCtor(const ibClassID& WXUNUSED(clsid)) const { return nullptr; }

	// Whether what the tab shows may be changed — the server's word, said with the tab (ibFrame::ibTab::ReadOnly: its
	// document's IsEditable); read off the frame as it stands, so it answers the tab's state now. No tab, or none said:
	// editable — the desktop's answer for a configuration nobody restricted.
	bool IsEditable() const;

	std::vector<const ibValueMetaObject*> GetAnyArrayObject(const std::initializer_list<ibClassID> WXUNUSED(classes)) const { return {}; }
	template <class T>
	std::vector<const T*> GetAnyArrayObject(const ibClassID& WXUNUSED(clsid)) const { return {}; }

private:
	long long m_tabId = 0;
};

#endif
