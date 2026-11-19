#ifndef __METAOBJECT_METADATA_ENUM_H__
#define __METAOBJECT_METADATA_ENUM_H__

#include "backend/compiler/enumUnit.h"
#include "core/compiler/codeDef.h"   // ibProgramSyntax
class ibValueEnumVersion : public ibValueEnumeration<ibProgramVersion> {
	public:

	ibValueEnumVersion() : ibValueEnumeration<ibProgramVersion>() {}

	virtual void CreateEnumeration() {
		this->AddEnumeration(version_oes_last, ibT("OES_Last"), ibTRANSLATE("Don't use compatibility"));
	};
};

class ibValueEnumSyntax : public ibValueEnumeration<ibProgramSyntax> {
	public:

	ibValueEnumSyntax() : ibValueEnumeration<ibProgramSyntax>() {}

	virtual void CreateEnumeration() {
		// "ves" — Visual Basic-style ES, the word-based dialect.
		this->AddEnumeration(syntax_ves, ibT("ves"), ibTRANSLATE("ves"));
		this->AddEnumeration(syntax_ces, ibT("ces"), ibTRANSLATE("ces"));
	};
};

#endif