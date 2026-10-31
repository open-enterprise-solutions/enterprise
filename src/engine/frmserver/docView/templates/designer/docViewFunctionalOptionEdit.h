#ifndef _DESIGNER_FUNCTIONAL_OPTION_EDIT_H__
#define _DESIGNER_FUNCTIONAL_OPTION_EDIT_H__

// A FUNCTIONAL OPTION'S MEMBERS AS THE DESIGNER EDITS THEM — the desktop designer's ibFunctionalOptionEditDocument and
// ibFunctionalOptionEditView (designer/docManager/templates/docViewFunctionalOption.h) with its editor
// (designer/win/editor/functionalOptionEditor): what is not shown while the option is off, ticked. The walk is the
// editor's (its FillData) — every object that may belong (IsFunctionalOptionAllowed), under its group, with the fields
// and tables inside it that say the same — writing the rows the client's copy of the editor draws (ViewKind
// FunctionalOption); a tick is its OnCheckItem. Not the desktop's names — see docViewModuleEdit.h.

#include "frmserver/docView/docView.h"

class FRMSERVER_API ibFunctionalOptionMembersView : public ibMetaView {
public:

	ibFunctionalOptionMembersView() : ibMetaView() {}

	// The option's members — the configuration's row, under it a group per metatype, under each the objects of it that
	// may belong, under each its fields and tables at every depth, ticked where they belong to this one.
	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::FunctionalOption; }
	// An object, a field or a table ticked or not (Toggle {NodeId, Checked}) — in the option or out of it.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

private:

	// A metatype's group row — the configuration's row itself for a metatype with no constructor, as the desktop's.
	ibDataNode& GroupFor(ibDataNode& root, const ibClassID& clsid);

	ibDataNode& AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject);

	// The fields and tables inside an object that may belong on their own — at every depth, so a column of a tabular
	// section can be ticked without its table.
	void AppendChildren(ibDataNode& parent, ibValueMetaObject* metaObject);

	void FillData(ibDataNode& root);

	wxDECLARE_DYNAMIC_CLASS(ibFunctionalOptionMembersView);
};

// Its document — the functional option metaobject's (ibMetaDocument::GetMetaObject); the membership lives in the
// members, saved with the configuration — nothing of its own to hold; its Save is the configuration's (see
// docViewInterfaceEdit.h).
class FRMSERVER_API ibFunctionalOptionMembersDocument : public ibMetaDocument {
public:

	ibFunctionalOptionMembersDocument() : ibMetaDocument() {}

	ibFunctionalOptionMembersDocument(const ibFunctionalOptionMembersDocument&) = delete;
	ibFunctionalOptionMembersDocument& operator=(const ibFunctionalOptionMembersDocument&) = delete;
	wxDECLARE_DYNAMIC_CLASS(ibFunctionalOptionMembersDocument);
};

#endif
