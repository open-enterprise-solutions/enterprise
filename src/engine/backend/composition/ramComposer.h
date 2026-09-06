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
// ComputeOrder is defined out-of-line in ramComposer.cpp (it reads the storage's node API).

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
	// returns the LIVE nodes — the node IS the storage row. NO SQL on this road, so L5-2 stays
	// self-contained; PRINTING the same composition goes through Run below. slice-1: filter + sort, flat.
	// ⚠ NOT const: it builds the filter in force, and BUILDING a condition registers the values it
	// compares against (ibDataComposer::AddParam). The const was covering that write.
	std::vector<long> ComputeOrder();

	// ⭐⭐ THE DRIVER WALK, FOR RAM. A driver does not care where rows come from — it is handed a schema and
	// then rows — so a table of values prints onto a sheet exactly as a query does; only the source differs,
	// and here it is a degenerate table rather than a database (Max, 2026-08-29).
	//
	// 🛑 IT USED TO INHERIT THE BASE'S NO-OP. The RAM display path is ComputeOrder + the live nodes windowed
	// on the model side, so nothing ever needed a walk — and anything that wanted the composition PRINTED
	// (output list, and search after it) got an empty answer from a table that plainly had rows.
	//
	// Flat: one output, the selected fields as Detail columns, the rows in the order in force. Grouping is
	// the model's own doing on the display side (RunStoragePage folds the levels) and is not repeated here.
	virtual bool Run(ibCompositionDriver& driver) override;

	// ⚠ The no-argument Run comes with it — an override hides the whole name, and a caller holding
	// this concrete type would lose the outputs-and-their-drivers one. Same note as the DB composer.
	using ibDataComposer::Run;

	// …and a copy of itself — see ibDataComposer::Clone. The storage pointer rides along: a copy reads the
	// SAME rows, which is the whole point of taking one.
	virtual ibDataRamComposer* Clone() const override { return new ibDataRamComposer(*this); }

private:
	const ibRamValueStorage* m_storage = nullptr;   // the RAM value-storage (NON-owning; owned by the model)
};

#endif // __RAM_COMPOSER_H__
