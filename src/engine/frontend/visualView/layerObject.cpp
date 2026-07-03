#include "layerObject.h"
#include "frontend/visualView/ctrl/frame.h"   // ibValueFrame — owner metadata + FindVisualEditor
#ifndef OES_USE_WEB
#include "frontend/visualView/visualHost.h"    // ibFrontendVisualEditorNotebook (designer routing)

namespace {
	// The owner frame's designer editor — the routing seam for property/event edits (this object
	// isn't an ibValueFrame, so it has no FindVisualEditor of its own). Null outside the designer.
	ibFrontendVisualEditorNotebook* FindEditor(ibValueFrame* owner) {
		return owner != nullptr ? owner->FindVisualEditor() : nullptr;
	}
}
#endif

// Layer object — shared metadata + designer routing for every kind (bar and item). All of it
// hangs off the owner FRAME the concrete kind resolves via GetOwnerFrame().

const ibMetaData* ibValueLayerObject::GetMetaData() const
{
	// Borrow the owner form's metadata — a layer object carries none of its own; the property
	// editors (picture, translate string, action picker) need it.
	ibValueFrame* owner = GetOwnerFrame();
	return owner != nullptr ? owner->GetMetaData() : nullptr;
}

void ibValueLayerObject::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
#ifndef OES_USE_WEB
	// ModifyProperty applies the value and, for a non-control owner, falls to RefreshEditor →
	// UpdateVisualHost, which re-renders the layer.
	if (ibFrontendVisualEditorNotebook* editor = FindEditor(GetOwnerFrame()))
		editor->ModifyProperty(property, oldValue, newValue);
#else
	(void)property; (void)oldValue; (void)newValue;
#endif
}

void ibValueLayerObject::OnEventChanged(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue)
{
#ifndef OES_USE_WEB
	// The Action event goes through ModifyEvent — creates/opens the handler procedure and jumps
	// to the code page, exactly as a control does.
	if (ibFrontendVisualEditorNotebook* editor = FindEditor(GetOwnerFrame()))
		editor->ModifyEvent(event, oldValue, newValue);
#else
	(void)event; (void)oldValue; (void)newValue;
#endif
}
