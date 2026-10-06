#include "layerObject.h"
#include "sfrontend/visualView/ctrl/frame.h"   // ibValueFrame — owner metadata

// Layer object — shared metadata for every kind (bar and item). It hangs off the owner FRAME the
// concrete kind resolves via GetOwnerFrame().

const ibMetaData* ibValueLayerObject::GetMetaData() const
{
	// Borrow the owner form's metadata — a layer object carries none of its own; the properties
	// (picture, translate string, action) need it.
	ibValueFrame* owner = GetOwnerFrame();
	return owner != nullptr ? owner->GetMetaData() : nullptr;
}
