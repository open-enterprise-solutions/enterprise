#ifndef __META_SESSION_PARAMETER_OBJECT_H__
#define __META_SESSION_PARAMETER_OBJECT_H__

// A SESSION PARAMETER — a named, typed value that exists once per session.
//
// It is an ATTRIBUTE, and that is the whole design: an attribute is already
// "a declared name with a type, its qualifiers and an editor", which is exactly
// what is being declared here. Deriving it means the type description, the
// designer's property page, the serialisation and AdjustValue arrive finished —
// a metatype of its own would have to be taught all four again.
//
// WHAT MAKES IT DIFFERENT from an attribute of a catalog is its OWNER, not its
// shape. A catalog's attribute is a column: it belongs to a table, it is stored
// per row, and its value is asked of an object. A session parameter belongs to
// the SESSION — no table, no column, no row. It is set once, by the session
// module, and read everywhere:
//
//     // session module
//     Procedure SetSessionParameters()
//     {
//         SessionParameters.Organisation = ...;
//     }
//
// WHY IT EXISTS AT ALL. Row-level access is written as a decorator here: the
// handler folds a Where into the source and the lambda CAPTURES the value it
// compares against, so a captured value reaches the query as a parameter by
// itself. What was missing was never the substitution — it was somewhere for
// the value to live for the length of a session, set before the first read and
// unchanged after. That is what this declares.
//
// It stores nothing and creates nothing. No table is generated for it, and no
// restructuring pass reaches it — not because it says so, but because a table
// is built from the attributes of an OWNER that has one, and this one's owner is
// the configuration.

#include "attribute/metaAttributeObject.h"

class BACKEND_API ibValueMetaObjectSessionParameter : public ibValueMetaObjectAttribute {
	public:

	ibValueMetaObjectSessionParameter(const ibValueTypes& valType = ibValueTypes::TYPE_STRING)
		: ibValueMetaObjectAttribute(valType) {}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();
};

//////////////////////////////////////////////////////////////////////
//  ONE parameter — the unit that owns the value
//////////////////////////////////////////////////////////////////////
//
// It knows its DECLARATION and manages that parameter's value ON THE SESSION:
// reading adjusts to the declared type, writing goes through the same door and is
// refused outside the session module.
//
// No copy of the value lives here. The value belongs to the session — two users
// signed in at the same moment hold different ones — so this is a handle, and
// creating a second one for the same declaration changes nothing.

class BACKEND_API ibValueSessionParameter : public ibValue {
	public:

	// ITS DECLARATION. Defaulted so the type registry can create one with no
	// arguments; an unbound handle holds nothing and says so.
	ibValueSessionParameter(ibValueMetaObjectSessionParameter* metaObject = nullptr);
	virtual ~ibValueSessionParameter() {}

	// FROM THE SESSION, already adjusted to the declared type. Never set answers the
	// type's own empty — an empty reference of that kind rather than a bare
	// Undefined, so a comparison against it narrows the rows to none.
	// AN OVERRIDE of ibValue::GetValue, not a name of its own. `SetValue` below already overrides its
	// base twin, so reading had to answer in the same voice: while this declared `GetValue()` with no
	// parameter it HID the base one, and everything reaching a parameter through ibValue got the
	// HOLDER back while the setter wrote through to the session. `getThis` keeps its base meaning —
	// hand back this object rather than what is in it.
	virtual ibValue GetValue(bool getThis = false) const override;
	// Through the declaration's AdjustValue, and only while the session module runs.
	void SetValue(const ibValue& value) override;

	// Reads AS ITS VALUE: comparing a parameter with a field must compare what is in
	// it, not the holder.
	virtual wxString GetString() const { return GetValue().GetString(); }
	virtual bool IsEmpty() const { return GetValue().IsEmpty(); }

	private:

	// ibValuePtr — a metaobject is a refcounted value; a raw pointer would outlive a
	// configuration reload.
	ibValuePtr<ibValueMetaObjectSessionParameter> m_metaObject;
};

//////////////////////////////////////////////////////////////////////
//  SessionParameters — the collection reached from the global context
//////////////////////////////////////////////////////////////////////
//
//     SessionParameters.Organisation          // read — from anywhere
//     SessionParameters.Organisation = org;   // write — only in the session module
//
// The members are whatever the configuration declared, so the name list is read
// from the metadata every time it is asked: a parameter added in the designer
// appears without a restart, and the editor completes exactly what the runtime
// accepts.
//
// Each name yields the UNIT above, and the unit is what talks to the session. This
// object holds no values and no cached list of its own.

class BACKEND_API ibValueSessionParameters : public ibValueDynamicMembers {
	public:

	// WHICH configuration's parameters. Defaulted so the type registry can create
	// one with no arguments; the global context hands over the metadata it already
	// holds, and a null falls back to the active one.
	ibValueSessionParameters(class ibMetaData* metaData = nullptr);
	virtual ~ibValueSessionParameters() {}

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);

	private:

	void FillMembers(ibMemberTable& helper) const;

	// BY POSITION, asked of the metadata when asked. Keeping a list here would be a
	// second copy of the declarations, stale the moment one is added.
	ibValueMetaObjectSessionParameter* ParameterAt(long index) const;

	class ibMetaData* m_metaData;
};

#endif // !__META_SESSION_PARAMETER_OBJECT_H__
