/////////////////////////////////////////////////////////////////////////////
// Help entries taken from the type registry.
//
// The corpus files describe keywords and global functions. Array, Structure,
// AccumulationRecordType and the values the platform hands out are registered
// the same way, and a search that never asks the registry answers that they
// do not exist. A search asks this when it runs. The corpus files are not a
// second copy of the registry.
//
// A member's call form is read off a value built for the purpose. Building
// one asks wx (a font, a point), so it waits until wx is up. The name of the
// type is recorded either way: a search for Array does not need its methods.
/////////////////////////////////////////////////////////////////////////////

#include "backend/syntaxHelper/helpLoader.h"

#include "backend/compiler/typeCtor.h"
#include "backend/compiler/value.h"

#include <wx/app.h>   // wxTheApp — a value is built only once wx itself is up

#include <set>
#include <utility>
#include <vector>

namespace {

enum class Family {
	Value,
	Enum,
	Handed,
};

bool Remember(std::set<wxString>& ids, const wxString& id)
{
	return !id.IsEmpty() && ids.insert(id).second;
}

void AddEntry(std::vector<ibHelpEntry>& out, ibHelpEntry entry)
{
	out.push_back(std::move(entry));
}

ibHelpEntry Head(const wxString& id, const wxString& name, ibHelpKind kind,
                 ibClassID classId, const wxString& signature, const wxString& description)
{
	ibHelpEntry e;
	e.id           = id;
	e.nameEn       = name;
	e.nameLocal    = name;
	e.kind         = kind;
	e.classId      = classId;
	e.signature    = signature;
	e.syntaxBlock  = signature;
	e.description  = description;
	e.categoryKeys = { wxT("value_types") };
	return e;
}

void AddMembers(const ibCtorAbstractType* ctor, const wxString& name, const wxString& typeId,
                bool asEnum, std::vector<ibHelpEntry>& out, std::set<wxString>& ids)
{
	if (wxTheApp == nullptr)
		return;

	ibValue value;
	try {
		value = ctor->CreateObject();
	} catch (...) {
		return;
	}
	if (value.GetType() == ibValueTypes::TYPE_EMPTY)
		return;

	try {
		const long nMethods = value.GetNMethods();
		for (long i = 0; i < nMethods; ++i) {
			const wxString method = value.GetMethodName(i);
			const wxString id = typeId + wxT(".") + method;
			if (method.IsEmpty() || !Remember(ids, id))
				continue;

			const wxString helper = value.GetMethodHelper(i);
			ibHelpEntry e = Head(id, method,
			                     value.HasRetVal(i) ? ibHelpKind::kMethod : ibHelpKind::kProcedure,
			                     0, helper.IsEmpty() ? method : helper,
			                     helper.IsEmpty() ? method : helper);
			e.seeAlso = { typeId };
			AddEntry(out, std::move(e));
		}

		const long nProps = value.GetNProps();
		for (long i = 0; i < nProps; ++i) {
			if (value.IsPropScoped(i))
				continue;
			const wxString prop = value.GetPropName(i);
			const wxString id = typeId + wxT(".") + prop;
			if (prop.IsEmpty() || !Remember(ids, id))
				continue;

			const wxString signature = asEnum ? (name + wxT(".") + prop) : prop;
			ibHelpEntry e = Head(id, prop,
			                     asEnum ? ibHelpKind::kSystemEnum : ibHelpKind::kProperty,
			                     0, signature, signature);
			e.seeAlso = { typeId };
			AddEntry(out, std::move(e));
		}
	} catch (...) {
		// This type's members stay unlisted. The type itself is already recorded.
	}
}

void OneFamily(ibCtorObjectType objectType, Family family,
               std::vector<ibHelpEntry>& out, std::set<wxString>& ids)
{
	const bool asEnum = family == Family::Enum;
	const bool handed = family == Family::Handed;

	for (const ibCtorAbstractType* ctor : ibValue::GetListCtorsByType(objectType)) {
		if (ctor == nullptr)
			continue;
		const wxString name = ctor->GetClassName();
		if (name.IsEmpty())
			continue;

		const wxString typeId = (asEnum ? wxT("enum.") : wxT("cls.")) + name;
		if (Remember(ids, typeId)) {
			wxString signature;
			wxString description;
			ibHelpKind kind = ibHelpKind::kCollection;
			if (asEnum) {
				kind = ibHelpKind::kEnumType;
				signature = name;
				description = wxT("A platform enumeration.");
			} else if (handed) {
				signature = name;
				description = wxT("A value the platform hands you.");
			} else {
				signature = wxT("New ") + name;
				description = wxT("A value built with New.");
			}
			AddEntry(out, Head(typeId, name, kind, ctor->GetClassType(), signature, description));
		}

		// A handed value is named here. Building one is a different question, and some of
		// them exist only against the object that owns them.
		if (!handed)
			AddMembers(ctor, name, typeId, asEnum, out, ids);
	}
}

} // namespace

std::vector<ibHelpEntry> HelpEntriesFromTypeRegistry()
{
	std::vector<ibHelpEntry> entries;
	std::set<wxString> ids;
	try {
		OneFamily(ibCtorObjectType::ibCtorObjectType_object_value, Family::Value, entries, ids);
		OneFamily(ibCtorObjectType::ibCtorObjectType_object_enum, Family::Enum, entries, ids);
		OneFamily(ibCtorObjectType::ibCtorObjectType_object_system, Family::Handed, entries, ids);
	} catch (...) {
	}
	return entries;
}
