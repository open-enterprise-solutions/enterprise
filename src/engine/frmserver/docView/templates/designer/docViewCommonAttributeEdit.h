#ifndef _DESIGNER_COMMON_ATTRIBUTE_EDIT_H__
#define _DESIGNER_COMMON_ATTRIBUTE_EDIT_H__

// A COMMON ATTRIBUTE'S COMPOSITION AS THE DESIGNER EDITS IT — the desktop designer's ibCommonAttributeEditDocument and
// ibCommonAttributeEditView (designer/docManager/templates/docViewCommonAttribute.h) with its editor
// (designer/win/editor/commonAttributeEditor): the objects that carry it, ticked. The walk is the editor's (its FillData)
// — every object that may carry one (IsCompositionAllowed), under its group — writing the rows the client's copy of the
// editor draws (ViewKind CommonAttribute); a tick is its OnCheckItem, and it moves the attribute itself: checking
// creates the copy inside that object, unchecking deletes it. Not the desktop's names — see docViewModuleEdit.h.

#include "frmserver/docView/docView.h"

class FRMSERVER_API ibCommonAttributeCompositionView : public ibMetaView {
public:

	ibCommonAttributeCompositionView() : ibMetaView() {}

	// The attribute's composition — the configuration's row, under it a group per metatype, under each the objects of
	// it that may carry one, ticked where this one is carried.
	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::CommonAttribute; }
	// An object ticked or not (Toggle {NodeId, Checked}) — the attribute put into it or taken out.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

private:

	// A metatype's group row — the configuration's row itself for a metatype with no constructor, as the desktop's.
	ibDataNode& GroupFor(ibDataNode& root, const ibClassID& clsid);

	ibDataNode& AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject);

	void FillData(ibDataNode& root);

	wxDECLARE_DYNAMIC_CLASS(ibCommonAttributeCompositionView);
};

// Its document — the common attribute metaobject's (ibMetaDocument::GetMetaObject); the composition lives in the
// objects, saved with the configuration — nothing of its own to hold; its Save is the configuration's (see
// docViewInterfaceEdit.h).
class FRMSERVER_API ibCommonAttributeCompositionDocument : public ibMetaDocument {
public:

	ibCommonAttributeCompositionDocument() : ibMetaDocument() {}

	ibCommonAttributeCompositionDocument(const ibCommonAttributeCompositionDocument&) = delete;
	ibCommonAttributeCompositionDocument& operator=(const ibCommonAttributeCompositionDocument&) = delete;
	wxDECLARE_DYNAMIC_CLASS(ibCommonAttributeCompositionDocument);
};

#endif
