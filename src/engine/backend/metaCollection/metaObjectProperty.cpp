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
	// ⭐ ONE DOOR FOR THE RENAME. This used to ask the metadata for its tree and rename THROUGH it,
	// falling back to the metadata when there was none — two roads to one state, and the tree's road
	// did nothing of its own but call the very method the fallback called.
	if (m_propertyName == property)
		return m_metaData->RenameMetaObject(this, newValue.GetString());
	m_metaData->Modify(true);
	return true;
}

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

	// ⭐ THE EDIT IS STATED, NOT DRAWN. This used to ask the metadata for its tree — purely to learn
	// "am I in a designer?" — and then reach through a session for the main window and repaint it.
	// Three things the engine has no business knowing, to say one thing it does: this metaobject was
	// edited. Whoever is watching repaints whatever it is that shows it; a host with nobody watching
	// draws nothing, which is what the null tree used to stand for.
	m_metaData->MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Edited, this);
}