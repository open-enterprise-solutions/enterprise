////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject menu
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "backend/metaData.h"

void ibValueMetaObject::OnPropertyCreated(ibProperty* property)
{
}

void ibValueMetaObject::OnPropertySelected(ibProperty* property)
{
}

bool ibValueMetaObject::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	ibBackendMetadataTree* metadataTree = m_metaData->GetMetaTree();
	if (m_propertyName == property && metadataTree != nullptr)
		return metadataTree->RenameMetaObject(this, newValue.GetString());
	else if (m_propertyName == property)
		return m_metaData->RenameMetaObject(this, newValue.GetString());
	m_metaData->Modify(true);
	return true;
}

#include "backend/backend_mainFrame.h"
#include "backend/appData.h"
#include "backend/session/session.h"

void ibValueMetaObject::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// THE RENAME IS COMPLETE AT THIS POINT — OnPropertyChanging ran before the value was applied,
	// so from here every ctor of this metaobject computes a different name than before. Say so and
	// nothing more: the METADATA's registry marks its by-name cache stale and recomputes it on the
	// next lookup. The global value factory is deliberately not touched — it holds the STATIC
	// types, whose names cannot move. Identities (clsid) do not move either, so nothing is
	// registered again, and this one line covers every metatype there is and every one to come.
	if (m_propertyName == property) {
		if (m_metaData != nullptr)
			m_metaData->InvalidateCtorNames();
		m_propertySynonym->SetValue(stringUtils::GenerateSynonym(newValue));
	}
	wxASSERT(m_metaData);
	const ibBackendMetadataTree* metadataTree = m_metaData->GetMetaTree();
	// Metaobject property edit — designer UI repaint. Reach frame via
	// the main session (designer has a single session per process).
	if (metadataTree != nullptr) {
		if (auto* frame = ibSession::CurrentFrame())
			frame->RefreshFrame();
	}
}