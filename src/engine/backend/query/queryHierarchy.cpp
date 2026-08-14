////////////////////////////////////////////////////////////////////////////
//	Description : «IN HIERARCHY» - resolving what a named value stands for
////////////////////////////////////////////////////////////////////////////

#include "queryHierarchy.h"

#include "dataQueryBuilder.h"   // L3 door - the one read the whole walk needs
#include "queryProvider.h"      // ibBackendQueryProvider - ResolveReferenceTarget, the metadata owner

namespace {

// ⭐⭐ THE PARENT MAP, IN ONE READ. A subtree is walked DOWN, so what is wanted is children-by-parent
// — and the cheapest honest way to have it is to read the target's own (row key, parent) pairs once
// and index them. The alternative, one query per node, is a round trip per account: plausible on
// paper, and a hundred of them on a chart nobody would call large.
//
// The TARGET's OWN declarations answer both halves: its row key is what the filtered rows store
// against, and its parent column is the hierarchy. A source with no parent column is a FLAT list —
// the one arrangement that records no parent — and it contributes nothing, which leaves every named
// value standing for itself.
void ReadChildrenMap(const ibBackendQueryable* target, std::map<wxString, std::vector<ibValue>>& childrenOf)
{
	if (target == nullptr)
		return;

	const std::vector<const ibBackendQueryColumn*> keys = target->GetPrimaryKeyColumns();
	const ibBackendQueryColumn* rowKey = keys.empty() ? nullptr : keys.front();
	const ibBackendQueryColumn* parent = target->GetHierarchyColumn();
	if (rowKey == nullptr || parent == nullptr)
		return;

	// A source that cannot be read is a failure of the READING - the driver's own words travel to the
	// caller untouched, and there is nothing to catch on the way: answering with a silently narrower
	// filter would report a subtree missing its branches and look like a correct total.
	ibDataQueryBuilder q;
	q.From(target);
	q.Select(rowKey, wxEmptyString);
	q.Select(parent, wxEmptyString);

	ibDataQueryResult rows = q.Execute(ibReadPageRequest{});
	while (rows.Next()) {
		const ibValue self = rows.GetValue(rowKey);
		if (self.IsEmpty())
			continue;
		// A ROOT's parent is an EMPTY reference, not a null — it keys the map like any other value and
		// simply never gets asked for, because a descent starts at the values that were named.
		childrenOf[rows.GetValue(parent).GetHashKey()].push_back(self);
	}
}

} // namespace

ibQueryHierarchyScope::ibQueryHierarchyScope(const ibBackendQueryable* source, const ibBackendQueryColumn* column,
                                             const std::vector<ibValue>& named, ibQueryDimUnfold unfold)
{
	// «in» asks the database nothing: the values passed ARE the answer, and nothing is implied about
	// what stands under them.
	if (unfold == ibQueryDimUnfold::Elements) {
		for (const ibValue& value : named)
			if (!value.IsEmpty())
				m_accepted.push_back(value);
		return;
	}

	// ⭐ THE COLUMN SAYS WHERE THE SUBTREE LIVES, AND THE PROVIDER RESOLVES IT. Not the value: a value
	// would have to be cast to a reference and asked for its metaobject, which is metadata read in a
	// tier that owns none. A COMPOSITE reference names several targets and gets a map from each — the
	// keys carry their own type, so two charts cannot be confused for one another.
	std::map<wxString, std::vector<ibValue>> childrenOf;
	if (source != nullptr && column != nullptr) {
		const ibBackendQueryProvider& provider = source->GetProvider();
		if (const ibBackendQueryable* single = provider.ResolveReferenceTarget(source, column))
			ReadChildrenMap(single, childrenOf);
		else
			for (const ibBackendQueryable* target : provider.ResolveReferenceTargets(source, column))
				ReadChildrenMap(target, childrenOf);
	}

	for (const ibValue& value : named) {
		if (value.IsEmpty())
			continue;

		// Descend from the named value. A node is expanded ONCE: a cycle in a parent link is a corrupt
		// tree rather than a legitimate shape, and a reading is not the place to hang because of one.
		std::vector<ibValue>     subtree;
		std::map<wxString, char> walked;
		subtree.push_back(value);
		walked[value.GetHashKey()] = 1;
		for (size_t i = 0; i < subtree.size(); i++) {
			const wxString key = subtree[i].GetHashKey();   // taken BEFORE the pushes below reallocate
			const auto kids = childrenOf.find(key);
			if (kids == childrenOf.end())
				continue;
			for (const ibValue& child : kids->second) {
				const wxString childKey = child.GetHashKey();
				if (walked[childKey])
					continue;
				walked[childKey] = 1;
				subtree.push_back(child);
			}
		}

		for (size_t i = 0; i < subtree.size(); i++) {
			if (i == 0 && unfold == ibQueryDimUnfold::HierarchyOnly)
				continue;   // «hierarchy only» - the subordinates, not the value that names them
			m_accepted.push_back(subtree[i]);
			m_reportedUnder[subtree[i].GetHashKey()] = value;
		}
	}
}

ibValue ibQueryHierarchyScope::ReportedUnder(const ibValue& value) const
{
	const auto found = m_reportedUnder.find(value.GetHashKey());
	return found != m_reportedUnder.end() ? found->second : value;
}

std::vector<ibValue> ibQueryHierarchyNamedValues(const ibValue& given)
{
	std::vector<ibValue> named;
	if (given.IsEmpty())
		return named;

	// A COLLECTION expands into its elements - an array, a value table, a computed set; the same
	// reading an ordinary IN gives a captured collection. A scalar has no iterator and goes in as it
	// is. (A reference is a scalar here: it vends no iterator, so an account never expands into its
	// own members.)
	ibValue value = given;   // CreateIterator is not const - the query holds its own copy either way
	if (std::shared_ptr<ibValueIteratorState> iterator = value.CreateIterator()) {
		ibValue element;
		while (iterator->MoveNext(element))
			if (!element.IsEmpty())
				named.push_back(element);
		return named;
	}

	named.push_back(given);
	return named;
}
