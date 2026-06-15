#include "databaseQueryBuilder.h"

#include "databaseLayerException.h"   // ibBackendQueryException

#include "backend/databaseLayer/preparedStatement.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/resultSetMetaData.h"

// --------------------------------------------------------------------------
// Parameter binding: ibValue -> ibPreparedStatement::SetParam* by type.
// Values travel through bind calls, never through the SQL text — so injection
// is impossible regardless of the value's content.
// --------------------------------------------------------------------------
static void ibBindParam(ibPreparedStatement* stmt, int pos, const ibValue& v)
{
	switch (v.GetType()) {
	case TYPE_BOOLEAN: stmt->SetParamBool(pos, v.GetBoolean());   break;
	case TYPE_NUMBER:  stmt->SetParamNumber(pos, v.GetNumber());  break;
	case TYPE_DATE:    stmt->SetParamDate(pos, v.GetDateTime());  break;
	case TYPE_STRING:  stmt->SetParamString(pos, v.GetString());  break;
	case TYPE_NULL:
	case TYPE_EMPTY:
	default:           stmt->SetParamNull(pos);                   break;
	}
}

// Bind a whole render plan in placeholder order (1-based). Inline Const values
// bind directly; Param entries pull from the caller-supplied vector.
static void ibBindPlan(ibPreparedStatement* stmt,
                       const std::vector<ibQueryParam>& plan,
                       const std::vector<ibValue>& external)
{
	int pos = 1;
	for (const ibQueryParam& p : plan) {
		if (p.m_external) {
			const bool inRange = p.m_externalIndex >= 0
			                  && p.m_externalIndex < static_cast<int>(external.size());
			ibBindParam(stmt, pos, inRange ? external[p.m_externalIndex] : wxEmptyValue);
		} else if (p.m_isBlob) {
			// Opaque bytes (a reference / binary key encoded by the metadata
			// layer). L2 binds them without interpreting — stays metadata-blind.
			stmt->SetParamBlob(pos, p.m_blob.GetData(), static_cast<long>(p.m_blob.GetDataLen()));
		} else {
			ibBindParam(stmt, pos, p.m_value);
		}
		++pos;
	}
}

// Run a rendered SELECT and wrap the cursor. Shared by Execute() and ExecuteIR().
static ibQueryResult ibRunRendered(const std::shared_ptr<ibDatabaseLayer>& conn,
                                   const ibRenderedQuery& rendered,
                                   const std::vector<ibValue>& externalParams)
{
	ibPreparedStatement* stmt = conn->PrepareStatement(rendered.m_sql);
	if (stmt == nullptr)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
			wxString::Format(_("Query layer failed to prepare statement: %s"), rendered.m_sql));

	ibBindPlan(stmt, rendered.m_params, externalParams);

	ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
	return ibQueryResult(conn, stmt, rs);
}

// ==========================================================================
// ibDatabaseQueryBuilder — construction
// ==========================================================================

ibDatabaseQueryBuilder::ibDatabaseQueryBuilder() : m_scope() {}

ibDatabaseQueryBuilder::ibDatabaseQueryBuilder(ibDatabaseConnectionHolder* holder) : m_scope(holder) {}

// ==========================================================================
// Fluent DQL construction
// ==========================================================================

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::From(const wxString& table)
{
	m_table = table;
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::From(ibQueryRelPtr source)
{
	m_source = std::move(source);
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::Join(const wxString& table,
	ibQueryExprPtr on, ibQueryJoinType type)
{
	return Join(ibScan(table), std::move(on), type);
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::Join(ibQueryRelPtr right,
	ibQueryExprPtr on, ibQueryJoinType type)
{
	ibQueryRelPtr left = m_source ? m_source : ibScan(m_table);
	m_source = ibJoin(std::move(left), std::move(right), std::move(on), type);
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::Select(std::vector<wxString> columns)
{
	m_select = std::move(columns);
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::Where(ibQueryExprPtr predicate)
{
	if (predicate) m_predicates.push_back(std::move(predicate));
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::OrderBy(const wxString& column, ibQuerySortDir dir)
{
	ibQuerySortKey key;
	key.m_expr = ibCol(column);
	key.m_dir  = dir;
	m_sortKeys.push_back(std::move(key));
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::AddSortKey(ibQuerySortKey key)
{
	m_sortKeys.push_back(std::move(key));
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::Limit(long count, long offset)
{
	m_hasLimit    = true;
	m_limitCount  = count;
	m_limitOffset = offset;
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::Project(std::vector<ibQueryProjItem> items)
{
	m_projection = std::move(items);
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::GroupBy(ibQueryExprPtr key)
{
	if (key) m_groupKeys.push_back(std::move(key));
	return *this;
}

ibDatabaseQueryBuilder& ibDatabaseQueryBuilder::Having(ibQueryExprPtr predicate)
{
	m_having = std::move(predicate);
	return *this;
}

// ==========================================================================
// Build() — fluent state -> ibQueryIR. Pure; no connection, no dialect.
// Wrap order Scan -> Filter -> Project -> Sort -> Limit; the renderer flattens
// the linear chain, so the predicates fold to a single AND-chain WHERE.
// ==========================================================================

ibQueryIR ibDatabaseQueryBuilder::Build() const
{
	ibQueryRelPtr rel = m_source ? m_source : ibScan(m_table);

	if (!m_predicates.empty()) {
		ibQueryExprPtr pred = m_predicates.front();
		for (size_t i = 1; i < m_predicates.size(); ++i)
			pred = ibBinOp(ibQueryBinOp::And, pred, m_predicates[i]);
		rel = ibFilter(rel, pred);
	}

	// GROUP BY -> Aggregate (explicit projection is the SELECT list); else an explicit
	// Project() projection (dot-walk: main.* + aliased leaf columns); else a plain
	// column projection from Select(columns).
	if (!m_groupKeys.empty()) {
		rel = ibAggregate(rel, m_projection, m_groupKeys, m_having);
	}
	else if (!m_projection.empty()) {
		rel = ibProject(rel, m_projection);
	}
	else if (!m_select.empty()) {
		std::vector<ibQueryProjItem> projection;
		projection.reserve(m_select.size());
		for (const wxString& col : m_select)
			projection.push_back(ibQueryProjItem{ ibCol(col), wxString() });
		rel = ibProject(rel, std::move(projection));
	}

	if (!m_sortKeys.empty())
		rel = ibSort(rel, m_sortKeys);

	if (m_hasLimit)
		rel = ibLimit(rel, m_limitCount, m_limitOffset);

	return ibQueryIR(rel);
}

// ==========================================================================
// Terminals + direct paths
// ==========================================================================

ibQueryResult ibDatabaseQueryBuilder::Execute(const std::vector<ibValue>& externalParams)
{
	return ExecuteIR(Build(), externalParams);
}

ibQueryResult ibDatabaseQueryBuilder::ExecuteIR(const ibQueryIR& ir, const std::vector<ibValue>& externalParams)
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	if (!conn)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::NoConnection,
			_("Query layer could not obtain a database connection from the holder."));

	const ibDialectDictionary& dialect = conn->GetDialect();

	ibQueryRenderer renderer(dialect);
	const ibRenderedQuery rendered = renderer.Render(ir);

	// The generated SQL contains only "?"/"$n" placeholders + identifiers +
	// keywords — no user data, no '%'. Safe through the vararg PrepareStatement.
	return ibRunRendered(conn, rendered, externalParams);
}

ibRenderedQuery ibDatabaseQueryBuilder::Render(const ibQueryIR& ir)
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	if (!conn)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::NoConnection,
			_("Query layer could not obtain a database connection from the holder."));

	ibQueryRenderer renderer(conn->GetDialect());
	return renderer.Render(ir);
}

ibQueryResult ibDatabaseQueryBuilder::ExecuteRendered(const ibRenderedQuery& rendered,
                                                      const std::vector<ibValue>& externalParams)
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	if (!conn)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::NoConnection,
			_("Query layer could not obtain a database connection from the holder."));

	return ibRunRendered(conn, rendered, externalParams);
}

bool ibDatabaseQueryBuilder::TableExists(const wxString& table)
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	return conn ? conn->TableExists(table) : false;
}

wxArrayString ibDatabaseQueryBuilder::GetColumns(const wxString& table)
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	return conn ? conn->GetColumns(table) : wxArrayString();
}

bool ibDatabaseQueryBuilder::IsOpen()
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	return conn ? conn->IsOpen() : false;
}

bool ibDatabaseQueryBuilder::IsActiveTransaction()
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	return conn ? conn->IsActiveTransaction() : false;
}

int ibDatabaseQueryBuilder::Execute(const ibDdlStatement& ddl)
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	if (!conn)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::NoConnection,
			_("Query layer could not obtain a database connection from the holder."));

	const ibDialectDictionary& dialect = conn->GetDialect();

	ibQueryRenderer renderer(dialect);
	const wxString sql = renderer.RenderDDL(ddl);

	// DDL text is generated by us (no user data, no '%') — safe through the
	// vararg RunQuery.
	return conn->RunQuery(sql);
}

int ibDatabaseQueryBuilder::Execute(const ibDmlStatement& dml, const std::vector<ibValue>& externalParams)
{
	std::shared_ptr<ibDatabaseLayer> conn = m_scope.shared();
	if (!conn)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::NoConnection,
			_("Query layer could not obtain a database connection from the holder."));

	const ibDialectDictionary& dialect = conn->GetDialect();

	ibQueryRenderer renderer(dialect);
	const ibRenderedQuery rendered = renderer.RenderDML(dml);

	ibPreparedStatement* stmt = conn->PrepareStatement(rendered.m_sql);
	if (stmt == nullptr)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
			wxString::Format(_("Query layer failed to prepare statement: %s"), rendered.m_sql));

	ibStatementGuard guard(conn, stmt);   // RAII close — no result set to wrap
	ibBindPlan(stmt, rendered.m_params, externalParams);
	return stmt->RunQuery();
}

// ==========================================================================
// ibQueryResult
// ==========================================================================

ibQueryResult::ibQueryResult(std::shared_ptr<ibDatabaseLayer> conn,
                             ibPreparedStatement* stmt,
                             ibDatabaseResultSet* rs)
	: m_conn(std::move(conn)), m_stmt(stmt), m_rs(rs)
{
}

ibQueryResult::~ibQueryResult()
{
	Release();
}

ibQueryResult::ibQueryResult(ibQueryResult&& other) noexcept
	: m_conn(std::move(other.m_conn))
	, m_stmt(other.m_stmt)
	, m_rs(other.m_rs)
	, m_meta(other.m_meta)
{
	other.m_stmt = nullptr;
	other.m_rs   = nullptr;
	other.m_meta = nullptr;
}

ibQueryResult& ibQueryResult::operator=(ibQueryResult&& other) noexcept
{
	if (this != &other) {
		Release();
		m_conn = std::move(other.m_conn);
		m_stmt = other.m_stmt;
		m_rs   = other.m_rs;
		m_meta = other.m_meta;
		other.m_stmt = nullptr;
		other.m_rs   = nullptr;
		other.m_meta = nullptr;
	}
	return *this;
}

void ibQueryResult::Release()
{
	// Order: result set first (it may reference the statement), then the
	// statement. Both through the owning connection.
	if (m_rs != nullptr && m_conn) {
		m_conn->CloseResultSet(m_rs);
		m_rs = nullptr;
	}
	if (m_stmt != nullptr && m_conn) {
		m_conn->CloseStatement(m_stmt);
		m_stmt = nullptr;
	}
	m_meta = nullptr;  // owned by m_rs
}

ibResultSetMetaData* ibQueryResult::Meta()
{
	if (m_meta == nullptr && m_rs != nullptr)
		m_meta = m_rs->GetMetaData();
	return m_meta;
}

bool ibQueryResult::Next()
{
	return m_rs != nullptr ? m_rs->Next() : false;
}

int ibQueryResult::ColumnCount()
{
	ibResultSetMetaData* md = Meta();
	return md != nullptr ? md->GetColumnCount() : 0;
}

wxString ibQueryResult::ColumnName(int column)
{
	ibResultSetMetaData* md = Meta();
	return md != nullptr ? md->GetColumnName(column) : wxString();
}

ibValue ibQueryResult::GetValue(int column)
{
	if (m_rs == nullptr)
		return ibValue();

	if (m_rs->IsFieldNull(column))
		return ibValue(TYPE_NULL);

	ibResultSetMetaData* md = Meta();
	const int type = md != nullptr ? md->GetColumnType(column)
	                               : static_cast<int>(ibResultSetMetaData::COLUMN_STRING);

	switch (type) {
	case ibResultSetMetaData::COLUMN_INTEGER:
	case ibResultSetMetaData::COLUMN_DOUBLE:
		return ibValue(m_rs->GetResultNumber(column));
	case ibResultSetMetaData::COLUMN_BOOL:
		return ibValue(m_rs->GetResultBool(column));
	case ibResultSetMetaData::COLUMN_DATE:
		return ibValue(m_rs->GetResultDate(column));
	case ibResultSetMetaData::COLUMN_NULL:
		return ibValue(TYPE_NULL);
	case ibResultSetMetaData::COLUMN_STRING:
	case ibResultSetMetaData::COLUMN_BLOB:   // BLOB not normalized in MVP — string fallback
	default:
		return ibValue(m_rs->GetResultString(column));
	}
}

ibValue ibQueryResult::GetValue(const wxString& name)
{
	ibResultSetMetaData* md = Meta();
	const int col = md != nullptr ? md->FindColumnByName(name) : -1;
	if (col < 0)
		return ibValue();
	return GetValue(col);  // NB: assumes metadata column ids are 1-based (matches GetResult*)
}

// Typed field reads by name — delegate to the borrowed driver cursor (the dialect-normalised
// physical field). The provider's value-assembly reads through these, never the raw L1 cursor.
wxString   ibQueryResult::GetResultString(const wxString& name)                  { return m_rs != nullptr ? m_rs->GetResultString(name)        : wxString(); }
int        ibQueryResult::GetResultInt(const wxString& name)                     { return m_rs != nullptr ? m_rs->GetResultInt(name)           : 0; }
long long  ibQueryResult::GetResultLong(const wxString& name)                    { return m_rs != nullptr ? m_rs->GetResultLong(name)          : 0; }
bool       ibQueryResult::GetResultBool(const wxString& name)                    { return m_rs != nullptr ? m_rs->GetResultBool(name)          : false; }
wxDateTime ibQueryResult::GetResultDate(const wxString& name)                    { return m_rs != nullptr ? m_rs->GetResultDate(name)          : wxDateTime(); }
double     ibQueryResult::GetResultDouble(const wxString& name)                  { return m_rs != nullptr ? m_rs->GetResultDouble(name)        : 0.0; }
ibNumber   ibQueryResult::GetResultNumber(const wxString& name)                  { return m_rs != nullptr ? m_rs->GetResultNumber(name)        : ibNumber(); }
void*      ibQueryResult::GetResultBlob(const wxString& name, wxMemoryBuffer& b) { return m_rs != nullptr ? m_rs->GetResultBlob(name, b)       : nullptr; }

// ==========================================================================
// ibQueryRenderer (merged from queryRenderer.cpp)
// ==========================================================================
ibRenderedQuery ibQueryRenderer::Render(const ibQueryIR& ir)
{
	m_out = ibRenderedQuery{};
	m_paramPos = 0;
	m_out.m_sql = RenderSelect(ir.m_root.get());
	// Pessimistic row lock — appended to the TOP-level SELECT only (subqueries render through
	// RenderSelect above and must NOT carry it). The dialect owns the clause + its emptiness.
	if (ir.m_lockForUpdate && !m_dialect.m_rowLockSuffix.empty()) {
		m_out.m_sql += m_dialect.m_rowLockSuffix;
		if (ir.m_lockNoWait)
			m_out.m_sql += m_dialect.m_rowLockNoWaitSuffix;   // " NOWAIT" (PG/MySQL); empty on FB/SQLite
	}
	return m_out;
}

// Flatten a relation chain (Filter/Project/Sort/Limit/Aggregate over a Scan/Join/
// Subquery source) into ONE SELECT, appending its bind params to m_out in placeholder
// order. Recursive: a Subquery FROM source renders its own SELECT back through here.
wxString ibQueryRenderer::RenderSelect(const ibQueryRel* root)
{
	// Set operations are a SELECT-level combinator, not a linear chain — render the
	// two members and splice (left before right keeps the bind plan in order). For an
	// ORDER BY / LIMIT over a union, wrap the union in ibSubquery (SQL needs it too).
	if (root && (root->m_kind == ibQueryRelKind::Union || root->m_kind == ibQueryRelKind::UnionAll)) {
		const wxString l = RenderSelect(root->m_input.get());
		const wxString r = RenderSelect(root->m_right.get());
		return l + (root->m_kind == ibQueryRelKind::UnionAll ? wxT(" UNION ALL ") : wxT(" UNION ")) + r;
	}

	// --- flatten the linear chain ----------------------------------------
	const ibQueryRel*            source = nullptr;   // FROM source: Scan / Join / Subquery
	std::vector<ibQueryExprPtr>  predicates;
	std::vector<ibQueryProjItem> projection;
	std::vector<ibQueryExprPtr>  groupKeys;
	ibQueryExprPtr               having;
	std::vector<ibQuerySortKey>  sortKeys;
	bool hasLimit = false, distinct = false, rollup = false;
	long limitCount = -1, limitOffset = 0;

	const ibQueryRel* node = root;
	while (node != nullptr) {
		switch (node->m_kind) {
		case ibQueryRelKind::Scan:
		case ibQueryRelKind::Join:
		case ibQueryRelKind::Subquery:
			source = node;        // FROM source — RenderSource walks it (recursively for joins / subqueries)
			node   = nullptr;
			break;
		case ibQueryRelKind::Filter:
			if (node->m_predicate) predicates.push_back(node->m_predicate);
			node = node->m_input.get();
			break;
		case ibQueryRelKind::Aggregate:
			if (projection.empty()) projection = node->m_projection;
			if (groupKeys.empty())  groupKeys  = node->m_groupKeys;
			if (!having)            having     = node->m_having;
			rollup = rollup || node->m_rollup;
			node = node->m_input.get();
			break;
		case ibQueryRelKind::Project:
			if (projection.empty()) projection = node->m_projection;
			node = node->m_input.get();
			break;
		case ibQueryRelKind::Sort:
			if (sortKeys.empty()) sortKeys = node->m_sortKeys;
			node = node->m_input.get();
			break;
		case ibQueryRelKind::Limit:
			if (!hasLimit) { hasLimit = true; limitCount = node->m_limitCount; limitOffset = node->m_limitOffset; }
			node = node->m_input.get();
			break;
		case ibQueryRelKind::Distinct:
			distinct = true;
			node = node->m_input.get();
			break;
		default:
			node = node->m_input.get();
			break;
		}
	}

	// --- assemble ---------------------------------------------------------
	// FIRST/SKIP (Firebird) and TOP (MSSQL legacy) lead the SELECT; LIMIT/
	// OFFSET and OFFSET..FETCH trail the statement.
	const bool leadingPagination =
		hasLimit && (m_dialect.m_pagination == ibPagination::FirstSkip
		          || m_dialect.m_pagination == ibPagination::Top);

	wxString sql = wxT("SELECT ");

	if (leadingPagination) {
		if (m_dialect.m_pagination == ibPagination::FirstSkip) {
			if (limitCount >= 0) sql += wxString::Format(wxT("FIRST %ld "), limitCount);
			if (limitOffset > 0) sql += wxString::Format(wxT("SKIP %ld "), limitOffset);
		} else { // Top
			if (limitCount >= 0) sql += wxString::Format(wxT("TOP %ld "), limitCount);
		}
	}

	if (distinct) sql += wxT("DISTINCT ");

	if (projection.empty()) {
		sql += wxT("*");
	} else {
		for (size_t i = 0; i < projection.size(); ++i) {
			if (i) sql += wxT(", ");
			sql += RenderExpr(projection[i].m_expr);
			if (!projection[i].m_alias.empty())
				sql += wxT(" AS ") + QuoteIdent(projection[i].m_alias);
		}
	}

	sql += wxT(" FROM ") + RenderSource(source);

	if (!predicates.empty()) {
		sql += wxT(" WHERE ");
		for (size_t i = 0; i < predicates.size(); ++i) {
			if (i) sql += wxT(" AND ");
			sql += RenderExpr(predicates[i]);
		}
	}

	// GROUP BY + HAVING (post-aggregation) — between WHERE and ORDER BY, so the bind
	// plan stays in SQL-text order (HAVING params follow WHERE params).
	if (!groupKeys.empty()) {
		sql += wxT(" GROUP BY ");
		if (rollup) sql += m_dialect.m_rollupPrefix;   // "ROLLUP(" (standard) / "" (MySQL WITH ROLLUP)
		for (size_t i = 0; i < groupKeys.size(); ++i) {
			if (i) sql += wxT(", ");
			sql += RenderExpr(groupKeys[i]);
		}
		if (rollup) sql += m_dialect.m_rollupSuffix;   // ")" (standard) / " WITH ROLLUP" (MySQL)
	}
	if (having)
		sql += wxT(" HAVING ") + RenderExpr(having);

	if (!sortKeys.empty()) {
		sql += wxT(" ORDER BY ");
		for (size_t i = 0; i < sortKeys.size(); ++i) {
			if (i) sql += wxT(", ");
			sql += RenderExpr(sortKeys[i].m_expr);
			sql += (sortKeys[i].m_dir == ibQuerySortDir::Desc) ? wxT(" DESC") : wxT(" ASC");
		}
	}

	if (hasLimit && !leadingPagination) {
		if (m_dialect.m_pagination == ibPagination::LimitOffset) {
			if (limitCount >= 0) sql += wxString::Format(wxT(" LIMIT %ld"), limitCount);
			if (limitOffset > 0) sql += wxString::Format(wxT(" OFFSET %ld"), limitOffset);
		} else if (m_dialect.m_pagination == ibPagination::OffsetFetch) {
			sql += wxString::Format(wxT(" OFFSET %ld ROWS"), limitOffset);
			if (limitCount >= 0) sql += wxString::Format(wxT(" FETCH NEXT %ld ROWS ONLY"), limitCount);
		}
	}

	return sql;
}

wxString ibQueryRenderer::RenderExpr(const ibQueryExprPtr& expr)
{
	if (!expr) return wxString();

	switch (expr->m_kind) {
	case ibQueryExprKind::Column: {
		// A bare "*" (the qualified all-columns form, t.*) is NOT an identifier — it
		// must not be quoted. Used by the dot-walk projection (main.* + aliased leaf
		// columns) so the joined query keeps the main row's columns by their own names.
		const wxString col = (expr->m_name == wxT("*")) ? wxString(wxT("*")) : QuoteIdent(expr->m_name);
		return expr->m_qualifier.empty()
			? col
			: QuoteIdent(expr->m_qualifier) + wxT(".") + col;
	}

	case ibQueryExprKind::Const: {
		ibQueryParam p;
		p.m_external = false;
		if (expr->m_blob.GetDataLen() > 0) {
			p.m_isBlob = true;
			p.m_blob   = expr->m_blob;
		} else {
			p.m_value = expr->m_const;
		}
		m_out.m_params.push_back(p);
		return RenderPlaceholder();
	}

	case ibQueryExprKind::Param: {
		ibQueryParam p;
		p.m_external      = true;
		p.m_externalIndex = expr->m_paramIndex;
		m_out.m_params.push_back(p);
		return RenderPlaceholder();
	}

	case ibQueryExprKind::BinOp: {
		// RenderExpr has a side effect (appends to the bind plan), so the lhs
		// MUST be rendered before the rhs to keep the plan in SQL-text /
		// placeholder order. operator+ does NOT sequence its operands (MSVC
		// evaluates function arguments right-to-left), so relying on
		// `"(" + RenderExpr(lhs) + ... + RenderExpr(rhs) + ")"` reversed the
		// plan per BinOp and crossed param bindings (e.g. a guid bound into a
		// DATE column → FB type-mismatch crash). Sequence explicitly.
		const wxString lhs = RenderExpr(expr->m_lhs);
		const wxString rhs = RenderExpr(expr->m_rhs);
		return wxT("(") + lhs + wxT(" ") + BinOpText(expr->m_binOp) + wxT(" ") + rhs + wxT(")");
	}

	case ibQueryExprKind::Func: {
		wxString s = expr->m_name + wxT("(");
		for (size_t i = 0; i < expr->m_args.size(); ++i) {
			if (i) s += wxT(", ");
			s += RenderExpr(expr->m_args[i]);
		}
		s += wxT(")");
		return s;
	}

	case ibQueryExprKind::Case: {
		// CASE WHEN c THEN v ... [ELSE e] END. Render each WHEN's condition then its
		// value in order (RenderExpr appends to the bind plan; keep placeholder order).
		wxString s = wxT("(CASE");
		for (const auto& wt : expr->m_cases) {
			const wxString cond = RenderExpr(wt.first);
			const wxString val  = RenderExpr(wt.second);
			s += wxT(" WHEN ") + cond + wxT(" THEN ") + val;
		}
		if (expr->m_else) {
			const wxString els = RenderExpr(expr->m_else);
			s += wxT(" ELSE ") + els;
		}
		s += wxT(" END)");
		return s;
	}

	case ibQueryExprKind::In: {
		// Empty list = constant predicate; lhs not evaluated (so no stray bind).
		if (expr->m_args.empty())
			return expr->m_negated ? wxT("(1 = 1)") : wxT("(1 = 0)");
		const wxString lhs = RenderExpr(expr->m_lhs);
		wxString s = wxT("(") + lhs + (expr->m_negated ? wxT(" NOT IN (") : wxT(" IN ("));
		for (size_t i = 0; i < expr->m_args.size(); ++i) {
			if (i) s += wxT(", ");
			s += RenderExpr(expr->m_args[i]);
		}
		s += wxT("))");
		return s;
	}

	case ibQueryExprKind::IsNull: {
		const wxString lhs = RenderExpr(expr->m_lhs);
		return wxT("(") + lhs + (expr->m_negated ? wxT(" IS NOT NULL)") : wxT(" IS NULL)"));
	}

	case ibQueryExprKind::Not: {
		const wxString operand = RenderExpr(expr->m_lhs);
		return wxT("(NOT ") + operand + wxT(")");
	}

	case ibQueryExprKind::Cast:
		return wxT("CAST(") + RenderExpr(expr->m_lhs) + wxT(" AS ") + expr->m_name + wxT(")");
	}

	return wxString();
}

wxString ibQueryRenderer::RenderPlaceholder()
{
	++m_paramPos;
	switch (m_dialect.m_paramStyle) {
	case ibParamStyle::QuestionMark: return wxT("?");
	case ibParamStyle::DollarN:      return wxString::Format(wxT("$%d"), m_paramPos);
	case ibParamStyle::Colon:        return wxString::Format(wxT(":p%d"), m_paramPos);
	}
	return wxT("?");
}

wxString ibQueryRenderer::QuoteIdent(const wxString& name) const
{
	return m_dialect.m_identQuoteOpen + name + m_dialect.m_identQuoteClose;
}

wxString ibQueryRenderer::BinOpText(ibQueryBinOp op)
{
	switch (op) {
	case ibQueryBinOp::Add: return wxT("+");
	case ibQueryBinOp::Sub: return wxT("-");
	case ibQueryBinOp::Mul: return wxT("*");
	case ibQueryBinOp::Div: return wxT("/");
	case ibQueryBinOp::Mod: return wxT("%");   // NB: Firebird wants MOD() — out of MVP scope
	case ibQueryBinOp::Eq:  return wxT("=");
	case ibQueryBinOp::Ne:  return wxT("<>");
	case ibQueryBinOp::Lt:  return wxT("<");
	case ibQueryBinOp::Le:  return wxT("<=");
	case ibQueryBinOp::Gt:   return wxT(">");
	case ibQueryBinOp::Ge:   return wxT(">=");
	case ibQueryBinOp::Like: return wxT("LIKE");
	case ibQueryBinOp::And:  return wxT("AND");
	case ibQueryBinOp::Or:   return wxT("OR");
	}
	return wxT("=");
}

wxString ibQueryRenderer::JoinTypeText(ibQueryJoinType type)
{
	switch (type) {
	case ibQueryJoinType::Inner: return wxT("INNER JOIN");
	case ibQueryJoinType::Left:  return wxT("LEFT JOIN");
	case ibQueryJoinType::Right: return wxT("RIGHT JOIN");
	case ibQueryJoinType::Full:  return wxT("FULL OUTER JOIN");  // gated by m_features.m_fullOuterJoin; emulation later
	}
	return wxT("INNER JOIN");
}

// FROM source — a Scan (table name) or a Join-tree (rendered recursively). The
// ON predicate's bind params are pushed here, before the WHERE's, matching
// their left-to-right position in the finished SQL.
wxString ibQueryRenderer::RenderSource(const ibQueryRel* rel)
{
	if (rel == nullptr) return wxString();

	switch (rel->m_kind) {
	case ibQueryRelKind::Scan:
		return rel->m_alias.empty()
			? QuoteIdent(rel->m_table)
			: QuoteIdent(rel->m_table) + wxT(" AS ") + QuoteIdent(rel->m_alias);
	case ibQueryRelKind::Subquery:
		// ( SELECT ... ) AS alias — the inner relation renders its own SELECT, its
		// bind params landing in the plan at the FROM position (before the WHERE).
		return wxT("(") + RenderSelect(rel->m_input.get()) + wxT(") AS ") + QuoteIdent(rel->m_alias);
	case ibQueryRelKind::Join: {
		// Same operand-sequencing rule as BinOp: RenderSource / RenderExpr push
		// bind params as a side effect, so left source → right source → ON
		// predicate must be rendered in that order (operator+ does not sequence
		// its operands; MSVC evaluates right-to-left). Otherwise nested-join ON
		// params land in the plan out of placeholder order.
		const wxString left  = RenderSource(rel->m_input.get());
		const wxString right = RenderSource(rel->m_right.get());
		const wxString on    = RenderExpr(rel->m_joinPredicate);
		return left + wxT(" ") + JoinTypeText(rel->m_joinType) + wxT(" ")
		     + right + wxT(" ON ") + on;
	}
	default:
		// A wrapping rel (Filter/Project/...) is never a FROM source in
		// well-formed IR; fall through to its input defensively.
		return RenderSource(rel->m_input.get());
	}
}

// ==========================================================================
// DDL
// ==========================================================================

wxString ibQueryRenderer::RenderDDL(const ibDdlStatement& ddl)
{
	switch (ddl.m_kind) {
	case ibDdlKind::CreateTable: {
		// A TEMPORARY table substitutes the driver's temp CREATE prefix (from the L1 temp dialect,
		// supplied on the DDL) for "CREATE TABLE" and appends its suffix (ON COMMIT … / none).
		wxString sql = (ddl.m_temporary && !ddl.m_createPrefix.empty())
		             ? ddl.m_createPrefix + wxT(" ")
		             : wxT("CREATE TABLE ");
		if (ddl.m_ifNotExists) sql += wxT("IF NOT EXISTS ");
		sql += QuoteIdent(ddl.m_table) + wxT(" (");
		for (size_t i = 0; i < ddl.m_columns.size(); ++i) {
			if (i) sql += wxT(", ");
			sql += RenderColumn(ddl.m_columns[i]);
		}
		sql += wxT(")");
		if (ddl.m_temporary && !ddl.m_createSuffix.empty())
			sql += ddl.m_createSuffix;
		return sql;
	}
	case ibDdlKind::DropTable: {
		wxString sql = wxT("DROP TABLE ");
		if (ddl.m_ifExists) sql += wxT("IF EXISTS ");
		sql += QuoteIdent(ddl.m_table);
		return sql;
	}
	case ibDdlKind::AddColumn: {
		wxString sql = wxT("ALTER TABLE ") + QuoteIdent(ddl.m_table) + wxT(" ADD ");
		if (!ddl.m_columns.empty()) sql += RenderColumn(ddl.m_columns[0]);
		return sql;
	}
	case ibDdlKind::DropColumn: {
		wxString sql = wxT("ALTER TABLE ") + QuoteIdent(ddl.m_table) + wxT(" DROP COLUMN ");
		if (!ddl.m_columns.empty()) sql += QuoteIdent(ddl.m_columns[0].m_name);
		return sql;
	}
	case ibDdlKind::AlterTable: {
		// One ALTER folding every coalesced clause: ALTER TABLE t ADD c1, ADD c2, DROP COLUMN c3.
		wxString sql = wxT("ALTER TABLE ") + QuoteIdent(ddl.m_table) + wxT(" ");
		for (size_t i = 0; i < ddl.m_alterClauses.size(); ++i) {
			if (i) sql += wxT(", ");
			const ibAlterClause& clause = ddl.m_alterClauses[i];
			if (clause.m_op == ibAlterOp::Add)
				sql += wxT("ADD ") + RenderColumn(clause.m_column);
			else
				sql += wxT("DROP COLUMN ") + QuoteIdent(clause.m_column.m_name);
		}
		return sql;
	}
	case ibDdlKind::AlterColumn: {
		// SQLite's (empty) template => throw: it cannot change a column type in place.
		if (m_dialect.m_alterColumnTemplate.empty())
			ibBackendQueryException::Throw(ibBackendQueryException::Kind::UnsupportedNode,
				_("This database cannot change a column's type in place (a table rebuild is required)"));
		if (ddl.m_columns.empty())
			return wxString();
		wxString sql = m_dialect.m_alterColumnTemplate;
		sql.Replace(wxT("{table}"),  QuoteIdent(ddl.m_table));
		sql.Replace(wxT("{column}"), QuoteIdent(ddl.m_columns[0].m_name));
		sql.Replace(wxT("{type}"),   MapType(ddl.m_columns[0].m_type));
		return sql;
	}
	case ibDdlKind::CreateIndex: {
		wxString sql = ddl.m_unique ? wxT("CREATE UNIQUE INDEX ") : wxT("CREATE INDEX ");
		sql += QuoteIdent(ddl.m_indexName) + wxT(" ON ") + QuoteIdent(ddl.m_table) + wxT(" (");
		for (size_t i = 0; i < ddl.m_indexColumns.size(); ++i) {
			if (i) sql += wxT(", ");
			sql += QuoteIdent(ddl.m_indexColumns[i]);
		}
		sql += wxT(")");
		return sql;
	}
	case ibDdlKind::DropIndex: {
		wxString sql = wxT("DROP INDEX ") + QuoteIdent(ddl.m_indexName);
		if (m_dialect.m_dropIndexNeedsTable && !ddl.m_table.empty())
			sql += wxT(" ON ") + QuoteIdent(ddl.m_table);
		return sql;
	}
	}
	return wxString();
}

wxString ibQueryRenderer::RenderColumn(const ibDdlColumn& col)
{
	wxString s = QuoteIdent(col.m_name) + wxT(" ") + MapType(col.m_type);
	if (!col.m_default.empty()) s += wxT(" DEFAULT ") + col.m_default;   // before NOT NULL (e.g. "INTEGER DEFAULT 0 NOT NULL")
	if (col.m_primaryKey)   s += wxT(" PRIMARY KEY");   // implies NOT NULL
	else if (col.m_notNull) s += wxT(" NOT NULL");
	return s;
}

wxString ibQueryRenderer::MapType(const ibColumnType& type) const
{
	switch (type.m_kind) {
	case ibCanonicalKind::Boolean: return m_dialect.m_typeBoolean;
	case ibCanonicalKind::Integer: return m_dialect.m_typeInteger;
	case ibCanonicalKind::BigInt:  return m_dialect.m_typeBigInt;
	case ibCanonicalKind::Blob:    return m_dialect.m_typeBlob;
	case ibCanonicalKind::Guid:    return m_dialect.m_typeGuid;
	case ibCanonicalKind::Binary:
		// PG's BYTEA carries no length — render the pattern verbatim when it has no %d.
		return m_dialect.m_typeBinaryPattern.Contains(wxT("%"))
		     ? wxString::Format(m_dialect.m_typeBinaryPattern, type.m_length > 0 ? type.m_length : 16)
		     : m_dialect.m_typeBinaryPattern;
	case ibCanonicalKind::Date:
		switch (type.m_datePrec) {
		case ibDatePrec::Date:     return m_dialect.m_typeDateOnly;
		case ibDatePrec::Time:     return m_dialect.m_typeTime;
		case ibDatePrec::DateTime: return m_dialect.m_typeDate;
		}
		return m_dialect.m_typeDate;
	case ibCanonicalKind::String:
		return wxString::Format(type.m_fixed ? m_dialect.m_typeCharPattern : m_dialect.m_typeStringPattern,
		                        type.m_length > 0 ? type.m_length : 255);
	case ibCanonicalKind::Number:
		return wxString::Format(m_dialect.m_typeNumberPattern,
		                        type.m_precision > 0 ? type.m_precision : 18, type.m_scale);
	}
	return m_dialect.m_typeInteger;
}

// ==========================================================================
// DML
// ==========================================================================

ibRenderedQuery ibQueryRenderer::RenderDML(const ibDmlStatement& dml)
{
	m_out = ibRenderedQuery{};
	m_paramPos = 0;

	wxString sql;

	switch (dml.m_kind) {
	case ibDmlKind::Insert: {
		// INSERT … SELECT — the row source is a relation tree (RenderSelect pushes any of its binds
		// in placeholder order). Standard SQL, identical on every driver.
		if (dml.m_selectSource) {
			sql = wxT("INSERT INTO ") + QuoteIdent(dml.m_table);
			if (!dml.m_insertColumns.empty()) {
				sql += wxT(" (");
				for (size_t i = 0; i < dml.m_insertColumns.size(); ++i) {
					if (i) sql += wxT(", ");
					sql += QuoteIdent(dml.m_insertColumns[i]);
				}
				sql += wxT(")");
			}
			sql += wxT(" ") + RenderSelect(dml.m_selectSource.get());
			break;
		}
		sql = wxT("INSERT INTO ") + QuoteIdent(dml.m_table) + wxT(" (");
		for (size_t i = 0; i < dml.m_assignments.size(); ++i) {
			if (i) sql += wxT(", ");
			sql += QuoteIdent(dml.m_assignments[i].m_column);
		}
		sql += wxT(") VALUES (");
		for (size_t i = 0; i < dml.m_assignments.size(); ++i) {
			if (i) sql += wxT(", ");
			sql += RenderExpr(dml.m_assignments[i].m_value);   // pushes a bind param
		}
		sql += wxT(")");
		// Multi-row INSERT — each extra row appends ", (v0, v1, ...)" in the same column order; its
		// binds push after the prior rows', so the plan stays in placeholder order.
		for (const std::vector<ibQueryExprPtr>& row : dml.m_extraRows) {
			sql += wxT(", (");
			for (size_t i = 0; i < row.size(); ++i) {
				if (i) sql += wxT(", ");
				sql += RenderExpr(row[i]);
			}
			sql += wxT(")");
		}
		break;
	}
	case ibDmlKind::Update: {
		sql = wxT("UPDATE ") + QuoteIdent(dml.m_table) + wxT(" SET ");
		for (size_t i = 0; i < dml.m_assignments.size(); ++i) {
			if (i) sql += wxT(", ");
			sql += QuoteIdent(dml.m_assignments[i].m_column)
			     + wxT(" = ") + RenderExpr(dml.m_assignments[i].m_value);
		}
		if (dml.m_where)
			sql += wxT(" WHERE ") + RenderExpr(dml.m_where);
		break;
	}
	case ibDmlKind::Delete: {
		sql = wxT("DELETE FROM ") + QuoteIdent(dml.m_table);
		if (dml.m_where)
			sql += wxT(" WHERE ") + RenderExpr(dml.m_where);
		break;
	}
	case ibDmlKind::Upsert: {
		// Build the parts, then fill the dialect's UPSERT template. The renderer
		// holds NO per-DBMS spelling — that lives entirely in m_upsertTemplate /
		// m_upsertUpdateItem (dialectDictionary.h). Only {values} pushes bind
		// params, rendered here in column order, so the plan matches the text.
		auto isMatchKey = [&](const wxString& col) {
			for (const wxString& k : dml.m_matchKeys) if (k == col) return true;
			return false;
		};

		wxString columns, values;
		for (size_t i = 0; i < dml.m_assignments.size(); ++i) {
			const wxString sep = i ? wxT(", ") : wxT("");
			columns += sep + QuoteIdent(dml.m_assignments[i].m_column);
			values  += sep + RenderExpr(dml.m_assignments[i].m_value);   // pushes a bind param
		}

		wxString keys;
		for (size_t i = 0; i < dml.m_matchKeys.size(); ++i)
			keys += (i ? wxT(", ") : wxT("")) + QuoteIdent(dml.m_matchKeys[i]);

		// {update}: m_upsertUpdateItem per non-key column (empty for FB MATCHING).
		wxString update;
		if (!m_dialect.m_upsertUpdateItem.empty()) {
			for (const ibDmlAssign& a : dml.m_assignments) {
				if (isMatchKey(a.m_column)) continue;   // PK is the identity, never updated
				wxString item = m_dialect.m_upsertUpdateItem;
				item.Replace(wxT("{col}"), QuoteIdent(a.m_column));
				if (!update.empty()) update += wxT(", ");
				update += item;
			}
		}

		sql = m_dialect.m_upsertTemplate;
		sql.Replace(wxT("{table}"),   QuoteIdent(dml.m_table));
		sql.Replace(wxT("{columns}"), columns);
		sql.Replace(wxT("{values}"),  values);
		sql.Replace(wxT("{keys}"),    keys);
		sql.Replace(wxT("{update}"),  update);
		break;
	}
	}

	m_out.m_sql = sql;
	return m_out;
}

// ==========================================================================
// ibQueryStatement (merged from queryStatement.cpp)
// ==========================================================================
ibQueryStatement::ibQueryStatement(Kind kind, const wxString& table, std::vector<wxString> columns,
                                   std::vector<wxString> matchKeys, ibDatabaseConnectionHolder* holder)
	: m_kind(kind), m_table(table), m_columns(std::move(columns)),
	  m_matchKeys(std::move(matchKeys)), m_values(m_columns.size()), m_holder(holder)
{
}

void ibQueryStatement::Put(int position, ibQueryExprPtr expr)
{
	if (position >= 1 && position <= static_cast<int>(m_values.size()))
		m_values[position - 1] = std::move(expr);
}

// Every bind becomes a bound value node — never inlined, never executed here.
void ibQueryStatement::SetParamInt(int p, int v)                  { Put(p, ibConst(ibValue(ibNumber(v)))); }
void ibQueryStatement::SetParamDouble(int p, double v)            { Put(p, ibConst(ibValue(v))); }
void ibQueryStatement::SetParamNumber(int p, const ibNumber& v)   { Put(p, ibConst(ibValue(v))); }
void ibQueryStatement::SetParamString(int p, const wxString& v)   { Put(p, ibConst(ibValue(v))); }
void ibQueryStatement::SetParamNull(int p)                        { Put(p, ibConst(ibValue())); }
void ibQueryStatement::SetParamBlob(int p, const void* d, long n) { Put(p, ibConstBlob(d, static_cast<size_t>(n))); }
void ibQueryStatement::SetParamDate(int p, const wxDateTime& v)   { Put(p, ibConst(ibValue(v))); }
void ibQueryStatement::SetParamBool(int p, bool v)                { Put(p, ibConst(ibValue(v))); }

ibDmlStatement ibQueryStatement::BuildDml() const
{
	if (m_kind == Kind::Delete) {
		// DELETE ... WHERE col0 = v0 AND col1 = v1 ... (composite key). A column
		// with no bound value contributes nothing — never deletes by NULL.
		ibQueryExprPtr pred;
		for (size_t i = 0; i < m_columns.size(); ++i) {
			if (!m_values[i]) continue;
			ibQueryExprPtr eq = ibBinOp(ibQueryBinOp::Eq, ibCol(m_columns[i]), m_values[i]);
			pred = pred ? ibBinOp(ibQueryBinOp::And, pred, eq) : eq;
		}
		return ibDelete(m_table, pred);
	}

	std::vector<ibDmlAssign> assigns;
	assigns.reserve(m_columns.size());
	for (size_t i = 0; i < m_columns.size(); ++i)
		assigns.push_back({ m_columns[i], m_values[i] ? m_values[i] : ibConst(ibValue()) });

	return (m_kind == Kind::Upsert)
		? ibUpsert(m_table, std::move(assigns), m_matchKeys)
		: ibInsert(m_table, std::move(assigns));
}

int ibQueryStatement::RunQuery()
{
	// Default door (null holder) resolves to the session holder, so the write
	// joins any open document-save TX — same as the legacy ses_query path.
	if (m_holder != nullptr) {
		ibDatabaseQueryBuilder q(m_holder);
		return q.Execute(BuildDml());
	}
	ibDatabaseQueryBuilder q;
	return q.Execute(BuildDml());
}

ibDatabaseResultSet* ibQueryStatement::RunQueryWithResults()
{
	RunQuery();
	return nullptr;
}
