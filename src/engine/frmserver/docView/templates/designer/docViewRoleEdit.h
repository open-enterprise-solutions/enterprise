#ifndef _DESIGNER_ROLE_EDIT_H__
#define _DESIGNER_ROLE_EDIT_H__

// A ROLE AS THE DESIGNER EDITS IT — the desktop designer's ibRoleEditDocument and ibRoleEditView
// (designer/docManager/templates/docViewRole.h) with its editor (designer/win/editor/roleEditor): every object that has
// rights, and beside it the rights the role gives on the one selected. The walk is the editor's (its FillData) — every
// object with a right (GetRoleCount), under its group, a section's sub-sections under it — writing the rows the
// client's copy of the editor draws (ViewKind Role), each with its rights as its OnSelectedItem listed them; a tick is
// its OnCheckItem. Not the desktop's names — see docViewModuleEdit.h.

#include "frmserver/docView/docView.h"

class FRMSERVER_API ibRoleRightsView : public ibMetaView {
public:

	ibRoleRightsView() : ibMetaView() {}

	// The role's rights — the configuration's row, under it a group per metatype, under each the objects of it that have
	// rights, each with its rights (Roles) as this role gives them.
	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocolViewKind GetViewKind() const override { return ibProtocolViewKind::Role; }
	// A right ticked or not (Toggle {NodeId, Name, Checked}) — given by the role or taken away.
	virtual bool OnClientEvent(ibProtocolEvent event, const ibDataNode& args) override;

private:

	// A metatype's group row — the configuration's row itself for a metatype with no constructor, as the desktop's.
	ibDataNode& GroupFor(ibDataNode& root, const ibClassID& clsid);

	ibDataNode& AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject);
	// An object's rights as the role gives them — what the desktop's OnSelectedItem put into its check tree.
	void AppendRoles(ibDataNode& item, const ibValueMetaObject* metaObject);

	// A section's sub-sections, under it — they have rights of their own.
	void AddInterfaceItem(ibValueMetaObject* metaObject, ibDataNode& parent);

	void FillData(ibDataNode& root);

	wxDECLARE_DYNAMIC_CLASS(ibRoleRightsView);
};

// Its document — the role metaobject's (ibMetaDocument::GetMetaObject); the rights live in the objects, saved with the
// configuration — nothing of its own to hold; its Save is the configuration's (see docViewInterfaceEdit.h).
class FRMSERVER_API ibRoleRightsDocument : public ibMetaDocument {
public:

	ibRoleRightsDocument() : ibMetaDocument() {}

	wxDECLARE_NO_COPY_CLASS(ibRoleRightsDocument);
	wxDECLARE_DYNAMIC_CLASS(ibRoleRightsDocument);
};

#endif
