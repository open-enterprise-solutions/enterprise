#ifndef __RAM_COMPOSER_H__
#define __RAM_COMPOSER_H__

////////////////////////////////////////////////////////////////////////////
//	ibDataRamComposer — the L5-2 composer for a RAM value-storage source
////////////////////////////////////////////////////////////////////////////
//
// The RAM realisation of ibDataComposer. Where ibDataDBComposer (DB, L5-1) renders the settings into L4-1
// query TEXT and runs the parse → lower → walk pipeline over an ibBackendQueryable, ibDataRamComposer applies the
// SAME settings (filter + sort, + grouping later) IN PLACE over a RAM VALUE-STORAGE — ibRamValueStorage, the
// RAM analog of a queryable: a flat/tree table that OWNS the live nodes. The composer reads the storage's nodes
// DIRECTLY (no query text, no parser, no lowering, no ibRamTableQueryable / ComputeRows copy) and materialises
// the view. The composer NEVER references the model — only the DATA (the storage); mutations + notify are the
// model's job. See docs/ram-composer-decoupling.md.
//
// ComputeOrder / Run are defined out-of-line in model.cpp (they read the storage's node API).

#include "backend/composition/dataComposer.h"   // ibDataComposer

#include <vector>

class ibRamValueStorage;

class BACKEND_API ibDataRamComposer : public ibDataComposer
{
public:
	// Bind the RAM value-storage this composer filters + sorts (NON-owning — the model owns the storage AND us).
	ibDataRamComposer& FromStorage(const ibRamValueStorage* storage) {
		m_storage = storage;
		return *this;
	}

	bool HasSource() const override { return m_storage != nullptr; }

	// The ONLY output: filter + sort (+ group later) the storage's nodes → their STORAGE indices in display
	// order (index i ↔ storage node i). The model (RunComposerPage) windows this by the browsed anchor and
	// returns the LIVE nodes — the node IS the storage row. NO driver walk, NO SQL — the base Run() default (a
	// no-op) is inherited, so L5-2 is fully self-contained. slice-1: filter + sort, flat.
	std::vector<long> ComputeOrder() const;

private:
	const ibRamValueStorage* m_storage = nullptr;   // the RAM value-storage (NON-owning; owned by the model)
};

#endif // __RAM_COMPOSER_H__
