#ifndef __COMPOSED_QUERYABLE_H__
#define __COMPOSED_QUERYABLE_H__

// ==========================================================================
// ibComposedQueryable / ibComposedQueryColumn — A JOIN OR A UNION, READ AS ONE SOURCE.
//
// ⭐⭐ A COMPOSITE IS A SOURCE (query-one-road.md §7, decision 7). The translation held three narrow copies for a
// composite — a co-located join, aggregate and union, each with a gate of its own and each knowing less than the
// single source's read: no computed output beyond arithmetic, no anchored page, no reference output of a union. A
// source that is not a table was already a thing the single read takes whole — a register's balance answers
// `GetSourceRelation` with a derived table, and everything around it (WHERE, ORDER BY, paging, folds, walks) stays the
// single source's. A composite answers the same way: its relation is the join or the union of its leaves, and the
// single source's translation runs over it. One road, where there were four.
//
// BUILT LIKE ibAliasQueryable, a second reading of one table: TWIN columns, one per column of a leaf the query names,
// each delegating everything about the data to the leaf's own column. What a twin does not share is its NAME, and with
// it its fields: two leaves can carry the same field names (a table read twice, a register's balance beside its
// turnovers), so a twin's fields are its origin's with the twin's name in place of the origin's — `j0_fld1043_RRRef` —
// which is what the relation projects them as. A column's codec reads `<name><suffix>` for whatever name it is handed,
// so a leaf's column reads itself off the composite under its twin's name — which the composite answers, asked for the
// leaf's column's (GetPhysicalName, TwinOf).
//
// ⭐ AN OUTPUT OF THE SELECT LIST IS CALLED WHAT THE STATEMENT CALLS IT — `ibSqlAliasOf(alias)`, the name every reader of
// an output already asks a cursor for: a raw one by its alias, an object one reassembled under the alias as a prefix,
// and either by its column. So a composite's result answers its outputs the three ways a reader holds them, with
// nothing to translate; a column selected under two names has a twin under each.
//
// ⚠ ITS RELATION IS THE PROVIDER'S. What a leaf is read from, how a key is compared, which fields a column spreads
// into are the kitchen's (ibDbTableProvider), so `GetSourceRelation` is defined in dbTableProvider.cpp beside the FROM
// it builds; this file holds the twins.
//
// A condition on a LEFT JOIN's right leaf stands in the WHERE over the composite, after the join, by construction —
// what SQL means by it (decision 3).
// ==========================================================================

#include "backend/query/queryable.h"
#include "backend/query/dataQueryBuilder.h"   // ibQueryNode

#include <memory>
#include <vector>

class ibComposedQueryable;
class ibMemorySpace;
class ibTempTableManager;

class BACKEND_API ibComposedQueryColumn : public ibBackendQueryColumn
{
public:
	// `origin` — a leaf's column; `leaf` — the leaf it is read from (a union's twin: the first branch); `name` — what
	// it is called in the composite, its fields after it; `id` — the origin's, unless another twin already answers to it;
	// `branches` — a union's twin: the columns of its name in the other branches, whose types it takes on beside the
	// origin's.
	ibComposedQueryColumn(const std::shared_ptr<const ibBackendQueryColumn>& origin,
	                      const std::shared_ptr<const ibBackendQueryable>& leaf, const ibString& name, ibMetaID id,
	                      const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& branches = {});

	const std::shared_ptr<const ibBackendQueryColumn>& Origin() const { return m_origin; }
	const std::shared_ptr<const ibBackendQueryable>&   Leaf()   const { return m_leaf; }

	// IDENTITY and FIELDS — this column's own. Everything else is the origin's, but for a union's TYPE.
	ibMetaID GetColumnId()     const override { return m_id; }
	ibString GetPhysicalName() const override { return m_name; }
	std::vector<ibColumnSlot> DescribeLayout() const override;

	ibString           GetName()          const override { return m_origin->GetName(); }
	ibString           GetSynonym()       const override { return m_origin->GetSynonym(); }
	ibString           GetComment()       const override { return m_origin->GetComment(); }
	ibTypeDescription& GetTypeDesc()      const override { return m_type != nullptr ? *m_type : m_origin->GetTypeDesc(); }
	ibTypeDescription& GetTypeValueDesc() const override { return m_valueType != nullptr ? *m_valueType : m_origin->GetTypeValueDesc(); }
	bool               IsAllowed()        const override { return m_origin->IsAllowed(); }
	bool               IsAvailable()      const override { return m_origin->IsAvailable(); }
	ibPictureDescription GetColumnPicture() const override { return m_origin->GetColumnPicture(); }
	Kind               GetColumnKind()    const override { return m_origin->GetColumnKind(); }
	ibBalanceRole      GetBalanceRole()   const override { return m_origin->GetBalanceRole(); }
	int                GetPeriodRank()    const override { return m_origin->GetPeriodRank(); }
	const ibBackendColumnRawDB* AsRawColumn() const override { return m_origin->AsRawColumn(); }

	// …read under the name it is asked by — the twin's, wherever the composite projected it; a union's of several
	// types as a column of them all.
	bool ReadValue(const ibString& fieldName, const class ibMetaData* metaData,
	               class ibValue& retValue, class ibQueryResult& result, bool createData = false) const override {
		if (m_type != nullptr)
			return ibBackendQueryColumn::ReadValue(fieldName, metaData, retValue, result, createData);
		return m_origin->ReadValue(fieldName, metaData, retValue, result, createData);
	}
	void BindValue(class ibQueryStatement& statement, const class ibMetaData* metaData,
	               const class ibValue& value, int& position) const override {
		if (m_type != nullptr)
			ibBackendQueryColumn::BindValue(statement, metaData, value, position);
		else
			m_origin->BindValue(statement, metaData, value, position);
	}
	// …and its value the same way: a union's of the types of them all, any other as the origin makes it.
	ibValue CreateValue() const override { return m_valueType != nullptr ? ibBackendQueryColumn::CreateValue() : m_origin->CreateValue(); }
	ibValue AdjustValue() const override { return m_valueType != nullptr ? ibBackendQueryColumn::AdjustValue() : m_origin->AdjustValue(); }
	ibValue AdjustValue(const ibValue& varValue) const override {
		return m_valueType != nullptr ? ibBackendQueryColumn::AdjustValue(varValue) : m_origin->AdjustValue(varValue);
	}
	const ibMetaData* GetMetaData() const override { return m_origin->GetMetaData(); }

private:
	std::shared_ptr<const ibBackendQueryColumn> m_origin;
	std::shared_ptr<const ibBackendQueryable>   m_leaf;
	ibString                                    m_name;
	ibMetaID                                    m_id;
	// What a union's output may hold when its branches give it more than the origin's type — null otherwise.
	std::shared_ptr<ibTypeDescription>          m_type;
	std::shared_ptr<ibTypeDescription>          m_valueType;
};

class BACKEND_API ibComposedQueryable : public ibBackendQueryable
{
public:
	// The tree `root` composes — a join or a union of sources — carrying a twin for each of `columns` a leaf has. The
	// composer hands every column its spec names (a column no leaf has — a walk's target, a policy's inner source — is
	// not the composite's, and stays as it is), `outputs` — the select list, whose twins are called by their aliases —
	// `primary` — the source the spec reads from, whose key is the composite's — and `name`, what the statement calls
	// it (a composite inside another needs a name of its own, and has no outputs: only the one above reads it).
	//
	// ⭐ A COMPOSITE OVER COPIES HOLDS THEM — `space`, the space of the base in memory its leaves were copied into, and
	// `copies`, the copies themselves (ibQueryComposer::OverMemorySpace): the result read through it holds it, so the rows
	// stay while the result is read; the copies go first, then the space with whatever is left in it.
	ibComposedQueryable(const std::shared_ptr<ibQueryNode>& root,
	                    const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& columns,
	                    const std::vector<std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>>& outputs,
	                    const std::shared_ptr<const ibBackendQueryable>& primary, const ibString& name,
	                    std::shared_ptr<ibMemorySpace> space = nullptr,
	                    std::vector<std::unique_ptr<ibTempTableManager>> copies = {});
	~ibComposedQueryable() override;
	ibComposedQueryable(const ibComposedQueryable&) = delete; ibComposedQueryable& operator=(const ibComposedQueryable&) = delete;

	const std::shared_ptr<ibQueryNode>&                   Root()   const { return m_root; }
	// The leaves in the order the tree holds them — a twin's prefix is its leaf's place here.
	const std::vector<std::shared_ptr<const ibBackendQueryable>>& Leaves() const { return m_leaves; }
	const std::vector<std::shared_ptr<ibComposedQueryColumn>>& Twins()  const { return m_twins; }

	// ⭐ THE TWIN STANDING FOR A LEAF'S COLUMN — the inverse of ibAliasQueryable::OriginOf. By the exact object, never
	// by id: two surfaces of one register publish one id, each as its own object, and only the object tells which the
	// author named (ColocatedOwner). A union's by the name, for a column one of its branches lists. A twin is its own;
	// null when it was not composed.
	std::shared_ptr<const ibBackendQueryColumn> TwinOf(const std::shared_ptr<const ibBackendQueryColumn>& col) const;

	virtual bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override;
	// A leaf's column under its twin's name — the relation projects the leaf's fields there (TwinOf).
	virtual ibString GetPhysicalName(const std::shared_ptr<const ibBackendQueryColumn>& col) const override;
	virtual std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override;
	// ⭐ THE PRIMARY SOURCE'S KEY, twinned — the tail a page is ordered and anchored by is the one the caller stamped its
	// anchor with (ibDataQueryBuilder::EffectiveSort over the spec it built), so the two cannot differ in length. None
	// for a union that drops repeats: a key carried beside its outputs would keep every row.
	virtual std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetPrimaryKeyColumns() const override { return m_keys; }

	virtual ibString      GetQueryTableName() const override { return m_name; }
	virtual ibQueryRelPtr GetSourceRelation(const ibString& alias) const override;   // dbTableProvider.cpp
	virtual const ibMetaData* GetMetaData() const override;

private:
	std::shared_ptr<ibQueryNode>                             m_root;
	ibString                                                 m_name;
	std::vector<std::shared_ptr<const ibBackendQueryable>>   m_leaves;
	std::vector<std::shared_ptr<ibComposedQueryColumn>>      m_twins;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_keys;
	std::shared_ptr<ibMemorySpace>                           m_space;    // before the copies: let go after them
	std::vector<std::unique_ptr<ibTempTableManager>>         m_copies;
};

// ⭐ DOES ONE STATEMENT ON `holder` READ THE WHOLE TREE — every leaf's provider joins on it (CanJoinOn): none computed in
// memory, none another connection's. The tree's answer to the question its sources answer, asked by whoever writes a
// composite into a statement of that base — the composer's read and a declared query's body alike.
BACKEND_API bool ibCanJoinOn(const std::shared_ptr<ibQueryNode>& root, ibDatabaseConnectionHolder* holder);

#endif // __COMPOSED_QUERYABLE_H__
