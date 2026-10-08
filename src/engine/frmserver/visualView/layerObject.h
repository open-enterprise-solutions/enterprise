#ifndef _LAYER_OBJECT_H__
#define _LAYER_OBJECT_H__

#include "frmserver/frmserver.h"           // FRMSERVER_API (cross-module export macro)
#include "backend/compiler/value.h"
#include "backend/propertyManager/propertyManager.h"

class ibValueFrame;
class ibMetaData;

//********************************************************************************************
//*                        Layer object — common runtime+property base                       *
//********************************************************************************************

// A form LAYER (the command bar today; search field, status, … later) stands OVER a control. Every
// object such a layer is built from — the bar itself AND its child items — is at once a runtime value
// (ibValueDynamicMembers) and a property object (ibPropertyObject). This ABSTRACT base unifies them so
// a walk holds ONE pointer type and never casts per concrete kind. Never instantiated on its own
// (GetOwnerFrame is pure).
class FRMSERVER_API ibValueLayerObject : public ibValueDynamicMembers, public ibPropertyObject {
public:

	ibValueLayerObject() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {}
	virtual ~ibValueLayerObject() {}

	// The owner FRAME this object belongs to — its metadata feeds the properties. Each kind resolves it
	// its own way.
	virtual ibValueFrame* GetOwnerFrame() const = 0;

	// A layer container (the bar) holds children; a leaf (an item) does not. Lets the tree pick
	// the right context menu / icon without a dynamic_cast.
	virtual bool IsLayerContainer() const = 0;

	// Designer tree open-state for a container node (a leaf ignores it). Kept on the object so
	// RebuildTree restores it, mirroring a control's ibValueFrame::GetExpanded.
	virtual bool IsTreeExpanded() const { return true; }
	virtual void SetTreeExpanded(bool value) {}

	// ibValue concrete requirements — shared (a layer object exposes no script members).
	virtual bool Init(ibValue** paParams, const long lSizeArray) override { return true; }
	virtual bool IsEmpty() const override { return false; }
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override { return false; }
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override { return false; }

	// Metadata, shared through GetOwnerFrame() (borrow the owner form's).
	virtual const ibMetaData* GetMetaData() const override;
};

#endif
