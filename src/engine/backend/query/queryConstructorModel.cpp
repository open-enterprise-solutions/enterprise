////////////////////////////////////////////////////////////////////////////
//	Description : L4-1 — the query constructor's MODEL half (queryConstructorModel.h)
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorModel.h"

#include "queryRender.h"                 // ibRenderQueryExpr — an unaliased projection is named by its own text
#include "backend/appData.h"             // ibApplicationData::GetQueryableFactory (no config open)
#include "backend/metaData.h"            // ibMetaData::GetSourceFactory — the config the query runs on behalf of
#include "backend/srcDataObject.h"       // ibSourceDataObject::ibSourceExplorer — what a source answers its fields with

#include <algorithm>

// ⚠ WHAT CAN BE WALKED THROUGH, and it is NOT "the field has a type". Every typed attribute carries
// a clsid list — a string field's list holds the string's clsid — so "exactly one clsid" said yes
// to every field in the configuration and put an unfoldable [+] on all of them, behind which there
// was nothing. The question is whether that one type is a REFERENCE, which the id answers by its
// kind alone (clsid.h — the high byte IS the kind, no metadata lookup).
//
// A COMPOSITE reference (several clsids) is deliberately not walkable: it has no one set of fields
// behind it, and the lowering refuses a composite mid-segment for the same reason.
static ibClassID SingleReferenceOf(const std::vector<ibClassID>& clsids)
{
	return clsids.size() == 1 && IsReference(clsids.front()) ? clsids.front() : 0;
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
	return factory != nullptr ? factory : ibApplicationData::GetQueryableFactory();
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

		if (!statement.m_select || statement.m_select->m_intoTemp.IsEmpty())
			continue;

		ibQueryConstructorSource source;
		source.m_path.push_back(statement.m_select->m_intoTemp);
		source.m_presentation = statement.m_select->m_intoTemp;
		source.m_temp = true;
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

	// THE HOPS. Each segment must be found among the fields of the level above it; a segment that is
	// not a single-target reference ends the walk, because there is nothing behind it to look in.
	ibQueryConstructorField leaf;
	for (size_t i = first; i < path.size(); ++i) {
		const ibQueryConstructorField* found = nullptr;
		for (const ibQueryConstructorField& field : fields)
			if (field.m_name.IsSameAs(path[i], false)) { found = &field; break; }
		if (found == nullptr)
			return ibQueryConstructorField();   // a name this model cannot resolve — silence, never a guess

		leaf = *found;
		if (i + 1 < path.size()) {
			if (leaf.m_referenceClsid == 0)
				return ibQueryConstructorField();   // asked to walk THROUGH something that is not a reference
			fields = GetReferenceFields(leaf.m_referenceClsid);
		}
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
		if (projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Column) {
			field.m_referenceClsid = ReferenceOfPath(select, projection.m_expr->m_path,
			                                         package, beforeStatement);
			field.m_reference      = field.m_referenceClsid != 0;
		}
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

	// A TEMP TABLE is a bare name: find the statement that MADE it and read its projections. Same
	// answer as a nested table's, and for the same reason — a select is what defines both.
	if (source.m_name.size() == 1) {
		const size_t limit = std::min(beforeStatement, package.m_statements.size());
		for (size_t i = limit; i > 0; --i) {
			const ibQueryAstStatement& statement = package.m_statements[i - 1];
			if (statement.m_select && statement.m_select->m_intoTemp.IsSameAs(source.m_name[0], false))
				// ⚠ RESOLVED AS OF THE MAKER, not as of us: the making statement sees only what came
				// BEFORE it, which is also what stops a temp table resolving through itself.
				return stamp(FieldsOfSelect(*statement.m_select, package, i - 1));
		}
		return {};
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

			ibQueryConstructorField field;
			field.m_name         = node->GetSourceName();
			field.m_presentation = node->GetSourceSynonym().IsEmpty() ? node->GetSourceName() : node->GetSourceSynonym();
			// A reference field can be dot-walked further (Supplier.Region.Country) — the shell
			// shows it with a [+] and asks again with the leaf's own source.
			//
			// A SINGLE target is what can be walked. A composite reference names several types and
			// has no one set of fields behind it; the lowering refuses a composite mid-segment for
			// the same reason, so it stays a leaf here rather than offering a walk that would then
			// be rejected.
			field.m_referenceClsid = SingleReferenceOf(node->GetClsidList());
			field.m_reference      = field.m_referenceClsid != 0;
			field.m_type           = node->GetTypeDesc();
			field.m_icon           = node->GetSourceIcon();   // the column's own picture, asked not deduced
			out.push_back(std::move(field));
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
	if (clsid == 0 || !IsReference(clsid))
		return out;   // not a reference: nothing is behind it

	ibQueryableFactory* factory = Factory();
	if (factory == nullptr)
		return out;

	ibQueryableSourceDescriptor* descriptor =
		factory->ResolveDescriptorById(static_cast<ibMetaID>(clsid & kIbClsidBodyMask));
	if (descriptor == nullptr)
		return out;   // a type with no queryable (a report, a data processor) — not a table

	ibSourceDataObject::ibSourceExplorer explorer;
	descriptor->FillSourceExplorer(explorer);

	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const ibSourceDataObject::ibSourceExplorer* node = explorer.GetHelper(i);
		if (node == nullptr || node->IsTableSection())
			continue;

		ibQueryConstructorField field;
		field.m_name         = node->GetSourceName();
		field.m_presentation = node->GetSourceSynonym().IsEmpty() ? node->GetSourceName() : node->GetSourceSynonym();
		field.m_referenceClsid = SingleReferenceOf(node->GetClsidList());
		field.m_reference      = field.m_referenceClsid != 0;
		field.m_type           = node->GetTypeDesc();
		field.m_source         = sourceLabel;   // the walk stays under the table it started from
		out.push_back(std::move(field));
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

		ibQueryConstructorField field;
		field.m_name           = node->GetSourceName();
		field.m_presentation   = node->GetSourceSynonym().IsEmpty() ? node->GetSourceName() : node->GetSourceSynonym();
		field.m_referenceClsid = SingleReferenceOf(node->GetClsidList());
		field.m_reference      = field.m_referenceClsid != 0;
		field.m_type           = node->GetTypeDesc();
		field.m_icon           = node->GetSourceIcon();
		out.push_back(std::move(field));
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
