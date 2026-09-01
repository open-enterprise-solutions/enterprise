////////////////////////////////////////////////////////////////////////////
//	Description : the type tools — the vocabulary this platform speaks
////////////////////////////////////////////////////////////////////////////
//
// WHY THIS EXISTS. Everything a caller writes — a variable's declared type, an
// attribute's type, a query's CAST — is spelled with a name that means
// something only here. Without a way to ASK, a generating caller invents names
// that look right and are not, and finds out at compile time if it is lucky.
//
// The answer comes from the class factory, which is the one place every type
// registers itself (VALUE_TYPE_REGISTER, METADATA_TYPE_REGISTER and their
// family). No list is kept here: a type added tomorrow is answerable the day it
// registers, and a type that stops existing stops being offered.
//
// ⭐ IT ANSWERS THE CLASSIFICATION, not a bare list of words. A caller needs to
// know that "Catalog" is a metatype it can create and "Array" is a value it can
// construct — a distinction it would otherwise have to guess from the spelling.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/compiler/value.h"
#include "backend/compiler/typeCtor.h"
#include "backend/metadataConfiguration.h"   // the configuration's own type registry
#include "backend/objCtor.h"

namespace {

// The factory's kinds, in the caller's words. Kept as a table so the tool that
// FILTERS and the answer that LABELS cannot disagree — they read the same rows.
struct KindRow {
	const wxChar*     m_word;
	ibCtorObjectType  m_kind;
};

const KindRow s_kinds[] = {
	{ wxT("primitive"), ibCtorObjectType_object_primitive },
	{ wxT("value"),     ibCtorObjectType_object_value     },
	{ wxT("control"),   ibCtorObjectType_object_control   },
	{ wxT("system"),    ibCtorObjectType_object_system    },
	{ wxT("enum"),      ibCtorObjectType_object_enum      },
	{ wxT("context"),   ibCtorObjectType_object_context   },
	{ wxT("metadata"),  ibCtorObjectType_object_metadata  },
	{ wxT("metaValue"), ibCtorObjectType_object_meta_value },
};

void AppendKind(const KindRow& row, std::vector<ibDataValue>& types)
{
	for (const ibCtorAbstractType* ctor : ibValue::GetListCtorsByType(row.m_kind)) {

		if (ctor == nullptr)
			return;

		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		entry->SetValue(wxT("name"), ctor->GetClassName());
		entry->SetValue(wxT("kind"), wxString(row.m_word));
		entry->AddField(wxT("id"), ibDataValue::UInt((u64)ctor->GetClassType()));

		types.push_back(ibDataValue::Child(entry));
	}
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		_("The type's name as type_list gives it — 'Array', 'Structure', 'ValueTable'…"), /*required*/ true);
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// type_list
//---------------------------------------------------------------------------
class ibMcpToolTypeList : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("type_list"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("looking through the available types");
	}

	wxString GetDescription() const override
	{
		return _("Every type this platform knows, with what each one IS: a primitive, a value "
			"class you can construct, a metadata kind you can create, a control, an "
			"enumeration. Ask before writing a type name — the vocabulary is this "
			"configuration's, not a general one.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString wanted = params.GetValue<wxString>(wxT("kind"));

		std::vector<ibDataValue> types;
		bool matched = false;

		for (const KindRow& row : s_kinds) {
			if (!wanted.IsEmpty() && !wanted.IsSameAs(row.m_word, false))
				continue;

			matched = true;
			AppendKind(row, types);
		}

		// A KIND NOBODY HAS IS A REFUSAL, not an empty answer: "there are no
		// controls" and "control is not a word here" are different, and only one
		// of them means try again differently.
		if (!matched && !wanted.IsSameAs(wxT("configuration"), false)) {
			refusal = wxString::Format(
				_("'%s' is not a kind of type. Try: primitive, value, control, system, enum, "
				  "context, metadata, metaValue, configuration."), wanted);
			return false;
		}

		// ⚠ AND THE CONFIGURATION'S OWN, which the loop above cannot see.
		//
		// The global factory holds what the ENGINE registers; a configuration's
		// types — CatalogRef.Goods, a document's object, a register's record set —
		// live in the metadata's own registry, because they exist only against the
		// tree that declared them. Asked only of the factory, this answered with
		// 43 metatypes and not one type of the configuration in front of it, while
		// `CatalogRef.Goods` resolved perfectly well by name: the vocabulary was
		// there and the listing did not show it.
		if (wanted.IsEmpty() || wanted.IsSameAs(wxT("configuration"), false)) {

			if (activeMetaData != nullptr && activeMetaData->IsConfigOpen()) {
				for (const ibCtorMetaValueType* ctor : activeMetaData->GetListCtorsByType()) {

					if (ctor == nullptr)
						continue;

					std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					entry->SetValue(wxT("name"), ctor->GetClassName());
					entry->SetValue(wxT("kind"), wxString(wxT("configuration")));
					entry->AddField(wxT("id"), ibDataValue::UInt((u64)ctor->GetClassType()));

					types.push_back(ibDataValue::Child(entry));
				}
			}
		}

		result.AddField(wxT("types"), ibDataValue::Array(types));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolTypeList);

//---------------------------------------------------------------------------
// type_members
//---------------------------------------------------------------------------
//
// WHAT AN OBJECT IS MADE OF — the same question the editor answers after a dot,
// asked without a caret. A value carries its own member table: methods with the
// call form the platform itself would show, their arity, and whether each
// RETURNS anything (a function) or does not (a procedure); properties beside
// them.
//
// ⚠ IT CREATES ONE TO ASK IT. Members are per-instance here — a type fills its
// table when it is built — so the only honest way to answer is to build one and
// look. Anything that cannot be built without arguments says so instead of
// answering an empty list, because "no members" and "I could not make one" are
// different facts and only one of them means stop asking.
//
class ibMcpToolTypeMembers : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("type_members"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(_("looking at what %s can do"),
			ArgName().Text(params));
	}

	wxString GetDescription() const override
	{
		return _("What a type offers: its methods with their call form and whether each returns a "
			"value, and its properties. This is what the editor shows after a dot, asked by "
			"name instead of by cursor position.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgName() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString name = ArgName().Text(params);
		if (name.IsEmpty()) {
			refusal = _("No type named.");
			return false;
		}

		if (ibValue::GetAvailableCtor(name) == nullptr) {
			refusal = wxString::Format(
				_("'%s' is not a type this platform knows. Use type_list to see what is."), name);
			return false;
		}

		ibValue value;
		try {
			value = ibValue::CreateObject(name);
		}
		catch (...) {
			refusal = wxString::Format(
				_("'%s' cannot be built without arguments, so its members cannot be listed this "
				  "way."), name);
			return false;
		}

		std::vector<ibDataValue> methods;
		for (long i = 0; i < value.GetNMethods(); i++) {

			// (No scope filter on METHODS, matching what the editor does after a
			//  dot: it drops scope-local PROPERTIES and lists every method. The
			//  member table knows IsMethodScoped, but the value does not forward
			//  it — and inventing a way round that would make this answer differ
			//  from the one a person sees, which is the whole thing to avoid.)
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), value.GetMethodName(i));

			// THE CALL FORM the platform itself would show — the one place a
			// caller learns what the arguments MEAN, since the table keeps their
			// count but not their names.
			const wxString helper = value.GetMethodHelper(i);
			if (!helper.IsEmpty())
				entry->SetValue(wxT("signature"), helper);

			entry->AddField(wxT("parameters"), ibDataValue::Int((s64)value.GetNParams(i)));
			// Function or procedure — not cosmetic: calling a procedure where a
			// value is expected is a compile error, and the two are told apart
			// only here.
			entry->AddField(wxT("returnsValue"), ibDataValue::Bool(value.HasRetVal(i)));

			methods.push_back(ibDataValue::Child(entry));
		}

		std::vector<ibDataValue> properties;
		for (long i = 0; i < value.GetNProps(); i++) {
			if (value.IsPropScoped(i))
				continue;
			properties.push_back(ibDataValue::String(value.GetPropName(i)));
		}

		result.SetValue(wxT("name"), name);
		result.AddField(wxT("methods"), ibDataValue::Array(methods));
		result.AddField(wxT("properties"), ibDataValue::Array(properties));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolTypeMembers);

//---------------------------------------------------------------------------
// linq_methods
//---------------------------------------------------------------------------
//
// THE PIPELINE VOCABULARY. A query can be written as a chain over a source
// rather than as text, and the operations that chain admits are a closed set
// the engine already keeps — with a one-line description each, written for the
// editor's tooltip (ibValue::GetLinqMethodTable).
//
// It is answered from that table and not from a list here, for the usual
// reason: an operation added to the engine appears in this answer with nothing
// edited, and one removed stops being offered.
//
class ibMcpToolLinqMethods : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("linq_methods"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("looking at the collection operations");
	}

	wxString GetDescription() const override
	{
		return _("The pipeline operations a query written as a chain may use — Where, Select, "
			"GroupBy, Join and the rest — each with one line saying what it does and what shape "
			"of function it takes. This is the whole set; anything else is not one.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& WXUNUSED(params), ibDataNode& result, wxString& WXUNUSED(refusal)) const override
	{
		std::vector<ibDataValue> methods;

		for (const ibValue::ibLinqMethodInfo& info : ibValue::GetLinqMethodTable()) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), wxString(info.name));
			entry->SetValue(wxT("does"), wxString(info.helper));

			methods.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("methods"), ibDataValue::Array(methods));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolLinqMethods);
