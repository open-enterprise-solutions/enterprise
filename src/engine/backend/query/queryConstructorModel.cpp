////////////////////////////////////////////////////////////////////////////
//	Description : L4-1 — the query constructor's MODEL half (queryConstructorModel.h)
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorModel.h"

#include "queryRender.h"                 // ibRenderQueryExpr — an unaliased projection is named by its own text
#include "queryParser.h"                 // ibQueryCastType / ibQueryMakeCast — a primitive CAST, both ways
#include "queryable.h"                   // ibBackendQueryable::GetSourceMetaObject — a table's own reference type
#include "backend/metaCollection/genericData.h"   // ResolveQueryConstant — …typed by its empty reference
#include "backend/appData.h"             // ibApplicationInstance::GetQueryableFactory (no config open)
#include "backend/metaData.h"            // ibMetaData::GetSourceFactory — the config the query runs on behalf of
#include "backend/srcDataObject.h"       // ibSourceDataObject::ibSourceExplorer — what a source answers its fields with

#include <algorithm>
#include <functional>   // a computed projection is walked through for the fields it reads

// ⚠ WHAT CAN BE WALKED THROUGH, and it is NOT "the field has a type". Every typed attribute carries
// a clsid list — a string field's list holds the string's clsid — so "exactly one clsid" said yes
// to every field in the configuration and put an unfoldable [+] on all of them, behind which there
// was nothing. The question is whether that one type is a REFERENCE, which the id answers by its
// kind alone (clsid.h — the high byte IS the kind, no metadata lookup).
//
// A COMPOSITE reference names several, so there is no ONE target — this stays the answer to "what
// does it refer to", and it is zero for a composite. Whether the field can be WALKED is a different
// question, asked below.
//
// A barrier (`AnyRef`, `CatalogRef`) is no target either: it names no table. A field declared with one holds
// its members (FieldOfExplorer), and one with none to hold is a leaf.
static ibClassID SingleReferenceOf(const std::vector<ibClassID>& clsids)
{
	return clsids.size() == 1 && IsReference(clsids.front()) && !clsid_is_any(clsids.front()) ? clsids.front() : 0;
}

// ⭐ CAN IT BE WALKED — and the honest test is "is there ANY reference here", not "is there exactly
// one". The engine walks a composite: the lowering resolves a representative chain and the provider
// branches per alternative, COALESCING the leaf (queryLowering.cpp, ResolveReferenceTargets). While
// this model asked the single-target question, a register's Recorder was shown as a leaf and
// query_fields refused the path — for a walk that runs and returns rows.
static bool HasReference(const std::vector<ibClassID>& clsids)
{
	for (const ibClassID& clsid : clsids)
		if (IsReference(clsid) && !clsid_is_any(clsid))
			return true;
	return false;
}

// ⭐ ONE FIELD OF A SOURCE, AS THE CONSTRUCTOR OFFERS IT — written once for the three lists that offer
// fields (a source's own, one hop down a reference, a virtual table's condition), which each spelled it
// out for themselves.
//
// ⭐⭐ SHOWN BY ITS NAME, the one the query writes (Max, 2026-09-10: "in the query constructor it must
// be the name, not the synonym"). The sources were already listed by name and the fields by synonym,
// so a person picked a Russian synonym in the tree and read `Employee` in the text beside it — two
// vocabularies for one field, and the synonym in whichever language happened to be filled. The query
// constructor is where the query language is written; its tree speaks that language.
static ibQueryConstructorField FieldOfExplorer(const ibSourceDataObject::ibSourceExplorer& node)
{
	ibQueryConstructorField field;
	field.m_name           = node.GetSourceName();
	field.m_presentation   = node.GetSourceName();
	// ⭐ WHAT IT HOLDS (GetTypeValueDesc), the types the query walks it by (ResolveReferenceTargets): a field
	// declared `AnyRef` unfolds into every reference of the configuration, a characteristic into its chart's.
	// Read by the declaration, both were offered as a leaf, or as a [+] with nothing behind it.
	const ibTypeDescription& held = node.GetTypeValueDesc();
	// A reference field can be dot-walked further (Supplier.Region.Country) — the shell shows it with
	// a [+] and asks again with the leaf's own source.
	field.m_referenceClsid = SingleReferenceOf(held.GetClsidList());
	field.m_reference      = HasReference(held.GetClsidList());
	field.m_type           = held;
	field.m_icon           = node.GetSourceIcon();   // the column's own picture, asked not deduced
	field.m_available      = node.GetColumn() == nullptr || node.GetColumn()->IsAvailable();
	// …what a person calls it, and what it is in a balance — for the hosts that fill a field in from them.
	field.m_caption        = node.GetSourceSynonym();
	field.m_balanceRole    = node.GetColumn() != nullptr ? node.GetColumn()->GetBalanceRole() : ibBalanceRole::None;
	return field;
}

// …AND A FIELD WHOSE TYPE A CAST SAYS — typed and walkable the same way, from the type the CAST names.
static void TypeByCast(const ibQueryConstructorModel& model, const ibQueryAstExpr& cast, ibQueryConstructorField& field)
{
	field.m_type           = model.TypeOfCast(cast);
	field.m_referenceClsid = SingleReferenceOf(field.m_type.GetClsidList());
	field.m_reference      = HasReference(field.m_type.GetClsidList());
}

ibQueryConstructorModel::ibQueryConstructorModel(const ibMetaData* metaData)
	: m_metaData(metaData)
{
}

ibQueryableFactory* ibQueryConstructorModel::Factory() const
{
	// The config's OWN factory first (sources register per-config, and it descends to the global one
	// on a miss); with no config open the global factory is the honest answer, not an error.
	ibQueryableFactory* factory = m_metaData != nullptr ? m_metaData->GetSourceFactory() : nullptr;
	return factory != nullptr ? factory : ibApplicationInstance::GetQueryableFactory();
}

std::vector<ibQueryConstructorSource> ibQueryConstructorModel::GetSources() const
{
	std::vector<ibQueryConstructorSource> out;

	ibQueryableFactory* factory = Factory();
	if (factory == nullptr)
		return out;   // pre-appData / no config — an empty catalogue, not a crash

	// THE WALK. Whatever registered is what the constructor offers — a metatype added tomorrow
	// appears here the day it vends a descriptor, with nothing edited in this file.
	for (ibQueryableSourceDescriptor* descriptor : factory->GetDescriptors()) {
		if (descriptor == nullptr)
			continue;

		ibQueryConstructorSource source;
		source.m_path.push_back(descriptor->GetNamespace());

		// A descriptor's name is already the composite one for a virtual table
		// ("Goods.Balance"), so it splits back into the segments a query writes.
		const wxString name = descriptor->GetName();
		wxString segment;
		for (size_t i = 0; i < name.length(); ++i) {
			if (name[i] == wxT('.')) { source.m_path.push_back(segment); segment.clear(); }
			else                      { segment += name[i]; }
		}
		if (!segment.IsEmpty())
			source.m_path.push_back(segment);

		source.m_presentation = source.Text();
		out.push_back(std::move(source));
	}

	std::sort(out.begin(), out.end(),
		[](const ibQueryConstructorSource& a, const ibQueryConstructorSource& b) {
			return a.Text().CmpNoCase(b.Text()) < 0;
		});
	return out;
}

std::vector<ibQueryConstructorSource> ibQueryConstructorModel::GetTempSources(
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
		const wxString& name = statement.m_select->m_intoTemp.IsEmpty()
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
ibQueryConstructorField ibQueryConstructorModel::FieldOfPath(const ibQuerySelect& select,
                                                             const std::vector<wxString>& path,
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
	return WalkPath(fields, std::vector<wxString>(path.begin() + first, path.end()));
}

ibQueryConstructorField ibQueryConstructorModel::WalkPath(const std::vector<ibQueryConstructorField>& fields,
                                                         const std::vector<wxString>& path) const
{
	for (size_t head = path.size(); head > 0; --head) {
		wxString name = path.front();
		for (size_t i = 1; i < head; ++i)
			name += wxT(".") + path[i];
		const auto start = std::find_if(fields.begin(), fields.end(),
			[&name](const ibQueryConstructorField& field) { return field.m_name.IsSameAs(name, false); });
		if (start != fields.end())
			return WalkFrom(*start, std::vector<wxString>(path.begin() + head, path.end()));
	}
	return ibQueryConstructorField();   // a name this model cannot resolve — silence, never a guess
}

// THE HOPS. Each segment must be found among the fields of the level above it; a segment that is not a
// reference ends the walk, because there is nothing behind it to look in.
ibQueryConstructorField ibQueryConstructorModel::WalkFrom(const ibQueryConstructorField& from,
                                                         const std::vector<wxString>& hops) const
{
	ibQueryConstructorField leaf = from;
	for (const wxString& hop : hops) {
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

ibClassID ibQueryConstructorModel::ReferenceOfPath(const ibQuerySelect& select,
                                                   const std::vector<wxString>& path,
                                                   const ibQueryPackage& package,
                                                   size_t beforeStatement) const
{
	return FieldOfPath(select, path, package, beforeStatement).m_referenceClsid;
}

ibTypeDescription ibQueryConstructorModel::TypeOfPath(const ibQuerySelect& select,
                                                      const std::vector<wxString>& path,
                                                      const ibQueryPackage& package,
                                                      size_t beforeStatement) const
{
	// EMPTY IS "UNKNOWN", never "no type" — and a host that reads it as unknown offers everything
	// rather than narrowing on a guess.
	return FieldOfPath(select, path, package, beforeStatement).m_type;
}

std::vector<ibQueryConstructorField> ibQueryConstructorModel::FieldsOfSelect(
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
			if (of.m_icon.IsOk())
				field.m_icon = of.m_icon;   // the column's own picture, when it has one
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

std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetFields(
	const ibQuerySource& source, const ibQueryPackage& package, size_t beforeStatement) const
{
	// Every field carries the SOURCE it came out of — stamped once, here, so the shell never has to
	// work it back out of a qualified name (which a dot-walk path makes ambiguous anyway).
	struct Stamp {
		const wxString label;
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

	ibQueryableFactory* factory = Factory();
	if (factory == nullptr)
		return {};

	// A REAL SOURCE answers through its descriptor — the very call a dynamic list fills its columns
	// with. The constructor asks the source exactly as a form does; there is one answer to "which
	// fields does this source have" and this is it.
	const wxString ns = source.m_name[0];
	wxString name = source.m_name[1];
	for (size_t i = 2; i < source.m_name.size(); ++i)
		name += wxT(".") + source.m_name[i];

	std::vector<ibQueryConstructorField> out;
	for (ibQueryableSourceDescriptor* descriptor : factory->GetDescriptors()) {
		if (descriptor == nullptr || !descriptor->GetNamespace().IsSameAs(ns, false)
		    || !descriptor->GetName().IsSameAs(name, false))
			continue;

		// ⭐ ASKED WITH THE CALL'S ARGUMENTS, because for a parameterized table they decide the columns
		// (a register's turnovers reads at the granularity its periodicity names). Only the arguments
		// that are KNOWN HERE count — one written as `&Period` has no value until the query runs, and
		// the source then answers with everything it can offer, which is the honest shape for "not
		// decided yet".
		//
		// ⭐⭐ A CHOICE IS WRITTEN AS A BARE WORD, and it parses as a COLUMN. `Turnovers(&From, &To,
		// Record)` says Record the way the language says every keyword — unquoted — so the parser
		// hands back an identifier, not a literal. Read only literals and the periodicity is silently
		// "not given": the window then offered the widest shape while the query asked for the
		// narrowest, and picking `Record` changed nothing on screen.
		//
		// This is the SAME rule the lowering applies (queryLowering, the source-argument walk): a
		// single-segment identifier in a slot whose parameter declares CHOICES is that word. Two
		// readers of one sentence, and they now read it the same way.
		std::vector<ibQuerySourceParameter> declared;
		descriptor->DescribeParameters(declared);

		std::vector<ibValue> args;
		for (size_t i = 0; i < source.m_args.size(); ++i) {
			const ibQueryAstExprPtr& arg = source.m_args[i];
			if (!arg) {
				args.push_back(ibValue());
				continue;
			}
			if (arg->m_kind == ibQueryAstExprKind::Literal) {
				args.push_back(arg->m_literal);
				continue;
			}
			if (i < declared.size() && !declared[i].m_choices.empty()
			    && arg->m_kind == ibQueryAstExprKind::Column && arg->m_path.size() == 1) {
				ibValue word;
				word.SetString(arg->m_path.front());
				args.push_back(word);
				continue;
			}
			args.push_back(ibValue());
		}

		ibSourceDataObject::ibSourceExplorer explorer;
		descriptor->FillSourceExplorer(explorer, args);

		for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
			const ibSourceDataObject::ibSourceExplorer* node = explorer.GetHelper(i);
			if (node == nullptr || node->IsTableSection())
				continue;   // a section is a table of its own, not a field of this one

			// WALKABLE IS "ANY REFERENCE", not "exactly one" — see FieldOfExplorer. A composite names
			// several types and the engine walks it (one join sub-tree per alternative, the leaf
			// COALESCEd), so what is offered matches what the query can then do.
			out.push_back(FieldOfExplorer(*node));
		}
		break;
	}
	return stamp(std::move(out));
}

// ONE LEVEL DOWN A REFERENCE. No lookup table: a reference's clsid is CONSTRUCTIVE — the low 56
// bits ARE the metaID of what it refers to — so the referenced source is simply the one the factory
// already holds under that id. The walk stays a walk (docs §4, rule 1): a metatype registered
// tomorrow unfolds the day it vends a descriptor, with nothing edited here.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceFields(ibClassID clsid,
                                                                                const wxString& sourceLabel) const
{
	std::vector<ibQueryConstructorField> out;
	if (clsid == 0 || !IsReference(clsid) || clsid_is_any(clsid))
		return out;   // not a reference, or a barrier (`AnyRef`, `CatalogRef`): no one table is behind it

	ibQueryableFactory* factory = Factory();
	if (factory == nullptr)
		return out;

	ibQueryableSourceDescriptor* descriptor =
		factory->ResolveDescriptorById(static_cast<ibMetaID>(clsid_metaID(clsid)));
	if (descriptor == nullptr)
		return out;   // a type with no queryable (a report, a data processor) — not a table

	ibSourceDataObject::ibSourceExplorer explorer;
	descriptor->FillSourceExplorer(explorer);

	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const ibSourceDataObject::ibSourceExplorer* node = explorer.GetHelper(i);
		if (node == nullptr || node->IsTableSection())
			continue;

		ibQueryConstructorField field = FieldOfExplorer(*node);
		field.m_source = sourceLabel;   // the walk stays under the table it started from
		out.push_back(std::move(field));
	}
	return out;
}

// ONE ENTRY PER ALTERNATIVE — see the note on the declaration. Named as a query names the type
// (`Document.GoodsIssue`), which is both what a tree shows and what a person would have to write to
// cast to it.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceBranches(const ibTypeDescription& typeDesc) const
{
	std::vector<ibQueryConstructorField> out;

	ibQueryableFactory* factory = Factory();
	if (factory == nullptr)
		return out;

	for (const ibClassID& clsid : typeDesc.GetClsidList()) {
		if (!IsReference(clsid) || clsid_is_any(clsid))
			continue;   // a barrier names no table; its metaID, 0, is what a source with no metaobject answers

		ibQueryableSourceDescriptor* descriptor =
			factory->ResolveDescriptorById(static_cast<ibMetaID>(clsid_metaID(clsid)));
		if (descriptor == nullptr)
			continue;   // a type with no queryable (a report, a data processor) — not a table

		ibQueryConstructorField branch;
		branch.m_name         = descriptor->GetNamespace() + wxT(".") + descriptor->GetName();
		branch.m_presentation = branch.m_name;
		branch.m_referenceClsid = clsid;
		branch.m_reference      = true;
		branch.m_type.SetDefaultMetaType(clsid);   // the branch IS that one type, so it unfolds like any reference

		out.push_back(std::move(branch));
	}

	return out;
}

ibQueryAstExprPtr ibQueryConstructorModel::CastTo(ibQueryAstExprPtr value, const ibTypeDescription& type) const
{
	if (ibQueryAstExprPtr primitive = ibQueryMakeCast(value, type))
		return primitive;
	const std::vector<ibQueryConstructorField> branches =
		type.GetClsidCount() == 1 ? GetReferenceBranches(type) : std::vector<ibQueryConstructorField>();
	if (branches.size() != 1)
		return value;
	auto cast = ibQueryAstExpr::Make(ibQueryAstExprKind::Cast);
	cast->m_arg = std::move(value);
	const wxString& table = branches.front().m_name;   // `Catalog.Goods`
	cast->m_path = { table.BeforeFirst(wxT('.')), table.AfterFirst(wxT('.')) };
	return cast;
}

std::vector<ibQueryConstructorField> ibQueryConstructorModel::FieldsTakenFrom(const ibQuerySelect& reader,
                                                                               const ibQuerySource& source) const
{
	const wxString table = ibQuerySourceName(source);
	std::vector<ibQueryConstructorField> out;
	for (const ibQueryProjection& projection : reader.m_projections) {
		const ibQueryAstExprPtr& e = projection.m_expr;
		const bool cast = e && e->m_kind == ibQueryAstExprKind::Cast;
		const ibQueryAstExprPtr& column = cast ? e->m_arg : e;
		if (!column || column->m_kind != ibQueryAstExprKind::Column || column->m_arg || column->m_path.size() != 2
		    || !column->m_path.front().IsSameAs(table, false))
			continue;
		const wxString& name = column->m_path.back();
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

ibTypeDescription ibQueryConstructorModel::TypeOfCast(const ibQueryAstExpr& cast) const
{
	ibTypeDescription type;
	if (ibQueryCastType(cast, type))
		return type;
	type = ibTypeDescription();
	if (cast.m_kind != ibQueryAstExprKind::Cast || cast.m_path.size() < 2)
		return type;
	ibQueryableFactory* const factory = Factory();
	wxString name = cast.m_path[1];
	for (size_t i = 2; i < cast.m_path.size(); ++i)
		name += wxT(".") + cast.m_path[i];
	const ibBackendQueryable* const table = factory != nullptr ? factory->Resolve(cast.m_path[0], name) : nullptr;
	const ibValueMetaObjectGenericData* const meta = table != nullptr ? table->GetSourceMetaObject() : nullptr;
	ibValue emptyRef;
	if (meta != nullptr && meta->ResolveQueryConstant(ibRefMember::EmptyRef, emptyRef) && emptyRef.GetClassType() != 0)
		type.SetDefaultMetaType(emptyRef.GetClassType());
	return type;
}

// EVERY ALTERNATIVE OF A TYPE, MERGED BY NAME — see the note on the declaration. The merge is the
// one the query performs on the same walk: a name carried by several alternatives is ONE field,
// because `Recorder.Date` is one column whichever document a row points at.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetReferenceFields(const ibTypeDescription& typeDesc,
                                                                                const wxString& sourceLabel) const
{
	std::vector<ibQueryConstructorField> out;

	for (const ibClassID& clsid : typeDesc.GetClsidList()) {
		if (!IsReference(clsid))
			continue;   // a non-reference alternative of a composite carries no fields behind it

		for (ibQueryConstructorField& field : GetReferenceFields(clsid, sourceLabel)) {

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

			same->m_reference = HasReference(same->m_type.GetClsidList());
			same->m_referenceClsid = SingleReferenceOf(same->m_type.GetClsidList());
			same->m_available = same->m_available || field.m_available;   // offered while any branch offers it
		}
	}

	return out;
}

// THE DESCRIPTOR BEHIND A SOURCE, or null. Both parameter questions need it, and neither should
// repeat the namespace/name split that finding it takes.
static ibQueryableSourceDescriptor* DescriptorOf(ibQueryableFactory* factory, const ibQuerySource& source)
{
	if (factory == nullptr || source.m_name.size() < 2 || source.m_subquery)
		return nullptr;

	const wxString ns = source.m_name[0];
	wxString name = source.m_name[1];
	for (size_t i = 2; i < source.m_name.size(); ++i)
		name += wxT(".") + source.m_name[i];   // `Register.Stock.Balance` — the virtual table is the third segment

	for (ibQueryableSourceDescriptor* descriptor : factory->GetDescriptors())
		if (descriptor != nullptr && descriptor->GetNamespace().IsSameAs(ns, false)
		    && descriptor->GetName().IsSameAs(name, false))
			return descriptor;
	return nullptr;
}

std::vector<ibQuerySourceParameter> ibQueryConstructorModel::GetSourceParameters(const ibQuerySource& source) const
{
	std::vector<ibQuerySourceParameter> out;
	if (ibQueryableSourceDescriptor* descriptor = DescriptorOf(Factory(), source))
		descriptor->DescribeParameters(out);
	return out;
}

std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetConditionFields(const ibQuerySource& source, const wxString& slot) const
{
	std::vector<ibQueryConstructorField> out;
	ibQueryableSourceDescriptor* descriptor = DescriptorOf(Factory(), source);
	if (descriptor == nullptr)
		return out;

	// ⚠ THE CONDITION'S OWN SET, not the source's output. A balance RETURNS its resources and is
	// FILTERED BY its dimensions only — the engine reading a balance sees nothing but the
	// dimensions, so offering a resource here would offer a filter it cannot honour.
	ibSourceDataObject::ibSourceExplorer explorer;
	descriptor->FillConditionExplorer(explorer, slot);

	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const ibSourceDataObject::ibSourceExplorer* node = explorer.GetHelper(i);
		if (node == nullptr || node->IsTableSection())
			continue;

		out.push_back(FieldOfExplorer(*node));
	}
	return out;
}

std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetQualifiedFields(
	const ibQuerySource& source, const ibQueryPackage& package, size_t beforeStatement) const
{
	// The prefix a query actually writes: the alias where the author gave one, else the source's
	// own last segment (Catalog.Products -> Products), which is what the lowering resolves by.
	const wxString prefix = ibQuerySourceName(source);

	std::vector<ibQueryConstructorField> out = GetFields(source, package, beforeStatement);
	if (prefix.IsEmpty())
		return out;

	// ⚠ ONLY THE NAME IS QUALIFIED — never the PRESENTATION. The name is what the query TEXT must
	// carry to be unambiguous; the presentation is what a person reads, and they read it under a
	// node that already says which table it is. Prefixing both produced `Catalog1 › Catalog1.Code`,
	// which is the table's name twice on one line and the field's name pushed off the right — the
	// whole reason the field trees stopped being readable once a second table joined.
	for (ibQueryConstructorField& field : out)
		field.m_name = prefix + wxT(".") + field.m_name;
	return out;
}

// ------------------------------------------------------------------------------------------------
// THE FIELDS A QUERY TEXT OFFERS — see the header. Lifted out of ibValueDataComposition, which is
// where it used to live and where it made every editor of a query hold a running one.
// ------------------------------------------------------------------------------------------------

#include "queryParser.h"                 // ibQueryParser — the text is parsed here and nowhere else
#include "queryable.h"                   // ibSourceMetaDataScope — names resolve against THIS config
#include "queryLowering.h"               // PlacePackageLinks — where a linked selection stands, asked ONCE

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

wxString ibQueryTextWithoutRoles(const wxString& text)
{
	if (text.IsEmpty())
		return text;
	ibQueryPackage package;
	try { package = ibQueryParser().ParsePackage(text); }
	catch (const ibCoreException&) { return text; }   // half-typed: whoever reads it says why, in the parser's words
	return ibQueryDropRoles(package) ? ibRenderQueryPackage(package) : text;
}

std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const wxString& text,
	const ibMetaData* metaData, wxString* error)
{
	std::vector<ibQueryConstructorField> fields;
	if (error != nullptr)
		error->Clear();

	if (text.IsEmpty())
		return fields;

	// The names resolve against the CONFIGURATION HANDED IN — never the active one, which in the
	// designer is not necessarily this query's (two are open at once).
	const ibSourceMetaDataScope scope(metaData);
	try {
		ibQueryParser parser;
		ibQueryPackage package = parser.ParsePackage(text);
		if (package.m_statements.empty())
			return fields;
		ibQueryDropRoles(package);   // a composition's roles are its own word — see ibQueryTextWithoutRoles

		// ⭐⭐ A LINKED PACKAGE OFFERS EVERY RELATED SELECTION'S FIELDS, QUALIFIED BY ITS NAME.
		//
		// There is no "last statement" to read when the statements are related: what the composition
		// stands on is the FINAL query, whose sources ARE the named selections
		// (docs/private/query-language-arc.md § 24.4b). So the fields are the union of theirs, written the
		// way a path over two selections has to be written — `Sales.Qty` — which is the very job
		// `ONTO` exists for: settling a clash of names, nothing else.
		//
		// ⚠ AND ONLY THE SELECTIONS THE LINKS ACTUALLY PLACED. One that relates to nothing is not in
		// the final query, so offering its fields would offer a path that cannot resolve.
		if (!package.m_links.empty()) {
			std::vector<wxString> declared;
			for (const ibQueryAstStatement& statement : package.m_statements)
				if (statement.m_select && !statement.m_select->m_ontoName.IsEmpty())
					declared.push_back(statement.m_select->m_ontoName);

			const ibQueryLowering::FromTree tree =
				ibQueryLowering::PlacePackageLinks(package.m_links, declared);

			std::vector<wxString> placed;
			if (!tree.m_head.IsEmpty()) {
				placed.push_back(tree.m_head);
				for (const ibQueryLowering::JoinStep& step : tree.m_steps)
					placed.push_back(step.m_name);
			}

			const ibQueryConstructorModel model(metaData);
			for (const wxString& name : placed) {
				for (size_t i = 0; i < package.m_statements.size(); ++i) {
					const ibQuerySelectPtr select = package.m_statements[i].m_select;
					if (!select || !select->m_ontoName.IsSameAs(name, false))
						continue;
					// RESOLVED AS OF ITS MAKER, like every other named result (GetFields).
					for (ibQueryConstructorField field : model.FieldsOfSelect(*select, package, i)) {
						field.m_source = name;
						field.m_name   = name + wxT(".") + field.m_name;
						fields.push_back(std::move(field));
					}
					break;
				}
			}
			if (!fields.empty())
				return fields;
		}

		// THE LAST STATEMENT is the one that produces the result — a package builds temp tables and
		// reads them at the end, so its fields are the ones a resource or a level is written over.
		// Earlier statements are handed in as context so a temp table's own fields resolve.
		const size_t last = package.m_statements.size() - 1;
		const ibQuerySelectPtr select = package.m_statements[last].m_select;
		if (!select)
			return fields;

		const ibQueryConstructorModel model(metaData);
		fields = model.FieldsOfSelect(*select, package, last);
	}
	catch (const ibCoreException& err) {
		// Half-typed text offers nothing YET — an empty list, and the complaint only if asked for.
		fields.clear();
		if (error != nullptr)
			*error = err.GetErrorDescription();
	}
	return fields;
}
