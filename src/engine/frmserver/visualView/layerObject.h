#ifndef _LAYER_OBJECT_H__
#define _LAYER_OBJECT_H__

#include "frmserver/frmserver.h"           // FRMSERVER_API (cross-module export macro)
#include "backend/compiler/value.h"
#include "backend/propertyManager/propertyManager.h"

class ibValueFrame;
class ibValueCommandBar;
class ibMetaData;
class ibDataNode;
struct ibCommandEntry;

//********************************************************************************************
//*                        Layer object — common runtime+property base                       *
//********************************************************************************************

// A form LAYER (the command bar today; search field, status, … later) stands OVER a control. Every
// object such a layer is built from — the bar itself AND its child items — is at once a runtime value
// (ibValueDynamicMembers) and a property object (ibPropertyObject). This ABSTRACT base unifies them so
// a walk holds ONE pointer type and never casts per concrete kind. Never instantiated on its own
// (GetOwnerFrame is pure).
//
// ⭐ IT HOLDS ITS CHILDREN AS A CONTROL DOES — the same helper (GetChild, GetChildCount, AddChild, RemoveChild, GetParent):
// a bar's commands and submenus are its children, a submenu's are its own, to any depth.
class FRMSERVER_API ibValueLayerObject : public ibValueDynamicMembers, public ibPropertyObjectHelper<ibValueLayerObject> {
public:

	ibValueLayerObject() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {}
	virtual ~ibValueLayerObject() {}

	// The owner FRAME this object belongs to — its metadata feeds the properties. Each kind resolves it
	// its own way.
	virtual ibValueFrame* GetOwnerFrame() const = 0;
	// The BAR it hangs in — its parent's, up to the bar, which answers itself; none while it hangs in nothing.
	virtual const ibValueCommandBar* GetBar() const { return GetParent() != nullptr ? GetParent()->GetBar() : nullptr; }

	// Its name — a command's, a submenu's: one namespace on the bar. The bar has none.
	virtual ibString GetName() const { return ibString(); }
	// The entry it stands for on the bar, as it shows it — a command, a submenu a group holding its children's; the client
	// draws the bar and its menus from these. False: none (hidden, unavailable, bound to nothing alive). Its id is the
	// bar's to give.
	virtual bool GetCommandEntry(ibCommandEntry& entry) const { return false; }

	// A layer container (the bar, a submenu) holds children; a leaf (an item) does not. Lets the tree pick
	// the right context menu / icon without a dynamic_cast.
	virtual bool IsLayerContainer() const = 0;

	// Studio tree open-state for a container node (a leaf ignores it). Kept on the object so
	// RebuildTree restores it, mirroring a control's ibValueFrame::GetExpanded.
	virtual bool IsTreeExpanded() const { return true; }
	virtual void SetTreeExpanded(bool value) {}

	// Its own fields and its children, into a node and back — as a control reads and writes its own (ibValueFrame), so a
	// holder writes each child without asking which kind it is.
	virtual bool ReadData(const ibDataNode& node) { return true; }
	virtual bool WriteData(ibDataNode& node) const { return true; }

	// ibValue concrete requirements — shared (a layer object exposes no script members, unless it says otherwise).
	virtual bool Init(ibValue** paParams, const long lSizeArray) override { return true; }
	virtual bool IsEmpty() const override { return false; }
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override { return false; }
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override { return false; }

	// Metadata, shared through GetOwnerFrame() (borrow the owner form's).
	virtual const ibMetaData* GetMetaData() const override;
};

#endif
