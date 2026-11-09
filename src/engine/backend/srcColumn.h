#ifndef __SRC_COLUMN_H__
#define __SRC_COLUMN_H__

// ibBackendSourceColumn — a COLUMN OF A SOURCE, as the form binding and the field lists see one: its name, synonym and
// comment, its type, its picture. The dot (ibBackendTypeSourceFactory::WalkSource) returns THIS, blind to the concrete
// class: an attribute of the configuration, a form's own attribute (ibBackendFormAttributeValue), a value table's
// column — each IS one, no adapter. A control reads its caption and its type off it without knowing
// which it stands on.
//
// Kin of the source (srcObject.h, srcDataObject.h) and of nothing else: it does not meet the query.

#include "backend/backend.h"                       // BACKEND_API
#include "core/description/typeDescription.h"      // ibTypeDescription (the column's L3 type)
#include "core/description/pictureDescription.h"   // ibPictureDescription — the column's picture, by its number (GetColumnPicture)

class BACKEND_API ibBackendSourceColumn
{
public:
	virtual ~ibBackendSourceColumn() = default;

	// Logical identifier — what a script / the L4 parser refers to.
	virtual ibString GetName() const = 0;

	// Display caption. Defaults to the logical name; an attribute — of the configuration or of a form — overrides it
	// with its synonym.
	virtual ibString GetSynonym() const { return GetName(); }

	// Tooltip / comment. Empty by default; an attribute overrides it.
	virtual ibString GetComment() const { return ibEmptyString; }

	// The column's L3 type — CLSIDs + number / string / date qualifiers. The single source of
	// "what this column holds". The SAME accessor the attribute already exposes — free.
	virtual ibTypeDescription& GetTypeDesc() const = 0;

	// WHAT A VALUE HERE MAY BE — the same object for every ordinary column. Declared on BOTH bases (the
	// type factory names it too, backend_type.h) for the same reason GetTypeDesc is: an attribute is a
	// column AND a type factory, and one overrider then answers for both. A column that stands behind
	// no declaration of its own — temp, computed — inherits this default and never differs.
	virtual ibTypeDescription& GetTypeValueDesc() const { return GetTypeDesc(); }

	// Is the column usable / shown? A plain (queryable) column always is; a metaobject attribute
	// overrides — a deleted or access-denied field is not. The metadata-agnostic source explorer
	// gates on THIS instead of poking the metaobject.
	virtual bool IsAllowed() const { return true; }

	// Is the column AVAILABLE in this base — a functional option's question, and NOT IsAllowed. A column
	// that is not allowed is not there; one that is not available is there, read and written like any
	// other, and only the interface does not offer it (functionalOptionGate.h). A plain column always is;
	// a metaobject attribute asks its metaobject. Last in the class: a virtual added in the middle moves
	// every slot after it for whatever was built before it.
	virtual bool IsAvailable() const { return true; }

	// THE COLUMN'S OWN PICTURE — asked of the column, never deduced by the reader. The default is
	// the plain ATTRIBUTE picture, so every column is dressed even when it stands behind no
	// metaobject (a view's column, a temp table's); a column that IS a metaobject answers with its
	// metatype's. That is what keeps a dimension from looking like a resource without anybody,
	// anywhere, keeping a list of kinds and a switch over it — a metatype added tomorrow is dressed
	// the day it registers.
	//
	// ⭐ A NUMBER, NOT AN IMAGE: the class whose picture it is, as a tree row names one to the client
	// (ibBackendPicture::SendPicture). Whoever draws it makes its own image from it — the desktop beside
	// the server, the client from the server's answer.
	//
	// Body in metaAttributeObject_res.cpp, next to the attribute class it names: this header must not
	// pull the metadata tree in (the attribute metaobject includes THIS file).
	virtual ibPictureDescription GetColumnPicture() const;
};

#endif
