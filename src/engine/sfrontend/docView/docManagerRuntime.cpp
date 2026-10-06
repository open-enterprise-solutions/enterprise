#include "docManagerRuntime.h"

#include "sfrontend/schema/runtime/allFunctions.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibDocManagerRuntime, ibDocManager);

ibDocManagerRuntime::ibDocManagerRuntime()
	: ibDocManager()
{
	// All functions — it opens what the runtime runs.
	RegisterSchema(ibClientSchemaKind::AllFunctions, std::make_unique<ibSchemaAllFunctions>());
}
