#ifndef __METAOBJECT_METADATA_ENUM_H__
#define __METAOBJECT_METADATA_ENUM_H__

#include "backend/compiler/enumUnit.h"
#include "core/compiler/codeDef.h"   // ibProgramSyntax
class ibValueEnumVersion : public ibValueEnumeration<ibProgramVersion> {
	public:

	ibValueEnumVersion() : ibValueEnumeration<ibProgramVersion>() {}

	virtual void CreateEnumeration() {
		this->AddEnumeration(version_oes_last, ibT("OES_Last"), _("Don't use compatibility"));
	};
};

class ibValueEnumSyntax : public ibValueEnumeration<ibProgramSyntax> {
	public:

	ibValueEnumSyntax() : ibValueEnumeration<ibProgramSyntax>() {}

	virtual void CreateEnumeration() {
		// Wire string stays "vbs" for back-compat with existing serialised
		// configs; the user-visible label is "ves" — Visual Basic-style
		// ES, a legacy business-scripting dialect. Renaming the wire token would force migration
		// of every stored module's syntax property.
		this->AddEnumeration(syntax_ves, ibT("vbs"), _("ves"));
		this->AddEnumeration(syntax_ces, ibT("ces"), _("ces"));
	};
};

#endif