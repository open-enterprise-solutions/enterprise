#ifndef __META_FUNCTIONAL_OPTION_OBJECT_H__
#define __META_FUNCTIONAL_OPTION_OBJECT_H__

// A FUNCTIONAL OPTION — whether a part of the configuration is used in THIS base.
//
// It IS a stored value, as a constant is — the same base (metaStoredValueObject.h): a named value kept once
// per base, with a form to change it, a Read and a Write right, and a module whose BeforeWrite / OnWrite run
// in the write door. What it adds:
//
//   • its value is Boolean and nothing else — used, or not;
//   • MEMBERS: the objects, fields and tables that belong to it. Switched off, they are not available
//     (functionalOptionGate.h) — and nothing more: they stay in the metadata, in the schema and in every
//     query, and the data written before is there when the option comes back on.
//
// ⭐ THE MEMBERSHIP LIVES ON THE MEMBER, the way a section's does (functionalOptionHelper.h): every object
// keeps the options it belongs to, so "am I available" is asked of the object and a copied object takes its
// options along. Opening an option shows them as a tree to tick, the way a section and a common attribute
// show theirs (the designer's functionalOptionEditor); `option_include` does the same over MCP.
//
// The module is the stored value's: BeforeWrite refuses a switch the data does not allow ("there are
// balances in a foreign currency"), OnWrite carries its consequences. Code reads it the way it reads a constant:
//
//     If FunctionalOptions.MultipleWarehouses.Get() Then …

#include "backend/metaCollection/metaStoredValueObject.h"
#include "backend/propertyManager/property/propertyFunctionalOptions.h"

class BACKEND_API ibValueMetaObjectFunctionalOption : public ibValueMetaObjectStoredValue {
public:

	ibValueMetaObjectFunctionalOption();

	//support icons
	virtual wxIcon GetIcon() const override;
	static wxIcon GetIconGroup();

	// ⚠ THE TYPE IS PART OF THE METATYPE — Boolean, always; the value column asks this. (The fill check stays
	// the base's "no": False is an answer, not an empty field.)
	virtual ibTypeDescription& GetTypeDesc() const override { return m_typeBoolean; }

	// A switch is never made unavailable by a switch: an option that belonged to one could be taken out of
	// sight with nothing left to turn it back on. How options depend on each other is Requires.
	virtual bool IsFunctionalOptionAllowed() const override { return false; }

	// ⭐ THE VALUE FOR THE INTERFACE — read with no right asked, because what is available is a fact of the base
	// and the same for everyone. Nothing written (no table, no row, a read that failed) is the declared
	// initial value; anything else that is not a Boolean reads as used — an option that cannot be read must
	// not take a working part of the system out of sight.
	bool IsOn() const;

	// A FRESH OPTION IS ITS DECLARED INITIAL VALUE — what a base that has never switched it reads (the
	// constant's ReadStoredValue answers CreateValue for nothing written), so an option arriving with an
	// update starts where its author said. The gate and `FunctionalOptions.X.Get()` read the same answer.
	virtual ibValue CreateValue() const override;

	// The option this one works under: while that one is off, this one counts as off too, whatever its own
	// value. wxNOT_FOUND when it requires none. Resolved along the chain by the gate (EffectiveValues).
	ibMetaID GetRequired() const;

	// The write door said the value changed — what the gate answered from the old one is dropped.
	virtual void OnAfterValueWrite() const override;

	virtual bool OnDeleteMetaObject() override;
	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterCloseMetaObject() override;

	// The composition first, then the stored value's module (metaFunctionalOptionObjectMenu.cpp).
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items) override;

protected:

	//per-type node data — the option's own two settings; the members keep the membership
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	mutable ibTypeDescription m_typeBoolean{ g_valueBooleanCLSID };

	// THE VALUE'S OWN GROUP, named as the constant names its (partial/constant.h): what the value starts as,
	// and what it works under.
	ibPropertyCategory* m_categoryValue = ibPropertyObject::CreatePropertyCategory(wxT("Value"), _("Value"));
	// ON by default: a working part of the system must not disappear by itself when an option arrives.
	ibPropertyBoolean* m_propertyInitialValue = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryValue, wxT("InitialValue"), _("Initial value"), _("The value the option has in a base where nobody has switched it yet - an option arriving with an update. On by default, so a working part of the system does not disappear by itself; off for a new part the base should switch on deliberately."), true);
	ibPropertyFunctionalOptions* m_propertyRequires = ibPropertyObject::CreateProperty<ibPropertyFunctionalOptions>(m_categoryValue, wxT("Requires"), _("Requires"), _("The option this one works under. While that one is off, this one counts as off too, whatever its own value - what belongs to it is not shown."), ibPropertyChoiceMode::Single);
};

#endif // !__META_FUNCTIONAL_OPTION_OBJECT_H__
