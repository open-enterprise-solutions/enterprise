#ifndef _FRMCLIENT_DOC_VIEW_H__
#define _FRMCLIENT_DOC_VIEW_H__

/////////////////////////////////////////////////////////////////////////////
// Name:        frmclient/docView/docView.h
// Purpose:     the thin client's half of the doc/view: the metadata adapter
//              over the desktop's doc/view (deskgui/docview/docView.h).
/////////////////////////////////////////////////////////////////////////////

#include "deskgui/docview/docView.h"

#if wxUSE_DOC_VIEW_ARCHITECTURE

#include "frmclient/frmclient.h"   // FRMCLIENT_API
#include "frmclient/backend/metaCollection/metaObject.h"

// ============================================================================
// THE METADATA DOCUMENT AND VIEW — the old desktop's ibMetaDocument / ibMetaView, as the
// documents copied from the desktop call them. A document's metaobject is the server's: the client's has none, and
// ConvertMetaObjectToType answers none.
// ============================================================================

class FRMCLIENT_API ibMetaDocument : public ibFrontendDocument {
public:

    ibMetaDocument() : m_metaObject(nullptr), m_childDoc(true) {}

    virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }

    template <typename T>
    inline T* ConvertMetaObjectToType() {
        ibValueMetaObject* object = GetMetaObject();
        return object != nullptr ? object->ConvertToType<T>() : nullptr;
    }

    // Treats every metadata doc as "child" regardless of m_documentParent.
    virtual bool IsChildDocument() const override { return m_childDoc; }

protected:

    ibValueMetaObject* m_metaObject;	// current metadata object — none on the client
    bool m_childDoc;
};

class FRMCLIENT_API ibMetaView : public ibFrontendView {
public:

    ibMetaDocument* GetDocument() const {
        return dynamic_cast<ibMetaDocument*>(m_viewDocument);
    }

    virtual void OnDraw(wxDC* WXUNUSED(dc)) override {}
};

#endif // wxUSE_DOC_VIEW_ARCHITECTURE

#endif // _FRMCLIENT_DOC_VIEW_H__
