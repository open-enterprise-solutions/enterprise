#ifndef _DESIGNER_INTERFACE_EDIT_H__
#define _DESIGNER_INTERFACE_EDIT_H__

// A SECTION AS THE DESIGNER EDITS IT — the desktop designer's ibInterfaceEditDocument and ibInterfaceEditView
// (designer/docManager/templates/docViewInterface.h) with its editor (designer/win/editor/interfaceEditor): the objects in
// the section, ticked where the section keeps them. The walk is the editor's (its FillData) — every object that may be in
// a section, under its group — writing the rows the client's copy of the editor draws (ViewKind Interface); a tick is its
// OnCheckItem. Not the desktop's names — see docViewModuleEdit.h.

#include "frmserver/docView/docView.h"

class FRMSERVER_API ibInterfaceCompositionView : public ibMetaView {
public:

	ibInterfaceCompositionView() : ibMetaView() {}

	// The section's composition — the configuration's row, under it a group per metatype, under each the objects of it
	// that may be in a section, ticked where this one keeps them.
	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocolViewKind GetViewKind() const override { return ibProtocolViewKind::Interface; }
	// An object ticked or not (Toggle {NodeId, Checked}) — in the section or out of it.
	virtual bool OnClientEvent(ibProtocolEvent event, const ibDataNode& args) override;

private:

	// A metatype's group row — the configuration's row itself for a metatype with no constructor, as the desktop's.
	ibDataNode& GroupFor(ibDataNode& root, const ibClassID& clsid);

	ibDataNode& AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject);

	void FillData(ibDataNode& root);

	wxDECLARE_DYNAMIC_CLASS(ibInterfaceCompositionView);
};

// Its document — the section metaobject's (ibMetaDocument::GetMetaObject); the composition lives in the objects, saved
// with the configuration — nothing of its own to hold. So its Save is the configuration's (ibMetaDocument::Save): the
// desktop's m_childDoc = false sent it to a SaveAs that writes nothing, and the configuration's «*» stayed.
class FRMSERVER_API ibInterfaceCompositionDocument : public ibMetaDocument {
public:

	ibInterfaceCompositionDocument() : ibMetaDocument() {}

	wxDECLARE_NO_COPY_CLASS(ibInterfaceCompositionDocument);
	wxDECLARE_DYNAMIC_CLASS(ibInterfaceCompositionDocument);
};

#endif
