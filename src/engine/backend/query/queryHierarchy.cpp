////////////////////////////////////////////////////////////////////////////
//	Description : «IN HIERARCHY» - resolving what a named value stands for
////////////////////////////////////////////////////////////////////////////

#include "queryHierarchy.h"

#include "dataQueryBuilder.h"   // L3 door - the one read the whole walk needs
#include "queryProvider.h"      // ibBackendQueryProvider - ResolveReferenceTarget, the metadata owner
#include "dbTableProvider.h"    // ibDbTableProvider::ReferenceTargetsOf - the same owner, asked by a type
#include "queryException.h"     // ibBackendQueryException - a column that names no catalog, refused in words

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
void ReadChildrenMap(const ibBackendQueryable* target, std::unordered_map<ibValue, std::vector<ibValue>, ibValueHash, ibValueEqual>& childrenOf)
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
		childrenOf[rows.GetValue(parent)].push_back(self);
	}
}

// ⭐ THE COLUMN SAYS WHERE THE SUBTREE LIVES, AND THE PROVIDER RESOLVES IT. Not the value: a value
// would have to be cast to a reference and asked for its metaobject, which is metadata read in a
// tier that owns none. A COMPOSITE reference names several targets and gets a map from each — the
// keys carry their own type, so two charts cannot be confused for one another.
std::vector<const ibBackendQueryable*> TargetsOfColumn(const ibBackendQueryable* source, const ibBackendQueryColumn* column,
                                                       const std::vector<ibValue>& named, ibQueryDimUnfold unfold)
{
	std::vector<const ibBackendQueryable*> targets;
	if (unfold == ibQueryDimUnfold::Elements || source == nullptr || column == nullptr)
		return targets;
	const ibBackendQueryProvider& provider = source->GetProvider();
	if (const ibBackendQueryable* single = provider.ResolveReferenceTarget(source, column))
		targets.push_back(single);
	else
		targets = provider.ResolveReferenceTargets(source, column);

	// ⚠ A COLUMN THAT NAMES NO CATALOG CANNOT BE WALKED — a value table's column declared with no type, a
	// source that answers no configuration. Standing for the named values alone, it found not one row under
	// a group and said nothing (2026-09-29); refused instead, with the way out: the column is given a type.
	if (targets.empty())
		for (const ibValue& value : named)
			if (!value.IsEmpty())
				ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure, wxString::Format(
					_("IN HIERARCHY over '%s', which names no catalog - give the column a type: declare it on the table, "
					  "or select CAST(%s AS Catalog.<Name>)"),
					column->GetName(), column->GetName()));
	return targets;
}

} // namespace

ibQueryHierarchyScope::ibQueryHierarchyScope(const ibBackendQueryable* source, const ibBackendQueryColumn* column,
                                             const std::vector<ibValue>& named, ibQueryDimUnfold unfold)
	: ibQueryHierarchyScope(TargetsOfColumn(source, column, named, unfold), named, unfold)
{
}

ibQueryHierarchyScope::ibQueryHierarchyScope(const ibMetaData* metaData, const ibTypeDescription& type,
                                             const std::vector<ibValue>& named, ibQueryDimUnfold unfold)
	: ibQueryHierarchyScope(unfold == ibQueryDimUnfold::Elements ? std::vector<const ibBackendQueryable*>()
	                        : ibDbTableProvider::ReferenceTargetsOf(metaData, type), named, unfold)
{
}

ibQueryHierarchyScope::ibQueryHierarchyScope(const std::vector<const ibBackendQueryable*>& targets,
                                             const std::vector<ibValue>& named, ibQueryDimUnfold unfold)
	: m_unfold(unfold)
{
	// «in» asks the database nothing: the values passed ARE the answer, and nothing is implied about
	// what stands under them.
	if (unfold == ibQueryDimUnfold::Elements) {
		for (const ibValue& value : named)
			if (!value.IsEmpty())
				m_accepted.push_back(value);
		return;
	}

	std::unordered_map<ibValue, std::vector<ibValue>, ibValueHash, ibValueEqual> childrenOf;
	for (const ibBackendQueryable* target : targets)
		ReadChildrenMap(target, childrenOf);

	for (const ibValue& value : named) {
		if (value.IsEmpty())
			continue;

		// Descend from the named value. A node is expanded ONCE: a cycle in a parent link is a corrupt
		// tree rather than a legitimate shape, and a reading is not the place to hang because of one.
		std::vector<ibValue>     subtree;
		std::unordered_map<ibValue, char, ibValueHash, ibValueEqual> walked;
		subtree.push_back(value);
		walked[value] = 1;
		for (size_t i = 0; i < subtree.size(); i++) {
			const ibValue key = subtree[i];   // taken BEFORE the pushes below reallocate
			const auto kids = childrenOf.find(key);
			if (kids == childrenOf.end())
				continue;
			for (const ibValue& child : kids->second) {
				const ibValue& childKey = child;
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
			m_reportedUnder[subtree[i]] = value;
		}
	}
}

bool ibQueryHierarchyScope::Admits(const ibValue& value) const
{
	// A walked subtree keys every value it admits (m_reportedUnder); the named values of «in» are the list itself.
	if (m_unfold != ibQueryDimUnfold::Elements)
		return m_reportedUnder.find(value) != m_reportedUnder.end();
	for (const ibValue& accepted : m_accepted)
		if (accepted == value)
			return true;
	return false;
}

ibValue ibQueryHierarchyScope::ReportedUnder(const ibValue& value) const
{
	const auto found = m_reportedUnder.find(value);
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
