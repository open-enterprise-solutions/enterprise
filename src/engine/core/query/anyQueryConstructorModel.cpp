////////////////////////////////////////////////////////////////////////////
//	Description : the query constructor's model, as either side holds it (anyQueryConstructorModel.h)
////////////////////////////////////////////////////////////////////////////

#include "anyQueryConstructorModel.h"

#include "core/exception.h"     // ibCoreException — a half-typed text keeps its roles
#include "core/query/queryParser.h"   // ibQueryCastType / ibQueryMakeCast — a primitive CAST, both ways
#include "core/query/queryRender.h"   // ibRenderQueryExpr — an unaliased projection is named by its own text

#include <algorithm>
#include <functional>   // a computed projection is walked through for the fields it reads

namespace {

// …A FIELD WHOSE TYPE A CAST SAYS — typed and walkable the same way, from the type the CAST names.
void TypeByCast(const ibAnyQueryConstructorModel& model, const ibQueryAstExpr& cast, ibQueryConstructorField& field)
{
	field.m_type = model.TypeOfCast(cast);
	model.ResolveReference(field);
}

} // namespace

ibAnyQueryConstructorModel::~ibAnyQueryConstructorModel()
{
}

std::vector<ibQueryConstructorSource> ibAnyQueryConstructorModel::GetTempSources(
	const ibQueryPackage& package, size_t beforeStatement)
{
	std::vector<ibQueryConstructorSource> out;

	// ORDER IS THE WHOLE POINT: only what the statements BEFORE this one left is selectable.
	// A table made later does not exist yet, and offering it would produce a query that reads a
	// name nothing has filled.
	const size_t limit = std::min(beforeStatement, package.m_statements.size());
	for (size_t i = 0; i < limit; ++i) {
		const ibQueryAstStatement& statement = package.m_statements[i];

		if (statement.IsDrop()) {
			// A drop takes the name back out of the list — that is what "release early" MEANS,
			// and a constructor still offering it would be offering something already gone.
			const auto it = std::find_if(out.begin(), out.end(),
				[&statement](const ibQueryConstructorSource& s) {
					return !s.m_path.empty() && s.m_path[0].IsSameAs(statement.m_dropTemp, false);
				});
			if (it != out.end())
				out.erase(it);
			continue;
		}

		if (!statement.m_select)
			continue;

		// ⭐ A NAMED RESULT IS SELECTABLE TOO — a query result link. `ONTO Sales` makes `Sales` a name a
		// later statement can read, and reading it is what a LINK is: the package resolves the name
		// into that statement's own select, so the join runs in the DBMS.
		//
		// Listed beside the temp tables because from the constructor's side they are the same
		// question — "what may this statement select from that the package itself made" — and the
		// difference (a table versus a named result) is the package's business, not the tree's.
		const ibString& name = statement.m_select->m_intoTemp.IsEmpty()
			? statement.m_select->m_ontoName : statement.m_select->m_intoTemp;
		if (name.IsEmpty())
			continue;

		ibQueryConstructorSource source;
		source.m_path.push_back(name);
		source.m_presentation = name;
		source.m_temp = true;
		source.m_namedResult = statement.m_select->m_intoTemp.IsEmpty();   // named by ONTO, not INTO
		out.push_back(std::move(source));
	}
	return out;
}

// THE WALK, DONE ONCE. Both type questions read its answer — what the leaf REFERS TO and what it
// HOLDS — so there is one traversal and one place where "this path does not resolve" is decided.
ibQueryConstructorField ibAnyQueryConstructorModel::FieldOfPath(const ibQuerySelect& select,
                                                                const std::vector<ibString>& path,
                                                                const ibQueryPackage& package,
                                                                size_t beforeStatement) const
{
	if (path.empty())
		return ibQueryConstructorField();

	// WHICH SOURCE THE PATH STARTS ON. A qualified path names it outright; an unqualified one starts
	// on whichever source owns its first segment — the same two cases the lowering's ResolvePath
	// distinguishes, and for the same reason: the first segment is either a table or a field.
	const ibQuerySource* source = nullptr;
	size_t first = 0;
	if (path.size() > 1) {
		if (ibQuerySourceName(select.m_from).IsSameAs(path[0], false)) {
			source = &select.m_from;
			first  = 1;
		}
		else {
			for (const ibQueryAstJoin& join : select.m_joins)
				if (ibQuerySourceName(join.m_source).IsSameAs(path[0], false)) {
					source = &join.m_source;
					first  = 1;
					break;
				}
		}
	}

	std::vector<ibQueryConstructorField> fields;
	if (source != nullptr) {
		fields = GetFields(*source, package, beforeStatement);
	}
	else {
		// Unqualified: the source that HAS this field. The primary first, which is what an
		// unqualified name means when both sides could answer.
		std::vector<const ibQuerySource*> all{ &select.m_from };
		for (const ibQueryAstJoin& join : select.m_joins)
			all.push_back(&join.m_source);
		for (const ibQuerySource* candidate : all) {
			std::vector<ibQueryConstructorField> its = GetFields(*candidate, package, beforeStatement);
			for (const ibQueryConstructorField& field : its)
				if (field.m_name.IsSameAs(path[0], false)) { fields = std::move(its); break; }
			if (!fields.empty())
				break;
		}
	}

	// WHERE THE PATH STARTS among the fields of that source, and the hops after it.
	return WalkPath(fields, std::vector<ibString>(path.begin() + first, path.end()));
}

ibQueryConstructorField ibAnyQueryConstructorModel::WalkPath(const std::vector<ibQueryConstructorField>& fields,
                                                            const std::vector<ibString>& path) const
{
	for (size_t head = path.size(); head > 0; --head) {
		ibString name = path.front();
		for (size_t i = 1; i < head; ++i)
			name += ibT(".") + path[i];
		const auto start = std::find_if(fields.begin(), fields.end(),
			[&name](const ibQueryConstructorField& field) { return field.m_name.IsSameAs(name, false); });
		if (start != fields.end())
			return WalkFrom(*start, std::vector<ibString>(path.begin() + head, path.end()));
	}
	return ibQueryConstructorField();   // a name this model cannot resolve — silence, never a guess
}

// THE HOPS. Each segment must be found among the fields of the level above it; a segment that is not a
// reference ends the walk, because there is nothing behind it to look in.
ibQueryConstructorField ibAnyQueryConstructorModel::WalkFrom(const ibQueryConstructorField& from,
                                                            const std::vector<ibString>& hops) const
{
	ibQueryConstructorField leaf = from;
	for (const ibString& hop : hops) {
		// The whole type, not the single target: a composite hop offers what all its alternatives offer,
		// merged the way the query merges them — and nothing at all for what is not a reference.
		const std::vector<ibQueryConstructorField> fields = GetReferenceFields(leaf.m_type);
		const auto found = std::find_if(fields.begin(), fields.end(),
			[&hop](const ibQueryConstructorField& field) { return field.m_name.IsSameAs(hop, false); });
		if (found == fields.end()) {
			// Silence, never a guess — but what the walk KNEW stands: a hop nobody answers past a hidden
			// one does not bring the hidden one back.
			ibQueryConstructorField unresolved;
			unresolved.m_available = leaf.m_available;
			return unresolved;
		}
		const bool available = leaf.m_available && found->m_available;   // one hidden hop hides the walk
		leaf = *found;
		leaf.m_available = available;
	}
	return leaf;
}

ibClassID ibAnyQueryConstructorModel::ReferenceOfPath(const ibQuerySelect& select,
                                                      const std::vector<ibString>& path,
                                                      const ibQueryPackage& package,
                                                      size_t beforeStatement) const
{
	return FieldOfPath(select, path, package, beforeStatement).m_referenceClsid;
}

ibTypeDescription ibAnyQueryConstructorModel::TypeOfPath(const ibQuerySelect& select,
                                                         const std::vector<ibString>& path,
                                                         const ibQueryPackage& package,
                                                         size_t beforeStatement) const
{
	// EMPTY IS "UNKNOWN", never "no type" — and a host that reads it as unknown offers everything
	// rather than narrowing on a guess.
	return FieldOfPath(select, path, package, beforeStatement).m_type;
}

std::vector<ibQueryConstructorField> ibAnyQueryConstructorModel::FieldsOfSelect(
	const ibQuerySelect& select, const ibQueryPackage& package, size_t beforeStatement) const
{
	std::vector<ibQueryConstructorField> out;

	// SELECT * over one source IS that source's field list — types and all. Reading it as "no
	// projections, therefore no fields" is what made a `SELECT * INTO Tmp` temp table look empty.
	if (select.m_selectAll)
		return GetFields(select.m_from, package, beforeStatement);

	for (const ibQueryProjection& projection : select.m_projections) {
		ibQueryConstructorField field;
		// THE NAME THE OUTER QUERY REFERS TO IT BY — the engine's own answer, so the constructor and
		// the runtime cannot call one column two things.
		field.m_name = ibQueryOutputName(projection);
		if (field.m_name.IsEmpty() && projection.m_expr)
			field.m_name = ibRenderQueryExpr(*projection.m_expr);
		if (field.m_name.IsEmpty())
			continue;
		field.m_presentation = field.m_name;

		// …AND WHAT IT HOLDS. A projected reference stays a reference in the table it lands in, so
		// the next statement can walk into it exactly as it could have walked into the original.
		//
		// ⭐ THE TYPE COMES WITH IT, and the whole field is asked for rather than just the reference
		// clsid: one walk answers both. m_type is what a HOST does its type questions with — which
		// aggregates fit, what the other side of a condition may be, and whether the field unfolds
		// (ibSettingsFieldTree reads its clsid list to decide the [+]). Leaving it empty here made
		// every field of a composition's query look like a leaf of no type at all.
		if (projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Column) {
			const ibQueryConstructorField of = FieldOfPath(select, projection.m_expr->m_path,
			                                              package, beforeStatement);
			field.m_referenceClsid = of.m_referenceClsid;
			field.m_reference      = of.m_reference;   // a composite stays walkable across the projection
			field.m_type           = of.m_type;
			if (!of.m_picture.IsEmptyPicture())
				field.m_picture = of.m_picture;   // the column's own picture, when it has one
			field.m_available = of.m_available;
			// …and what it is in a balance travels with a plain reading, renamed or not:
			// `T.AmountClosingBalance AS Amount` is still a closing balance. What the source CALLS it travels
			// only while the query keeps the source's name — a field the author renamed is spoken for by
			// that name.
			field.m_balanceRole = of.m_balanceRole;
			if (!projection.m_expr->m_path.empty() && field.m_name.IsSameAs(projection.m_expr->m_path.back(), false))
				field.m_caption = of.m_caption;
		}
		else if (projection.m_expr) {
			// ⭐ A COMPUTED ONE IS AVAILABLE WHILE EVERYTHING IT READS IS — each field asked the way a plain
			// projection is. One the options take away takes the result with it: a value computed from a
			// hidden field would show that field under another name.
			std::function<bool(const ibQueryAstExpr&)> readsAvailable = [&](const ibQueryAstExpr& e) {
				if (e.m_kind == ibQueryAstExprKind::Column)
					return FieldOfPath(select, e.m_path, package, beforeStatement).m_available;
				bool available = true;
				ibQueryForEachOperand(e, [&](const ibQueryAstExprPtr& child) {
					available = available && (!child || readsAvailable(*child));
				});
				return available;
			};
			field.m_available = readsAvailable(*projection.m_expr);
			// ⭐ A CAST SAYS ITS TYPE, and a temporary table described by CASTs over a table handed in as a
			// parameter has typed fields before that parameter holds a row — the next statement walks by them.
			if (projection.m_expr->m_kind == ibQueryAstExprKind::Cast)
				TypeByCast(*this, *projection.m_expr, field);
		}
		// …AND A ROLE THE QUERY SAYS ITSELF (`ROLE OPENING`) is what the field is, over whatever its source says.
		if (projection.m_roleSaid)
			field.m_balanceRole = projection.m_role;
		out.push_back(std::move(field));
	}
	return out;
}

std::vector<ibQueryConstructorField> ibAnyQueryConstructorModel::GetFields(
	const ibQuerySource& source, const ibQueryPackage& package, size_t beforeStatement) const
{
	// Every field carries the SOURCE it came out of — stamped once, here, so the shell never has to
	// work it back out of a qualified name (which a dot-walk path makes ambiguous anyway).
	struct Stamp {
		const ibString label;
		std::vector<ibQueryConstructorField> operator()(std::vector<ibQueryConstructorField> fields) const {
			for (ibQueryConstructorField& field : fields) field.m_source = label;
			return fields;
		}
	} stamp{ ibQuerySourceLabel(source) };

	// A NESTED TABLE answers with its own projections — its fields come from the inner query, not
	// from any descriptor, because there is no metaobject standing behind it.
	if (source.m_subquery)
		return stamp(FieldsOfSelect(*source.m_subquery, package, beforeStatement));

	if (source.m_name.empty())
		return {};

	// ⭐ A TABLE HANDED IN (`FROM &Goods`) — or a temporary table no statement of this package makes, which the
	// temporary tables manager brings when the query runs — has no rows while the query is written: its fields
	// are what the select READING it says of them (FieldsTakenFrom). That select is found by the source itself,
	// which is one of its nodes; nothing else could answer before the query runs.
	auto described = [&]() -> std::vector<ibQueryConstructorField> {
		auto reads = [&source](const ibQuerySelect& select) {
			if (&select.m_from == &source)
				return true;
			for (const ibQueryAstJoin& join : select.m_joins)
				if (&join.m_source == &source)
					return true;
			return false;
		};
		for (const ibQueryAstStatement& statement : package.m_statements) {
			if (!statement.m_select)
				continue;
			if (reads(*statement.m_select))
				return stamp(FieldsTakenFrom(*statement.m_select, source));
			for (const ibQuerySelectPtr& branch : statement.m_select->m_unions)
				if (branch && reads(*branch))
					return stamp(FieldsTakenFrom(*branch, source));
		}
		return {};
	};
	if (source.m_parameter)
		return described();

	// A TEMP TABLE is a bare name: find the statement that MADE it and read its projections. Same
	// answer as a nested table's, and for the same reason — a select is what defines both.
	if (source.m_name.size() == 1) {
		const size_t limit = std::min(beforeStatement, package.m_statements.size());
		for (size_t i = limit; i > 0; --i) {
			const ibQueryAstStatement& statement = package.m_statements[i - 1];
			// The name may be a temp table's OR a named result's — both are defined by the select
			// that made them, and that select is what answers "which fields does this have".
			if (statement.m_select
			    && (statement.m_select->m_intoTemp.IsSameAs(source.m_name[0], false)
			     || statement.m_select->m_ontoName.IsSameAs(source.m_name[0], false)))
				// ⚠ RESOLVED AS OF THE MAKER, not as of us: the making statement sees only what came
				// BEFORE it, which is also what stops a temp table resolving through itself.
				return stamp(FieldsOfSelect(*statement.m_select, package, i - 1));
		}
		return described();   // made by nobody here — the manager's, described where it is read
	}

	// A TABLE OF THE CONFIGURATION answers through the side — the engine's descriptor, the client's tab.
	return stamp(GetTableFields(source));
}

std::vector<ibQueryConstructorField> ibAnyQueryConstructorModel::GetQualifiedFields(
	const ibQuerySource& source, const ibQueryPackage& package, size_t beforeStatement) const
{
	// The prefix a query actually writes: the alias where the author gave one, else the source's
	// own last segment (Catalog.Products -> Products), which is what the lowering resolves by.
	const ibString prefix = ibQuerySourceName(source);

	std::vector<ibQueryConstructorField> out = GetFields(source, package, beforeStatement);
	if (prefix.IsEmpty())
		return out;

	// ⚠ ONLY THE NAME IS QUALIFIED — never the PRESENTATION. The name is what the query TEXT must
	// carry to be unambiguous; the presentation is what a person reads, and they read it under a
	// node that already says which table it is. Prefixing both produced `Catalog1 › Catalog1.Code`,
	// which is the table's name twice on one line and the field's name pushed off the right — the
	// whole reason the field trees stopped being readable once a second table joined.
	for (ibQueryConstructorField& field : out)
		field.m_name = prefix + ibT(".") + field.m_name;
	return out;
}

// ONE LEVEL DOWN A REFERENCE — the type of the one class, walked as the overload below walks every type.
std::vector<ibQueryConstructorField> ibAnyQueryConstructorModel::GetReferenceFields(ibClassID clsid,
                                                                                   const ibString& sourceLabel) const
{
	if (clsid == 0)
		return {};   // no type at all — nothing behind it
	ibTypeDescription type;
	type.SetDefaultMetaType(clsid);
	return GetReferenceFields(type, sourceLabel);
}

// EVERY TABLE BEHIND A TYPE, ITS FIELDS MERGED BY NAME — see the note on the declaration. The merge is the
// one the query performs on the same walk: a name carried by several alternatives is ONE field,
// because `Recorder.Date` is one column whichever document a row points at.
std::vector<ibQueryConstructorField> ibAnyQueryConstructorModel::GetReferenceFields(const ibTypeDescription& typeDesc,
                                                                                   const ibString& sourceLabel) const
{
	std::vector<ibQueryConstructorField> out;

	for (const ibMetaID table : ConvertToMetaIds(typeDesc.GetClsidList())) {
		for (ibQueryConstructorField& field : GetTableFields(table)) {
			field.m_source = sourceLabel;   // the walk stays under the table it started from

			const auto same = std::find_if(out.begin(), out.end(),
				[&field](const ibQueryConstructorField& have) { return have.m_name.IsSameAs(field.m_name, false); });

			if (same == out.end()) {
				out.push_back(std::move(field));
				continue;
			}

			// ⭐ THE SAME NAME FROM A SECOND BRANCH WIDENS THE TYPE rather than being dropped. Two
			// documents' `Contract` may point at different catalogs, and a walk one hop further has
			// to be offered the fields of BOTH — which is exactly what this same function then does
			// with the widened type. Dropping the duplicate would have quietly narrowed the walk to
			// whichever branch happened to be enumerated first.
			for (const ibClassID& alternative : field.m_type.GetClsidList())
				same->m_type.AppendMetaType(alternative);

			// …and what each branch said of it stands for the union: walkable while either is, referring to one
			// table only while both refer to the same one.
			same->m_reference = same->m_reference || field.m_reference;
			if (same->m_referenceClsid != field.m_referenceClsid)
				same->m_referenceClsid = 0;
			same->m_available = same->m_available || field.m_available;   // offered while any branch offers it
		}
	}

	return out;
}

void ibAnyQueryConstructorModel::ResolveReference(ibQueryConstructorField& field) const
{
	const std::vector<ibClassID>& clsids = field.m_type.GetClsidList();
	const std::vector<ibMetaID> tables = ConvertToMetaIds(clsids);
	field.m_reference      = !tables.empty();
	field.m_referenceClsid = clsids.size() == 1 && tables.size() == 1 ? clsids.front() : 0;
}

// ONE ENTRY PER ALTERNATIVE — see the note on the declaration. Named as a query names the type
// (`Document.GoodsIssue`), which is both what a tree shows and what a person would have to write to
// cast to it.
std::vector<ibQueryConstructorField> ibAnyQueryConstructorModel::GetReferenceBranches(const ibTypeDescription& typeDesc) const
{
	std::vector<ibQueryConstructorField> out;

	for (const ibClassID& clsid : typeDesc.GetClsidList()) {
		const ibString table = TableOfReference(clsid);
		if (table.IsEmpty())
			continue;   // no table behind it — a primitive, a barrier, a report or a data processor

		ibQueryConstructorField branch;
		branch.m_name           = table;
		branch.m_presentation   = branch.m_name;
		branch.m_referenceClsid = clsid;
		branch.m_reference      = true;
		branch.m_type.SetDefaultMetaType(clsid);   // the branch IS that one type, so it unfolds like any reference

		out.push_back(std::move(branch));
	}

	return out;
}

ibQueryAstExprPtr ibAnyQueryConstructorModel::CastTo(ibQueryAstExprPtr value, const ibTypeDescription& type) const
{
	if (ibQueryAstExprPtr primitive = ibQueryMakeCast(value, type))
		return primitive;
	const std::vector<ibQueryConstructorField> branches =
		type.GetClsidCount() == 1 ? GetReferenceBranches(type) : std::vector<ibQueryConstructorField>();
	if (branches.size() != 1)
		return value;
	auto cast = ibQueryAstExpr::Make(ibQueryAstExprKind::Cast);
	cast->m_arg = std::move(value);
	const ibString& table = branches.front().m_name;   // `Catalog.Goods`
	cast->m_path = { table.BeforeFirst(ibT('.')), table.AfterFirst(ibT('.')) };
	return cast;
}

std::vector<ibQueryConstructorField> ibAnyQueryConstructorModel::FieldsTakenFrom(const ibQuerySelect& reader,
                                                                                  const ibQuerySource& source) const
{
	const ibString table = ibQuerySourceName(source);
	std::vector<ibQueryConstructorField> out;
	for (const ibQueryProjection& projection : reader.m_projections) {
		const ibQueryAstExprPtr& e = projection.m_expr;
		const bool cast = e && e->m_kind == ibQueryAstExprKind::Cast;
		const ibQueryAstExprPtr& column = cast ? e->m_arg : e;
		if (!column || column->m_kind != ibQueryAstExprKind::Column || column->m_arg || column->m_path.size() != 2
		    || !column->m_path.front().IsSameAs(table, false))
			continue;
		const ibString& name = column->m_path.back();
		if (std::any_of(out.begin(), out.end(), [&name](const ibQueryConstructorField& f) { return f.m_name.IsSameAs(name, false); }))
			continue;
		ibQueryConstructorField field;
		field.m_name = field.m_presentation = name;
		field.m_source = ibQuerySourceLabel(source);
		if (cast)
			TypeByCast(*this, *e, field);
		out.push_back(std::move(field));
	}
	return out;
}

// A PRIMITIVE CAST IS THE PARSER'S; a CAST to a table types its value by that table's own reference (ReferenceOfTable).
ibTypeDescription ibAnyQueryConstructorModel::TypeOfCast(const ibQueryAstExpr& cast) const
{
	ibTypeDescription type;
	if (ibQueryCastType(cast, type))
		return type;
	type = ibTypeDescription();
	if (cast.m_kind != ibQueryAstExprKind::Cast || cast.m_path.size() < 2)
		return type;
	const ibClassID reference = ReferenceOfTable(cast.m_path);
	if (reference != 0)
		type.SetDefaultMetaType(reference);
	return type;
}

//////////////////////////////////////////////////////////////////////
// Roles — the package's own word
//////////////////////////////////////////////////////////////////////

namespace {

// EVERY ROLE A SELECT SAYS, FORGOTTEN — its own projections, its union's branches and what it reads FROM.
// True when there was one.
bool ForgetSaidRoles(ibQuerySelect& select)
{
	bool forgot = false;
	for (ibQueryProjection& projection : select.m_projections) {
		if (!projection.m_roleSaid)
			continue;
		projection.m_roleSaid   = false;
		projection.m_role       = ibBalanceRole::None;
		projection.m_periodRank = 0;
		forgot = true;
	}
	if (select.m_from.m_subquery)
		forgot = ForgetSaidRoles(*select.m_from.m_subquery) || forgot;
	for (ibQueryAstJoin& join : select.m_joins)
		if (join.m_source.m_subquery)
			forgot = ForgetSaidRoles(*join.m_source.m_subquery) || forgot;
	for (const ibQuerySelectPtr& branch : select.m_unions)
		if (branch)
			forgot = ForgetSaidRoles(*branch) || forgot;
	return forgot;
}

} // namespace

bool ibQueryDropRoles(ibQueryPackage& package)
{
	bool forgot = false;
	for (ibQueryAstStatement& statement : package.m_statements)
		if (statement.m_select)
			forgot = ForgetSaidRoles(*statement.m_select) || forgot;
	return forgot;
}

ibString ibQueryTextWithoutRoles(const ibString& text)
{
	if (text.IsEmpty())
		return text;
	ibQueryPackage package;
	try { package = ibQueryParser().ParsePackage(text); }
	catch (const ibCoreException&) { return text; }   // half-typed: whoever reads it says why, in the parser's words
	return ibQueryDropRoles(package) ? ibRenderQueryPackage(package) : text;
}
