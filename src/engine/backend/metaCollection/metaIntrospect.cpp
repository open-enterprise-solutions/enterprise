////////////////////////////////////////////////////////////////////////////
//	Description : reading the shape of a configuration
////////////////////////////////////////////////////////////////////////////

#include "backend/metaCollection/metaIntrospect.h"

#include "backend/fileSystem/fs.h"            // ibWriterMemory — the JSON sink
#include "backend/metaData.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaObjectMetadata.h"   // GetCompileSyntax — the configuration's dialect
#include "backend/objCtor.h"                  // ibCtorMetaValueType — full type, for the type resolver
#include "backend/serialize/jsonProvider.h"

ibClassID ibResolveMetaKind(const ibMetaData* metaData, const wxString& kind)
{
	if (metaData == nullptr || !metaData->IsConfigOpen())
		return 0;

	// THE ONE REGISTRY every metatype registers itself in
	// (METADATA_TYPE_REGISTER → ibValue::RegisterCtor). So "Catalog" here is the
	// same "Catalog" a configuration writes, by construction rather than by a
	// table somebody has to keep in step — and a metatype added tomorrow is
	// answerable the day it registers, with nothing edited here.
	const ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(kind);
	if (ctor == nullptr)
		return 0;

	// Not everything with a name is a metatype: a value class and a control
	// register too, and asking for "Array" must not return every array.
	if (ctor->GetObjectTypeCtor() != ibCtorObjectType::ibCtorObjectType_object_metadata)
		return 0;

	return ctor->GetClassType();
}

std::vector<wxString> ibListMetaObjects(const ibMetaData* metaData, const wxString& kind)
{
	std::vector<wxString> names;

	const ibClassID clsid = ibResolveMetaKind(metaData, kind);
	if (clsid == 0)
		return names;

	for (const ibValueMetaObject* object : metaData->GetAnyArrayObject<ibValueMetaObject>(clsid))
		if (object != nullptr)
			names.push_back(object->GetName());

	return names;
}

ibValueMetaObject* ibFindMetaObject(ibMetaData* metaData, const wxString& kind, const wxString& name)
{
	const ibClassID clsid = ibResolveMetaKind(metaData, kind);
	if (clsid == 0)
		return nullptr;

	// ⭐ THE TREE FINDS ITS OWN. This used to pull every object of the kind and compare names in a
	// loop — a second copy of ibMetaData::FindAnyObjectByFilter, which takes the name and the clsid
	// together and is the one road the designer and the compiler already take.
	//
	// ⚠ AND THE CLSID IS NOT ONLY A FILTER. Given one, the walk settles the cast with a static_cast,
	// because an exact class id IS an exact type; without one it has to dynamic_cast every candidate.
	// Naming the kind therefore costs nothing and buys the cheap answer.
	//
	// ⭐ AND IT DESCENDS. Without the child filter the walk sees only what hangs off the top of the
	// configuration — so every NESTED kind was unfindable by name: a Template, a Form, a Composer, a
	// Command, an Attribute. Told *"created Template 'Template1'"*, nothing could then look it up,
	// and `metadata_get` answered "No Template is named 'Template1'" about one that plainly existed
	// (2026-09-01). The debugger's own lookup has always passed `true` here, for the same reason —
	// a module is nested too.
	return metaData->FindAnyObjectByFilter<ibValueMetaObject>(name, clsid, true);
}

ibValueMetaObject* ibFindMetaObjectById(ibMetaData* metaData, const ibMetaID& id)
{
	if (metaData == nullptr || !metaData->IsConfigOpen())
		return nullptr;

	// 🛑 THE ROOT IS PART OF THE TREE AND IS NOT ONE OF ITS CHILDREN. The walk below starts AT the
	// common object and descends, so the one object it can never return is the object it starts
	// from — and the configuration root is a metaobject like any other: it has a metaID, it is
	// selectable in the designer's tree, it carries help and notes, and the notes on it are where
	// the direction of the whole configuration belongs.
	//
	// Which meant every tool that addresses by id — read it, describe it, write a note on it —
	// silently could not reach the one object a caller is most likely to be told to write on.
	// Fixed HERE rather than in each of them: they all ask this one question.
	if (ibValueMetaObject* root = metaData->GetCommonMetaObject())
		if (root->GetMetaID() == id)
			return root;

	// use_child_filter — walk INTO the objects, not only across the top level:
	// an attribute and a tabular section have ids of their own, and a caller
	// holding one got it from an answer that described a whole object.
	return metaData->FindAnyObjectByFilter<ibValueMetaObject>(id, /*use_child_filter*/ true);
}

bool ibBuildMetaObjectNode(ibValueMetaObject* object, ibDataNode& node)
{
	if (object == nullptr)
		return false;

	// ⚠ DESCRIBING IS NOT SAVING, and the flag is what says so. Without it
	// BuildDataNode runs OnSaveMetaObject — the object's readiness to be WRITTEN
	// TO A DATABASE — and a half-built object legitimately refuses: an
	// accumulation register with no recorder yet answers "Doesn't have any
	// recorder" and describes nothing.
	//
	// That refusal is correct for a save and wrong for a read, and it made the
	// half-built objects the ones you most need to look at invisible — you
	// cannot finish what you cannot see.
	return object->BuildDataNode(node, saveToFileFlag);
}

bool ibConfigurationWritesInWords(const ibMetaData* metaData)
{
	if (metaData == nullptr || !metaData->IsConfigOpen())
		return false;

	const ibValueMetaObjectConfiguration* configuration =
		dynamic_cast<const ibValueMetaObjectConfiguration*>(metaData->GetCommonMetaObject());
	if (configuration == nullptr)
		return false;

	return configuration->GetCompileSyntax() == ibProgramSyntax::syntax_ves;
}

std::function<wxString(ibClassID)> ibMetaTypeResolver(const ibMetaData* metaData)
{
	return [metaData](ibClassID clsid) -> wxString {
		const ibCtorMetaValueType* typeCtor = metaData != nullptr
			? metaData->GetTypeCtor(clsid) : nullptr;
		return typeCtor != nullptr ? typeCtor->GetClassName() : wxString();
	};
}

wxString ibDescribeMetaObject(ibMetaData* metaData, const wxString& kind, const wxString& name)
{
	ibValueMetaObject* found = ibFindMetaObject(metaData, kind, name);
	if (found == nullptr)
		return wxEmptyString;

	ibDataNode node;
	if (!ibBuildMetaObjectNode(found, node))
		return wxEmptyString;

	ibJsonProvider provider;
	provider.SetTypeResolver(ibMetaTypeResolver(metaData));

	ibWriterMemory writer;
	if (!provider.Write(node, writer))
		return wxEmptyString;

	return wxString::FromUTF8(reinterpret_cast<const char*>(writer.pointer()), writer.size());
}
