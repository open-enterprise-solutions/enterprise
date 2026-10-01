#ifndef __DATABASELAYER_DEF_H__
#define __DATABASELAYER_DEF_H__

#include "backend/backend_core.h"

// Driver identity, as answered by ibDatabaseLayer::GetDatabaseLayerType().
//
// PRODUCTION is Firebird and PostgreSQL. SQLite is embedded for TESTS and LOGGING
// only — single-process, no materialized views, no ALTER COLUMN — and is never a
// production target. ODBC is the base an MSSQL layer derives from (its own dialect
// dictionary, ibTriggerFamily::SetBased).
//
// 4 IS DELIBERATELY VACANT: it was MySQL, whose driver was removed. The neighbours
// keep their numbers rather than closing the gap, because renumbering a value that
// any stored connection setting might carry would silently repoint it at another
// driver. A hole costs nothing; a shift cannot be undone by looking at the value.
#define DATABASELAYER_FIREBIRD 1
#define DATABASELAYER_POSTGRESQL 2
#define DATABASELAYER_ODBC 3
#define DATABASELAYER_SQLLITE 5

#endif // __DATABASELAYER_DEF_H__
