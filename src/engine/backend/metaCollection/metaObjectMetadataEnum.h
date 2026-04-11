#ifndef __METAOBJECT_METADATA_ENUM_H__
#define __METAOBJECT_METADATA_ENUM_H__

#include "backend/compiler/enumUnit.h"
class ibValueEnumVersion : public ibValueEnumeration<ibProgramVersion> {
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumVersion);
public:

	ibValueEnumVersion() : ibValueEnumeration<ibProgramVersion>() {}

	virtual void CreateEnumeration() {
		this->AddEnumeration(version_oes_1_0_0, wxT("OES_1_0_0"), _("1.0.0"));
		this->AddEnumeration(version_oes_last, wxT("OES_Last"), _("Don't use compatibility"));
	};
};

class ibValueEnumSyntax : public ibValueEnumeration<ibProgramSyntax> {
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumSyntax);
public:

	ibValueEnumSyntax() : ibValueEnumeration<ibProgramSyntax>() {}

	virtual void CreateEnumeration() {
		this->AddEnumeration(syntax_vbs, wxT("vbs"), _("vbs"));
		this->AddEnumeration(syntax_ces, wxT("ces"), _("ces"));
	};
};

#endif