#ifndef __FORM_NAME_H__
#define __FORM_NAME_H__

////////////////////////////////////////////////////////////////////////////
//	Description : A FORM BY ITS FULL NAME — the one place that reads one.
//
//	A script that does not know at compile time which form it wants writes
//	its name as text: `Document.Invoice.ObjectForm`, `Report.Sales.Form.Main`,
//	`CommonForm.Settings`. That text is read here and nowhere else, so every
//	verb that takes such a name (GetForm today) accepts the same names and
//	refuses the same ones with the same sentence.
//
//	The spellings:
//	  Kind.Name.Form.FormName  — a form the object declares, by its name;
//	  Kind.Name.ObjectForm     — the object's own form of a kind: ObjectForm,
//	                             ListForm, ChoiceForm, GroupForm,
//	                             GroupChoiceForm, RecordForm; plain `Form`
//	                             is the object form;
//	  CommonForm.Name          — a common form.
//	The kind is the metatype's name, or that name as the Russian and the
//	Ukrainian translations write it — configurations carried over from those
//	languages build these names at run time, out of words in them.
////////////////////////////////////////////////////////////////////////////

#include <vector>

#include "backend/backend.h"
#include "backend/metaCollection/metaFormObject.h"   // ibFormID, defaultFormType

class BACKEND_API ibMetaData;
class BACKEND_API ibValueMetaObjectGenericData;
class BACKEND_API ibValueMetaObjectCommonForm;

class BACKEND_API ibFormName {
public:

	// ⭐ THE SPELLING, READ WITHOUT A CONFIGURATION. What the text says, and nothing it would take a
	// configuration to know — so it is answered (and tested) apart from one.
	struct ibParts {
		ibClassID m_kind = 0;                 // the metatype; g_metaCommonFormCLSID for a common form
		ibString m_objectName;                // the object, or the common form
		ibString m_formName;                  // Kind.Name.Form.<this>; empty for a form of a kind
		std::vector<ibString> m_formTypes;    // …the kind: the form types that answer it, the first one present wins
	};

	// Raises (ibBackendCoreException) with the name in the sentence when it is not spelled as above.
	static ibParts Parse(const ibString& fullName);

	// What the name names in `metaData`. Raises with the name in the sentence when it names nothing.
	static ibFormName Resolve(const ibMetaData* metaData, const ibString& fullName);

	const ibValueMetaObjectCommonForm* m_commonForm = nullptr;   // a common form — then nothing below is set
	const ibValueMetaObjectGenericData* m_owner = nullptr;       // the object whose form it is
	const ibValueMetaObjectFormBase* m_form = nullptr;           // the form named; nullptr = the owner's own of m_formType
	ibFormID m_formType = defaultFormType;
	ibString m_formTypeName;                                     // FormObject, FormList, … — the owner's word for m_formType

	// The form shows ONE object, so a Key parameter says which (a list form shows many and has no use for one).
	bool IsObjectForm() const;
};

#endif // !__FORM_NAME_H__
