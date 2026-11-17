////////////////////////////////////////////////////////////////////////////
//	Description : ibBackendQueryProvider — what the interface answers itself, for every
//	              provider alike (queryProvider.h).
////////////////////////////////////////////////////////////////////////////

#include "backend/query/queryProvider.h"

// ⭐ WHAT A SORT BY A REFERENCE COLUMN SORTS BY — the order column of what it points at (ibBackendColumnSortOrder).
// A reference to one of SEVERAL kinds — a register's recorder, an analytics slot of goods and counterparties — sorts
// by the one every kind has under ONE name: a walk through it finds it in each kind by that name, each kind's own,
// and the fields of one role meet in one COALESCE. A kind with no order of its own leaves the reference its
// identity's.
std::shared_ptr<const ibBackendQueryColumn> ibBackendQueryProvider::ResolveReferenceSortColumn(const std::shared_ptr<const ibBackendQueryable>& queryable,
                                                                               const std::shared_ptr<const ibBackendQueryColumn>& refColumn) const
{
	if (refColumn == nullptr)
		return nullptr;
	if (const std::shared_ptr<const ibBackendQueryable> target = ResolveReferenceTarget(queryable, refColumn))
		return target->GetSortColumn();
	std::shared_ptr<const ibBackendQueryColumn> shared;
	for (const std::shared_ptr<const ibBackendQueryable>& target : ResolveReferenceTargets(queryable, refColumn)) {
		std::shared_ptr<const ibBackendQueryColumn> by = target != nullptr ? target->GetSortColumn() : nullptr;
		if (by == nullptr || (shared != nullptr && !by->GetName().IsSameAs(shared->GetName(), false)))
			return nullptr;
		if (shared == nullptr)
			shared = by;
	}
	return shared;
}
