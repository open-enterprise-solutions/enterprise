#ifndef _STUDIO_SHARED_ATTRIBUTE_EDIT_H__
#define _STUDIO_SHARED_ATTRIBUTE_EDIT_H__

// A SHARED ATTRIBUTE'S COMPOSITION AS THE STUDIO EDITS IT — the desktop designer's ibSharedAttributeEditDocument and
// ibSharedAttributeEditView (designer/docManager/templates/docViewSharedAttribute.h) with its editor
// (designer/win/editor/sharedAttributeEditor): the objects that carry it, ticked. The walk is the editor's (its FillData)
// — every object that may carry one (IsCompositionAllowed), under its group — writing the rows the client's copy of the
// editor draws (ViewKind SharedAttribute); a tick is its OnCheckItem, and it moves the attribute itself: checking
// creates the copy inside that object, unchecking deletes it. Not the desktop's names — see docViewModuleEdit.h.

#include "frmserver/docView/docView.h"

class FRMSERVER_API ibSharedAttributeCompositionView : public ibMetaView {
public:

	ibSharedAttributeCompositionView() : ibMetaView() {}

	// The attribute's composition — the application's row, under it a group per metatype, under each the objects of
	// it that may carry one, ticked where this one is carried.
	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::SharedAttribute; }
	// An object ticked or not (Toggle {NodeId, Checked}) — the attribute put into it or taken out.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

private:

	// A metatype's group row — the application's row itself for a metatype with no constructor, as the desktop's.
	ibDataNode& GroupFor(ibDataNode& root, const ibClassID& clsid);

	ibDataNode& AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject);

	void FillData(ibDataNode& root);
};

// Its document — the shared attribute metaobject's (ibMetaDocument::GetMetaObject); the composition lives in the
// objects, saved with the application — nothing of its own to hold; its Save is the application's (see
// docViewInterfaceEdit.h).
class FRMSERVER_API ibSharedAttributeCompositionDocument : public ibMetaDocument {
public:

	ibSharedAttributeCompositionDocument() : ibMetaDocument() {}

	ibSharedAttributeCompositionDocument(const ibSharedAttributeCompositionDocument&) = delete;
	ibSharedAttributeCompositionDocument& operator=(const ibSharedAttributeCompositionDocument&) = delete;
};

#endif
