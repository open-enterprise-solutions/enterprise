#ifndef __PROPERTY_REGISTRY_H__
#define __PROPERTY_REGISTRY_H__

#include <wx/propgrid/propgrid.h>

#include <functional>
#include <vector>

#include "backend/propertyManager/propertyObject.h"

// The backend <-> grid correspondence, held ENTIRELY on the front.
//
// It replaces the ms_property* slot family. There, a property produced its own editor:
// backend declared `virtual wxObject* GetPGProperty()`, called a function pointer the
// frontend had filled, and null-checked it 32 times. The core knew it owed the grid an
// object — the last thing tying it to an editor it must not know about.
//
// Here the direction is reversed. A backend property is data; the front says "handed THIS
// backend type, this is the widget I build and this is how I read the value out of it".
// Registration carries both halves at once, so the pair cannot drift: no filename
// convention holds them together any more.
//
//   ibPropertyRegistry::Register([](ibPropertyBoolean* prop) -> wxPGProperty* {
//       return new wxBoolProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsBoolean());
//   });
//
// The backend type is named ONCE — in the lambda's own parameter — and deduced from it.
// Reading the value is then a plain typed call, no cast at the callsite.
//
// A `dynamic_cast` inside IS the match: it answers "is this mine?" and yields the typed
// pointer in one step, so the backend needs no type key of its own. It also matches by
// HIERARCHY, which a typeid/type_index table cannot: ibPropertyEnum<T> has two dozen-odd
// instantiations, and one Register taking ibPropertyEnumBase* covers them all. (The count
// is deliberately not written as a number here: it was 22 for long enough to become wrong,
// and a figure nobody recounts is a claim that rots in place.)
class ibPropertyRegistry {

	// Pull the backend property type out of the maker's parameter: T -> its operator(),
	// then the single argument of that. Both const and mutable lambdas are accepted.
	template <typename typeMaker>
	struct ibMakerArg : ibMakerArg<decltype(&typeMaker::operator())> {};
	template <typename typeClass, typename typeRet, typename typeArg>
	struct ibMakerArg<typeRet(typeClass::*)(typeArg*) const> { using type = typeArg; };
	template <typename typeClass, typename typeRet, typename typeArg>
	struct ibMakerArg<typeRet(typeClass::*)(typeArg*)> { using type = typeArg; };

	// A registered maker: the widget if the property is its type, nullptr if not.
	//
	// Keyed on ibBackendProperty, not ibProperty: ibProperty and ibEvent are SIBLINGS under
	// it (§2 of docs/property-system.md), and the inspector renders both through the same
	// GetProperty/GetEvent pair. One registry serves them; a maker just names the side it
	// wants in its parameter, and the dynamic_cast sorts it out.
	using ibPropertyMaker = std::function<wxPGProperty* (ibBackendProperty*)>;

	struct ibPropertyEntry {
		int m_priority;          // LOWER runs first — see Register()
		ibPropertyMaker m_maker;
	};

	// Types that answer "yes, that's me" and want no editor — see RegisterNoEditor().
	using ibPropertySilent = std::function<bool (ibBackendProperty*)>;

	static std::vector<ibPropertyEntry>& GetEntries();
	static std::vector<ibPropertySilent>& GetSilent();

public:

	enum {
		Priority_Exact = 0,     // a concrete property class — the default
		Priority_Base  = 100,   // a maker taking a BASE: matches every type deriving from it
	};

	// priority: lower is tried first. It exists for ONE reason — a base class matches its
	// derived types too, so a maker taking ibPropertyEnumBase* must run AFTER anything
	// deriving from it. Static-initialisation order across a DLL would be a coin toss;
	// this states the intent where the registration is.
	template <typename typeMaker>
	static void Register(typeMaker maker, int priority = Priority_Exact) {
		using typeProp = typename ibMakerArg<typeMaker>::type;
		GetEntries().push_back({
			priority,
			[maker](ibBackendProperty* property) -> wxPGProperty* {
				typeProp* typed = dynamic_cast<typeProp*>(property);
				return typed != nullptr ? maker(typed) : nullptr;
			}
		});
	}

	// "This type is shown in no grid at all" — a DELIBERATE absence, as opposed to a
	// forgotten Register. Without it the two would be indistinguishable: a maker returning
	// nullptr already means "not my type", so Create would walk past and assert on a case
	// that is perfectly correct (ibValueMetaObjectComposite has no editor by design).
	template <typename typeProp>
	static void RegisterNoEditor() {
		GetSilent().push_back([](ibBackendProperty* property) -> bool {
			return dynamic_cast<typeProp*>(property) != nullptr;
		});
	}

	// Build the editor for a property or an event. Null when the front was never loaded
	// (headless registers nothing and never reaches here) or when the type declared itself
	// editor-less. With a live front an unregistered type is a programming error —
	// something that would silently vanish from the grid — so it asserts.
	static wxPGProperty* Create(ibBackendProperty* property);
};

#endif
