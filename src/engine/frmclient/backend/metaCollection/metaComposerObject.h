#ifndef _FRMCLIENT_BACKEND_META_COMPOSER_OBJECT_H__
#define _FRMCLIENT_BACKEND_META_COMPOSER_OBJECT_H__

#include <wx/variant.h>

class ibMetaData;

// A COMPOSER OF THE CONFIGURATION — the engine's ibValueMetaObjectComposer (backend/metaCollection/
// metaComposerObject.h): the designer's, whose tab edits the composition its Settings property stores
// (ibComposerEditor). The client's documents have no metaobject — ConvertMetaObjectToType answers none — so
// nothing here is reached at runtime.
class ibValueMetaObjectComposer {
public:
	const ibMetaData* GetMetaData() const { return nullptr; }
	wxVariant GetCompositionValue() const { return wxVariant(); }
};

#endif
