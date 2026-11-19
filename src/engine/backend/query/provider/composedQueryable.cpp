#include "composedQueryable.h"

#include "backend/query/columnLayout.h"    // ibSqlAliasOf
#include "backend/query/queryProvider.h"   // ibBackendQueryProvider::CanJoinOn
#include "backend/query/provider/database/tempTableManager.h"   // the copies a composite over a base in memory holds

#include <algorithm>

namespace {

// A column whose fields carry its value under a tag — the spread of a declared type. The other kind is one field: a raw
// column, or an output the statement computed.
bool IsTagged(const ibBackendQueryColumn& col)
{
	const std::vector<ibColumnSlot> slots = col.DescribeLayout();
	return std::any_of(slots.begin(), slots.end(), [](const ibColumnSlot& s) { return s.m_role == ibColumnRole::Discriminator; });
}

// `own` with the classes `branches` add to it — null when they add none.
std::shared_ptr<ibTypeDescription> Widened(const ibTypeDescription& own,
	const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& branches, ibTypeDescription& (ibBackendQueryColumn::*typeOf)() const)
{
	std::shared_ptr<ibTypeDescription> type;
	for (const std::shared_ptr<const ibBackendQueryColumn>& branch : branches) {
		const ibTypeDescription& theirs = ((*branch).*typeOf)();
		for (const ibClassID& clsid : theirs.GetClsidList()) {
			if ((type != nullptr ? *type : own).ContainType(clsid))
				continue;
			if (type == nullptr)
				type = std::make_shared<ibTypeDescription>(own);
			type->AppendMetaType(clsid, theirs.GetTypeData());
		}
	}
	return type;
}

} // namespace

// ⭐ A UNION'S OUTPUT IS OF EVERY TYPE ITS BRANCHES GIVE IT. Read through the first branch's column alone, a branch of
// another type lost its value — a string under a number's spread had no field to go into. Where every branch spreads
// its value under a tag, or none does (each one field the statement computed), the twin takes on the types the others
// add, and is laid out and read as a column of them all. A column that declares no type is the widest of them already
// (DescribeLayout), and is left as it is.
ibComposedQueryColumn::ibComposedQueryColumn(const std::shared_ptr<const ibBackendQueryColumn>& origin,
                                             const std::shared_ptr<const ibBackendQueryable>& leaf, const ibString& name, ibMetaID id,
                                             const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& branches)
	: m_origin(origin), m_leaf(leaf), m_name(name), m_id(id)
{
	if (branches.empty() || origin->GetTypeValueDesc().GetClsidCount() == 0)
		return;
	const bool tagged = IsTagged(*origin);
	if (!tagged && origin->GetColumnKind() != Kind::Computed)
		return;
	for (const std::shared_ptr<const ibBackendQueryColumn>& branch : branches)
		if (branch->GetTypeValueDesc().GetClsidCount() == 0 || IsTagged(*branch) != tagged
			|| (!tagged && branch->GetColumnKind() != Kind::Computed))
			return;

	std::shared_ptr<ibTypeDescription> type  = Widened(origin->GetTypeDesc(), branches, &ibBackendQueryColumn::GetTypeDesc);
	std::shared_ptr<ibTypeDescription> value = Widened(origin->GetTypeValueDesc(), branches, &ibBackendQueryColumn::GetTypeValueDesc);
	if (type == nullptr && value == nullptr)
		return;
	m_type      = type  != nullptr ? type  : std::make_shared<ibTypeDescription>(origin->GetTypeDesc());
	m_valueType = value != nullptr ? value : std::make_shared<ibTypeDescription>(origin->GetTypeValueDesc());
}

// The origin's fields, under this name — the same slots in the same order, so a value spreads and reassembles as it
// does on the leaf. A field after the origin's name takes the same suffix after this one; a field named otherwise (a
// column made of the row's other fields — a moment, an order) goes under this name whole. A union's of several types is
// laid out as a column of them all.
std::vector<ibColumnSlot> ibComposedQueryColumn::DescribeLayout() const
{
	if (m_valueType != nullptr)
		return ibBackendQueryColumn::DescribeLayout();
	const ibString            base  = m_origin->GetPhysicalName();
	std::vector<ibColumnSlot> slots = m_origin->DescribeLayout();
	for (ibColumnSlot& slot : slots) {
		ibString suffix;
		slot.m_name = slot.m_name.StartsWith(base, &suffix) ? m_name + suffix : m_name + ibT("_") + slot.m_name;
	}
	return slots;
}

namespace {

void CollectLeaves(const std::shared_ptr<ibQueryNode>& node, std::vector<std::shared_ptr<const ibBackendQueryable>>& out)
{
	if (node == nullptr)
		return;
	switch (node->m_kind) {
	case ibQueryNode::Kind::Source:
		if (node->m_queryable != nullptr)
			out.push_back(node->m_queryable);
		return;
	case ibQueryNode::Kind::Join:
		CollectLeaves(node->m_left, out);
		CollectLeaves(node->m_right, out);
		return;
	case ibQueryNode::Kind::Union:
		for (const std::shared_ptr<ibQueryNode>& part : node->m_parts)
			CollectLeaves(part, out);
		return;
	}
}

// Does `leaf` list `col` — as a column, its sort order or its key. The object is asked for before the source answers
// by name and id, because a register's surfaces republish one column as themselves and only the object tells which
// surface the author named (ColocatedOwner says why, with the measurement).
bool ListsColumn(const std::shared_ptr<const ibBackendQueryable>& leaf, const std::shared_ptr<const ibBackendQueryColumn>& col)
{
	for (const std::shared_ptr<const ibBackendQueryColumn>& c : leaf->GetQueryColumns())
		if (c == col)
			return true;
	if (leaf->GetSortColumn() == col)
		return true;
	for (const std::shared_ptr<const ibBackendQueryColumn>& c : leaf->GetPrimaryKeyColumns())
		if (c == col)
			return true;
	return false;
}

bool DropsRepeats(const std::shared_ptr<ibQueryNode>& root)
{
	if (root == nullptr || root->m_kind != ibQueryNode::Kind::Union)
		return false;
	for (size_t part = 1; part < root->m_parts.size(); ++part)
		if (part >= root->m_partAll.size() || !root->m_partAll[part])
			return true;
	return false;
}

} // namespace

ibComposedQueryable::ibComposedQueryable(const std::shared_ptr<ibQueryNode>& root,
                                         const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& columns,
                                         const std::vector<std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>>& outputs,
                                         const std::shared_ptr<const ibBackendQueryable>& primary, const ibString& name,
                                         std::shared_ptr<ibMemorySpace> space,
                                         std::vector<std::unique_ptr<ibTempTableManager>> copies)
	: m_root(root), m_name(name), m_space(std::move(space)), m_copies(std::move(copies))
{
	CollectLeaves(m_root, m_leaves);
	if (m_leaves.empty())
		return;
	const bool isUnion = m_root->m_kind == ibQueryNode::Kind::Union;

	// The leaf a column is read from: the one that lists the object, else the first that answers for it.
	const auto ownerOf = [this](const std::shared_ptr<const ibBackendQueryColumn>& col) -> size_t {
		for (size_t i = 0; i < m_leaves.size(); ++i)
			if (ListsColumn(m_leaves[i], col))
				return i;
		for (size_t i = 0; i < m_leaves.size(); ++i)
			if (m_leaves[i]->ContainColumn(col))
				return i;
		return m_leaves.size();
	};

	// ⭐ ONE ID PER TWIN. The origin's own while no other twin answers to it — so a result read by a column's id finds
	// it — and an id of its own by position where two leaves publish one id (a register's balance beside its
	// turnovers), as the co-located join numbered such outputs.
	// Its name — an output's alias as the statement spells it, else the origin's under the leaf's number.
	// A union's twin is told the columns of its name the other branches give (the union's relation, dbTableProvider).
	const auto mint = [this, isUnion](const std::shared_ptr<const ibBackendQueryColumn>& origin, size_t leaf, const ibString& alias) {
		ibMetaID id = origin->GetColumnId();
		for (const std::shared_ptr<ibComposedQueryColumn>& t : m_twins)
			if (t->GetColumnId() == id) {
				id = ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Output,
				                                      static_cast<ibMetaID>(m_twins.size()));
				break;
			}
		const ibString name = !alias.IsEmpty() ? ibSqlAliasOf(alias)
			: ibString::Format(ibT("j%u_"), static_cast<unsigned>(leaf)) + origin->GetPhysicalName();
		// ⭐ ONE NAME, ONE TWIN — a name is a field of the statement, and a field given twice is refused (Firebird: "column
		// J0_FLD1202_TYPE was specified multiple times for derived table OES_COMPOSED"; the base in memory: "ambiguous").
		// A leaf's key may be another object for a field its list already gave (a balance's Item beside the Item a query
		// names, 2026-11-19): the field it names is the twin there is.
		for (const std::shared_ptr<ibComposedQueryColumn>& t : m_twins)
			if (t->GetPhysicalName().CmpNoCase(name) == 0)
				return std::shared_ptr<const ibBackendQueryColumn>(t);
		std::vector<std::shared_ptr<const ibBackendQueryColumn>> branches;
		if (isUnion)
			for (size_t b = 1; b < m_leaves.size(); ++b)
				if (std::shared_ptr<const ibBackendQueryColumn> own = m_leaves[b]->ResolveColumnByName(origin->GetName()))
					branches.push_back(std::move(own));
		m_twins.push_back(std::make_shared<ibComposedQueryColumn>(origin, m_leaves[leaf], name, id, branches));
		return std::shared_ptr<const ibBackendQueryColumn>(m_twins.back());
	};

	// ⭐ A UNION'S OUTPUT IS A NAME, every branch supplying its own column of it — so its twin stands for the name, the
	// origin the first branch's column (the type the lowering resolved the output against), and each branch gives its
	// own column's fields of the same suffix (the union's relation, dbTableProvider). What the first branch lacks is no
	// output of a union, in SQL as here.
	const auto originOf = [&](const std::shared_ptr<const ibBackendQueryColumn>& col) -> std::shared_ptr<const ibBackendQueryColumn> {
		if (!isUnion || ListsColumn(m_leaves.front(), col))
			return col;
		return m_leaves.front()->ResolveColumnByName(col->GetName());
	};

	// The outputs first, each under its alias — a column selected twice has a twin under each name, and the first is
	// the one its column reads as (TwinOf) — then every other column the spec names.
	std::vector<ibString> named;
	for (const std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>& out : outputs) {
		if (out.first == nullptr || out.second.IsEmpty())
			continue;
		const size_t leaf = ownerOf(out.first);
		const std::shared_ptr<const ibBackendQueryColumn> origin = leaf < m_leaves.size() ? originOf(out.first) : nullptr;
		const ibString spelled = ibSqlAliasOf(out.second);
		if (origin == nullptr || std::find(named.begin(), named.end(), spelled) != named.end())
			continue;
		named.push_back(spelled);
		mint(origin, isUnion ? 0 : leaf, out.second);
	}
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns) {
		if (col == nullptr)
			continue;
		const size_t leaf = ownerOf(col);
		if (leaf == m_leaves.size())
			continue;
		const std::shared_ptr<const ibBackendQueryColumn> origin = originOf(col);
		if (origin != nullptr && TwinOf(origin) == nullptr)
			mint(origin, isUnion ? 0 : leaf, ibString());
	}

	if (DropsRepeats(m_root))
		return;
	const std::shared_ptr<const ibBackendQueryable>& keyed = primary != nullptr ? primary : m_leaves.front();
	const size_t keyedLeaf = static_cast<size_t>(std::find(m_leaves.begin(), m_leaves.end(), keyed) - m_leaves.begin());
	for (const std::shared_ptr<const ibBackendQueryColumn>& key : keyed->GetPrimaryKeyColumns()) {
		if (key == nullptr)
			continue;
		std::shared_ptr<const ibBackendQueryColumn> twin = TwinOf(key);
		m_keys.push_back(twin != nullptr ? twin : mint(originOf(key), keyedLeaf < m_leaves.size() && !isUnion ? keyedLeaf : 0, ibString()));
	}
}

ibComposedQueryable::~ibComposedQueryable() = default;

std::shared_ptr<const ibBackendQueryColumn> ibComposedQueryable::TwinOf(const std::shared_ptr<const ibBackendQueryColumn>& col) const
{
	if (col == nullptr)
		return nullptr;
	for (const std::shared_ptr<ibComposedQueryColumn>& t : m_twins)
		if (t == col || t->Origin() == col)
			return t;
	// A union's twin is its NAME, whichever branch lists the column — and only a branch that LISTS it: by id, a
	// register's balance answers for its turnovers' columns too (ColocatedOwner, measured).
	if (m_root == nullptr || m_root->m_kind != ibQueryNode::Kind::Union ||
	    std::none_of(m_leaves.begin(), m_leaves.end(), [&](const std::shared_ptr<const ibBackendQueryable>& q) { return ListsColumn(q, col); }))
		return nullptr;
	for (const std::shared_ptr<ibComposedQueryColumn>& t : m_twins)
		if (t->Origin()->GetName().CmpNoCase(col->GetName()) == 0)
			return t;
	return nullptr;
}

bool ibComposedQueryable::ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const
{
	return col != nullptr && std::any_of(m_twins.begin(), m_twins.end(),
		[&](const std::shared_ptr<ibComposedQueryColumn>& t) { return t == col; });
}

ibString ibComposedQueryable::GetPhysicalName(const std::shared_ptr<const ibBackendQueryColumn>& col) const
{
	const std::shared_ptr<const ibBackendQueryColumn> twin = TwinOf(col);
	return (twin != nullptr ? twin : col)->GetPhysicalName();
}

std::vector<std::shared_ptr<const ibBackendQueryColumn>> ibComposedQueryable::GetQueryColumns() const
{
	return { m_twins.begin(), m_twins.end() };
}

// The leaves are of one application; the first that has one reads a reference or an enum back.
const ibMetaData* ibComposedQueryable::GetMetaData() const
{
	for (const std::shared_ptr<const ibBackendQueryable>& leaf : m_leaves)
		if (const ibMetaData* metaData = leaf->GetMetaData())
			return metaData;
	return nullptr;
}

bool ibCanJoinOn(const std::shared_ptr<ibQueryNode>& root, ibDatabaseConnectionHolder* holder)
{
	std::vector<std::shared_ptr<const ibBackendQueryable>> leaves;
	CollectLeaves(root, leaves);
	return !leaves.empty() && std::all_of(leaves.begin(), leaves.end(),
		[holder](const std::shared_ptr<const ibBackendQueryable>& leaf) { return leaf->GetProvider().CanJoinOn(holder); });
}
