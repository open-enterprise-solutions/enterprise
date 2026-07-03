#ifndef _LAYER_OBJECT_H__
#define _LAYER_OBJECT_H__

#include "frontend/frontend.h"           // FRONTEND_API (cross-module export macro)
#include "backend/compiler/value.h"
#include "backend/propertyManager/propertyManager.h"

class ibValueFrame;
class ibMetaData;
class wxMenu;
class ibFrontendVisualEditorNotebook;

//********************************************************************************************
//*                        Layer object — common runtime+property base                       *
//********************************************************************************************

// A form LAYER (the toolbar/command bar today; search field, status, … later) is rendered by a
// runtime subsystem OVER a control. Every object such a layer is built from — the bar itself AND
// its child items — is at once a runtime value (ibValueDynamicMembers) and a designer-editable
// object (ibPropertyObject). This ABSTRACT base unifies them so the designer tree holds ONE
// pointer type and never casts per concrete kind: metadata, property/event routing, and the
// context menu are all virtual here. Never instantiated on its own (GetOwnerFrame is pure).
class FRONTEND_API ibValueLayerObject : public ibValueDynamicMembers, public ibPropertyObject {
public:

	ibValueLayerObject() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {}
	virtual ~ibValueLayerObject() {}

	// The owner FRAME this object routes through — its metadata feeds the property editors and
	// its FindVisualEditor() drives the designer refresh. Each kind resolves it its own way.
	virtual ibValueFrame* GetOwnerFrame() const = 0;

	// A layer container (the bar) holds children; a leaf (an item) does not. Lets the tree pick
	// the right context menu / icon without a dynamic_cast.
	virtual bool IsLayerContainer() const = 0;

	// Designer tree open-state for a container node (a leaf ignores it). Kept on the object so
	// RebuildTree restores it, mirroring a control's ibValueFrame::GetExpanded.
	virtual bool IsTreeExpanded() const { return true; }
	virtual void SetTreeExpanded(bool value) {}

	// Designer context menu — SAME hook names a control uses (ibValueFrame::PrepareDefaultMenu /
	// ExecuteMenu). Each kind fills its own and runs the chosen id; the tree forwards blindly.
	virtual void PrepareDefaultMenu(wxMenu* menu) const {}
	virtual void ExecuteMenu(ibFrontendVisualEditorNotebook* editor, int menuId) {}

	// ibValue concrete requirements — shared (a layer object exposes no script members).
	virtual bool Init(ibValue** paParams, const long lSizeArray) override { return true; }
	virtual bool IsEmpty() const override { return false; }
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override { return false; }
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override { return false; }

	// Metadata + designer routing, shared through GetOwnerFrame() (borrow the owner form's).
	virtual const ibMetaData* GetMetaData() const override;
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;
	virtual void OnEventChanged(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue) override;
};

#endif
