#ifndef _FRMCLIENT_BACKEND_META_OBJECT_METADATA_H__
#define _FRMCLIENT_BACKEND_META_OBJECT_METADATA_H__

#include <vector>

#include "core/description/homePageDescription.h"   // ibHomePageDescription — the start-page workspace
#include "core/serialize/dataBuilder.h"

#include "frmclient/backend/metaCollection/metaObject.h"

// THE APPLICATION'S ROOT — the engine's ibValueMetaObjectApplication (backend/metaCollection/metaObjectMetadata.h) as
// far as its home page workspace editor asks it: the description, and the forms the workspace may show — what the
// desktop's dialog asked of the application itself. The server's (schema HomePage): read as the editor opens, given
// back on its OK.
class FRMCLIENT_API ibValueMetaObjectApplication : public ibValueMetaObject {
public:

	using ibValueMetaObject::ibValueMetaObject;

	// The workspace and the forms, as the server has them now.
	void ReadHomePage();

	const ibHomePageDescription& GetHomePage() const { return m_homePage; }
	// …given back: the server's root takes it, and marks the application modified.
	void SetHomePage(const ibHomePageDescription& homePage);

	// The forms the workspace may show, as the server lists them (Id, Name, FullName, Picture, Common; a form's owner:
	// Owner, OwnerName, OwnerPicture).
	ibDataRange<ibDataNode> GetFormArray() const { return m_formArray.Children(); }

	virtual bool IsEditable() const override { return !m_readOnly; }

private:

	ibHomePageDescription m_homePage;
	ibDataNode m_formArray;
	bool m_readOnly = false;
};

#endif
