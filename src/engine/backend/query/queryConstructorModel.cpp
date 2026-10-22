////////////////////////////////////////////////////////////////////////////
//	Description : L4-1 — the query constructor's MODEL half (queryConstructorModel.h)
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorModel.h"

#include "queryable.h"                   // ibBackendQueryable::GetSourceMetaObject — a table's own reference type
#include "backend/metaCollection/genericData.h"   // ResolveQueryConstant — …typed by its empty reference
#include "backend/appData.h"             // ibApplicationInstance::GetQueryableFactory (no config open)
#include "backend/metaData.h"            // ibMetaData::GetSourceFactory — the config the query runs on behalf of
#include "backend/srcDataObject.h"       // ibSourceDataObject::ibSourceExplorer — what a source answers its fields with

#include <algorithm>

// ⭐ ONE FIELD OF A SOURCE, AS THE CONSTRUCTOR OFFERS IT — written once for the three lists that offer
// fields (a source's own, one hop down a reference, a virtual table's condition), which each spelled it
// out for themselves.
//
// ⭐⭐ SHOWN BY ITS NAME, the one the query writes (Max, 2026-09-10: "in the query constructor it must
// be the name, not the synonym"). The sources were already listed by name and the fields by synonym,
// so a person picked a Russian synonym in the tree and read `Employee` in the text beside it — two
// vocabularies for one field, and the synonym in whichever language happened to be filled. The query
// constructor is where the query language is written; its tree speaks that language.
static ibQueryConstructorField FieldOfExplorer(const ibQueryConstructorModel& model,
                                               const ibSourceDataObject::ibSourceExplorer& node)
{
	ibQueryConstructorField field;
	field.m_name           = node.GetSourceName();
	field.m_presentation   = node.GetSourceName();
	// ⭐ WHAT IT HOLDS (GetTypeValueDesc), the types the query walks it by (ResolveReferenceTargets): a field
	// declared `AnyRef` unfolds into every reference of the configuration, a characteristic into its chart's.
	// Read by the declaration, both were offered as a leaf, or as a [+] with nothing behind it.
	// A reference field can be dot-walked further (Supplier.Region.Country) — the shell shows it with
	// a [+] and asks again with the leaf's own source.
	field.m_type           = node.GetTypeValueDesc();
	model.ResolveReference(field);
	field.m_picture        = node.GetSourcePicture();   // the column's own picture, asked not deduced
	field.m_available      = node.GetColumn() == nullptr || node.GetColumn()->IsAvailable();
	// …what a person calls it, and what it is in a balance — for the hosts that fill a field in from them.
	field.m_caption        = node.GetSourceSynonym();
	field.m_balanceRole    = node.GetColumn() != nullptr ? node.GetColumn()->GetBalanceRole() : ibBalanceRole::None;
	return field;
}

// THE DESCRIPTOR BEHIND A SOURCE, or null. The table's fields and both parameter questions need it, and
// none should repeat the namespace/name split that finding it takes.
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

		out.push_back(FieldOfExplorer(*this, *node));
	}
	return out;
}

ibClassID ibQueryConstructorModel::ReferenceOfTable(const std::vector<wxString>& path) const
{
	if (path.size() != 2)
		return 0;   // a virtual table — `AccumulationRegister.Stock.Balance` — is no table a reference points at
	ibQueryableFactory* const factory = Factory();
	const ibBackendQueryable* const table = factory != nullptr ? factory->Resolve(path[0], path[1]) : nullptr;
	const ibValueMetaObjectGenericData* const meta = table != nullptr ? table->GetSourceMetaObject() : nullptr;
	ibValue emptyRef;
	if (meta != nullptr && meta->ResolveQueryConstant(ibRefMember::EmptyRef, emptyRef))
		return emptyRef.GetClassType();
	return 0;
}

// Named as a query names the type (`Document.GoodsIssue`), which is both what a tree shows and what a
// person would have to write to cast to it.
wxString ibQueryConstructorModel::TableOfReference(ibClassID reference) const
{
	ibQueryableFactory* const factory = Factory();
	const std::vector<ibMetaID> tables = ConvertToMetaIds({ reference });
	const ibQueryableSourceDescriptor* const descriptor =
		factory != nullptr && tables.size() == 1 ? factory->ResolveDescriptorById(tables.front()) : nullptr;
	return descriptor != nullptr ? descriptor->GetNamespace() + wxT(".") + descriptor->GetName() : wxString();
}

// A REAL SOURCE answers through its descriptor — the very call a dynamic list fills its columns
// with. The constructor asks the source exactly as a form does; there is one answer to "which
// fields does this source have" and this is it.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetTableFields(const ibQuerySource& source) const
{
	std::vector<ibQueryConstructorField> out;
	ibQueryableSourceDescriptor* const descriptor = DescriptorOf(Factory(), source);
	if (descriptor == nullptr)
		return out;

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
			args.push_back(ibValue(arg->m_literal));
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

		// WALKABLE IS "ANY REFERENCE", not "exactly one" — see ResolveReference. A composite names
		// several types and the engine walks it (one join sub-tree per alternative, the leaf
		// COALESCEd), so what is offered matches what the query can then do.
		out.push_back(FieldOfExplorer(*this, *node));
	}
	return out;
}

// ONE LEVEL DOWN A REFERENCE: the table the factory holds under the id the reference names. The walk stays
// a walk (docs §4, rule 1): a metatype registered tomorrow unfolds the day it vends a descriptor, with
// nothing edited here.
std::vector<ibQueryConstructorField> ibQueryConstructorModel::GetTableFields(ibMetaID table) const
{
	std::vector<ibQueryConstructorField> out;
	ibQueryableFactory* factory = Factory();
	if (factory == nullptr)
		return out;

	ibQueryableSourceDescriptor* descriptor = factory->ResolveDescriptorById(table);
	if (descriptor == nullptr)
		return out;   // a type with no queryable (a report, a data processor) — not a table

	ibSourceDataObject::ibSourceExplorer explorer;
	descriptor->FillSourceExplorer(explorer);

	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const ibSourceDataObject::ibSourceExplorer* node = explorer.GetHelper(i);
		if (node == nullptr || node->IsTableSection())
			continue;
		out.push_back(FieldOfExplorer(*this, *node));
	}
	return out;
}

// No lookup table: a reference's clsid is CONSTRUCTIVE — the low 56 bits ARE the metaID of what it refers
// to (clsid.h §dynamic) — so the table behind it is simply the one the factory already holds under that id.
//
// ⚠ WHAT CAN BE WALKED THROUGH, and it is NOT "the field has a type". Every typed attribute carries a
// clsid list — a string field's list holds the string's clsid — so "exactly one clsid" said yes to every
// field in the configuration and put an unfoldable [+] on all of them, behind which there was nothing.
// The question is whether a class is a REFERENCE, which the id answers by its kind alone (the high byte IS
// the kind, no metadata lookup). A barrier (`AnyRef`, `CatalogRef`) names no table: a field declared with
// one holds its members (FieldOfExplorer), and one with none to hold is a leaf.
std::vector<ibMetaID> ibQueryConstructorModel::ConvertToMetaIds(const std::vector<ibClassID>& clsids) const
{
	std::vector<ibMetaID> tables;
	ibQueryableFactory* const factory = Factory();
	if (factory == nullptr)
		return tables;
	for (const ibClassID& clsid : clsids) {
		if (!IsReference(clsid) || clsid_is_any(clsid))
			continue;
		const ibMetaID table = static_cast<ibMetaID>(clsid_metaID(clsid));
		if (factory->ResolveDescriptorById(table) != nullptr && std::find(tables.begin(), tables.end(), table) == tables.end())
			tables.push_back(table);
	}
	return tables;
}

// ------------------------------------------------------------------------------------------------
// THE FIELDS A QUERY TEXT OFFERS — see the header. Lifted out of ibValueDataComposition, which is
// where it used to live and where it made every editor of a query hold a running one.
// ------------------------------------------------------------------------------------------------

#include "core/query/queryParser.h"                 // ibQueryParser — the text is parsed here and nowhere else
#include "queryLowering.h"               // PlacePackageLinks — where a linked selection stands, asked ONCE

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
