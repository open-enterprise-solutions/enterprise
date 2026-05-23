#ifndef __FIREBIRD_INTERFACES_H__
#define __FIREBIRD_INTERFACES_H__

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/dynlib.h>

#include "engine/ibase.h"

typedef ISC_STATUS(ISC_EXPORT *fb_interpretType)(ISC_SCHAR*, unsigned int, const ISC_STATUS**);
typedef void (ISC_EXPORT *isc_expand_dpbType)(ISC_SCHAR**, short*, ...);
typedef int (ISC_EXPORT *isc_modify_dpbType)(ISC_SCHAR**, short*, unsigned short, const ISC_SCHAR*, short);
typedef ISC_STATUS(ISC_EXPORT *isc_create_databaseType)(ISC_STATUS*, short, const ISC_SCHAR*,
	isc_db_handle*, short, const ISC_SCHAR*, short);
typedef ISC_STATUS(ISC_EXPORT *isc_attach_databaseType)(ISC_STATUS*, short, const ISC_SCHAR*,
	isc_db_handle*, short, const ISC_SCHAR*);
typedef ISC_STATUS(ISC_EXPORT *isc_detach_databaseType)(ISC_STATUS*, isc_db_handle*);
typedef ISC_STATUS(ISC_EXPORT *isc_start_transactionType)(ISC_STATUS*, isc_tr_handle*, short, ...);
typedef ISC_STATUS(ISC_EXPORT *isc_commit_transactionType)(ISC_STATUS*, isc_tr_handle*);
typedef ISC_STATUS(ISC_EXPORT *isc_rollback_transactionType)(ISC_STATUS*, isc_tr_handle*);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_execute_immediateType)(ISC_STATUS*, isc_db_handle*,
	isc_tr_handle*, unsigned short, const ISC_SCHAR*, unsigned short, XSQLDA*);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_allocate_statementType)(ISC_STATUS*, isc_db_handle*, isc_stmt_handle*);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_prepareType)(ISC_STATUS*, isc_tr_handle*, isc_stmt_handle*,
	unsigned short, const ISC_SCHAR*, unsigned short, XSQLDA*);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_describeType)(ISC_STATUS*, isc_stmt_handle*,
	unsigned short, XSQLDA*);
typedef void (ISC_EXPORT *isc_sql_interpreteType)(short, ISC_SCHAR*, short);
typedef ISC_LONG(ISC_EXPORT *isc_sqlcodeType)(const ISC_STATUS*);
typedef void (ISC_EXPORT *isc_encode_timestampType)(const void*, ISC_TIMESTAMP*);
typedef ISC_STATUS(ISC_EXPORT *isc_create_blob2Type)(ISC_STATUS*, isc_db_handle*,
	isc_tr_handle*, isc_blob_handle*, ISC_QUAD*, short, const ISC_SCHAR*);
typedef ISC_STATUS(ISC_EXPORT *isc_open_blob2Type)(ISC_STATUS*, isc_db_handle*,
	isc_tr_handle*, isc_blob_handle*, ISC_QUAD*, ISC_USHORT, const ISC_UCHAR*);
typedef ISC_STATUS(ISC_EXPORT *isc_put_segmentType)(ISC_STATUS*, isc_blob_handle*,
	unsigned short, const ISC_SCHAR*);
typedef ISC_STATUS(ISC_EXPORT *isc_close_blobType)(ISC_STATUS*, isc_blob_handle*);
typedef ISC_STATUS(ISC_EXPORT *isc_commit_retainingType)(ISC_STATUS*, isc_tr_handle*);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_free_statementType)(ISC_STATUS*, isc_stmt_handle*, unsigned short);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_describe_bindType)(ISC_STATUS*, isc_stmt_handle*,
	unsigned short, XSQLDA*);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_executeType)(ISC_STATUS*, isc_tr_handle*,
	isc_stmt_handle*, unsigned short, XSQLDA*);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_sql_infoType)(ISC_STATUS*, isc_stmt_handle*,
	short, const ISC_SCHAR*, short, ISC_SCHAR*);
typedef ISC_LONG(ISC_EXPORT *isc_vax_integerType)(const ISC_SCHAR*, short);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_execute2Type)(ISC_STATUS*, isc_tr_handle*,
	isc_stmt_handle*, unsigned short, XSQLDA*, XSQLDA*);
typedef ISC_STATUS(ISC_EXPORT *isc_dsql_fetchType)(ISC_STATUS*, isc_stmt_handle*,
	unsigned short, XSQLDA*);
typedef void (ISC_EXPORT *isc_decode_timestampType)(const ISC_TIMESTAMP*, void*);
typedef void (ISC_EXPORT *isc_decode_sql_dateType)(const ISC_DATE*, void*);
typedef void (ISC_EXPORT *isc_decode_sql_timeType)(const ISC_TIME*, void*);
typedef ISC_STATUS(ISC_EXPORT *isc_get_segmentType)(ISC_STATUS*, isc_blob_handle*,
	unsigned short*, unsigned short, ISC_SCHAR*);

// Services API — for gbak/nbackup/sweep through fbclient without
// spawning child processes. Service attachment authenticates once,
// then `isc_service_start` enqueues an action (backup, restore,
// repair, etc.) described by an SPB (Service Parameter Block).
// `isc_service_query` polls progress / waits for completion.
typedef ISC_STATUS(ISC_EXPORT *isc_service_attachType)(ISC_STATUS*, unsigned short,
	const ISC_SCHAR*, isc_svc_handle*, unsigned short, const ISC_SCHAR*);
typedef ISC_STATUS(ISC_EXPORT *isc_service_detachType)(ISC_STATUS*, isc_svc_handle*);
typedef ISC_STATUS(ISC_EXPORT *isc_service_startType)(ISC_STATUS*, isc_svc_handle*,
	isc_resv_handle*, unsigned short, const ISC_SCHAR*);
typedef ISC_STATUS(ISC_EXPORT *isc_service_queryType)(ISC_STATUS*, isc_svc_handle*,
	isc_resv_handle*, unsigned short, const ISC_SCHAR*, unsigned short,
	const ISC_SCHAR*, unsigned short, ISC_SCHAR*);

class ibInterfaceFirebird
{
public:
	ibInterfaceFirebird() = default;
	bool Init();

	fb_interpretType GetFbInterpret() { return m_pFbInterpret; }
	isc_expand_dpbType GetIscExpandDpb() { return m_pIscExpandDpb; }
	isc_modify_dpbType GetIscModifyDpb() { return m_pIscModifyDpb; }
	isc_create_databaseType GetIscCreateDatabase() { return m_pIscCreateDatabase; }
	isc_attach_databaseType GetIscAttachDatabase() { return m_pIscAttachDatabase; }
	isc_detach_databaseType GetIscDetachDatabase() { return m_pIscDetachDatabase; }
	isc_start_transactionType GetIscStartTransaction() { return m_pIscStartTransaction; }
	isc_commit_transactionType GetIscCommitTransaction() { return m_pIscCommitTransaction; }
	isc_rollback_transactionType GetIscRollbackTransaction() { return m_pIscRollbackTransaction; }
	isc_dsql_execute_immediateType GetIscDsqlExecuteImmediate() { return m_pIscDsqlExecuteImmediate; }
	isc_dsql_allocate_statementType GetIscDsqlAllocateStatement() { return m_pIscDsqlAllocateStatement; }
	isc_dsql_prepareType GetIscDsqlPrepare() { return m_pIscDsqlPrepare; }
	isc_dsql_describeType GetIscDsqlDescribe() { return m_pIscDsqlDescribe; }
	isc_sql_interpreteType GetIscSqlInterprete() { return m_pIscSqlInterprete; }
	isc_sqlcodeType GetIscSqlcode() { return m_pIscSqlcode; }
	isc_encode_timestampType GetIscEncodeTimestamp() { return m_pIscEncodeTimestamp; }
	isc_create_blob2Type GetIscCreateBlob2() { return m_pIscCreateBlob2; }
	isc_open_blob2Type GetIscOpenBlob2() { return m_pIscOpenBlob2; }
	isc_put_segmentType GetIscPutSegment() { return m_pIscPutSegment; }
	isc_close_blobType GetIscCloseBlob() { return m_pIscCloseBlob; }
	isc_commit_retainingType GetIscCommitRetaining() { return m_pIscCommitRetaining; }
	isc_dsql_free_statementType GetIscDsqlFreeStatement() { return m_pIscDsqlFreeStatement; }
	isc_dsql_describe_bindType GetIscDsqlDescribeBind() { return m_pIscDsqlDescribeBind; }
	isc_dsql_executeType GetIscDsqlExecute() { return m_pIscDsqlExecute; }
	isc_dsql_sql_infoType GetIscDsqlSqlInfo() { return m_pIscDsqlSqlInfo; }
	isc_vax_integerType GetIscVaxInteger() { return m_pIscVaxInteger; }
	isc_dsql_execute2Type GetIscDsqlExecute2() { return m_pIscDsqlExecute2; }
	isc_dsql_fetchType GetIscDsqlFetch() { return m_pIscDsqlFetch; }
	isc_decode_timestampType GetIscDecodeTimestamp() { return m_pIscDecodeTimestamp; }
	isc_decode_sql_dateType GetIscDecodeSqlDate() { return m_pIscDecodeSqlDate; }
	isc_decode_sql_timeType GetIscDecodeSqlTime() { return m_pIscDecodeSqlTime; }
	isc_get_segmentType GetIscGetSegment() { return m_pIscGetSegment; }

	isc_service_attachType GetIscServiceAttach() { return m_pIscServiceAttach; }
	isc_service_detachType GetIscServiceDetach() { return m_pIscServiceDetach; }
	isc_service_startType  GetIscServiceStart()  { return m_pIscServiceStart;  }
	isc_service_queryType  GetIscServiceQuery()  { return m_pIscServiceQuery;  }

private:
	wxDynamicLibrary m_FirebirdDLL;

	// Default-init to nullptr so a partial Init() (DLL loads but a symbol
	// is missing) leaves uninitialised pointers as nullptr instead of
	// random stack garbage. The Init() failure path bails before any
	// caller can dereference them — but if a future change forgets to
	// check the Init() return, the crash will be a clean nullptr deref
	// rather than a jump into the heap.
	fb_interpretType m_pFbInterpret = nullptr;
	isc_expand_dpbType m_pIscExpandDpb = nullptr;
	isc_modify_dpbType m_pIscModifyDpb = nullptr;
	isc_create_databaseType m_pIscCreateDatabase = nullptr;
	isc_attach_databaseType m_pIscAttachDatabase = nullptr;
	isc_detach_databaseType m_pIscDetachDatabase = nullptr;
	isc_start_transactionType m_pIscStartTransaction = nullptr;
	isc_commit_transactionType m_pIscCommitTransaction = nullptr;
	isc_rollback_transactionType m_pIscRollbackTransaction = nullptr;
	isc_dsql_execute_immediateType m_pIscDsqlExecuteImmediate = nullptr;
	isc_dsql_allocate_statementType m_pIscDsqlAllocateStatement = nullptr;
	isc_dsql_prepareType m_pIscDsqlPrepare = nullptr;
	isc_dsql_describeType m_pIscDsqlDescribe = nullptr;
	isc_sql_interpreteType m_pIscSqlInterprete = nullptr;
	isc_sqlcodeType m_pIscSqlcode = nullptr;
	isc_encode_timestampType m_pIscEncodeTimestamp = nullptr;
	isc_create_blob2Type m_pIscCreateBlob2 = nullptr;
	isc_open_blob2Type m_pIscOpenBlob2 = nullptr;
	isc_put_segmentType m_pIscPutSegment = nullptr;
	isc_close_blobType m_pIscCloseBlob = nullptr;
	isc_commit_retainingType m_pIscCommitRetaining = nullptr;
	isc_dsql_free_statementType m_pIscDsqlFreeStatement = nullptr;
	isc_dsql_describe_bindType m_pIscDsqlDescribeBind = nullptr;
	isc_dsql_executeType m_pIscDsqlExecute = nullptr;
	isc_dsql_sql_infoType m_pIscDsqlSqlInfo = nullptr;
	isc_vax_integerType m_pIscVaxInteger = nullptr;
	isc_dsql_execute2Type m_pIscDsqlExecute2 = nullptr;
	isc_dsql_fetchType m_pIscDsqlFetch = nullptr;
	isc_decode_timestampType m_pIscDecodeTimestamp = nullptr;
	isc_decode_sql_dateType m_pIscDecodeSqlDate = nullptr;
	isc_decode_sql_timeType m_pIscDecodeSqlTime = nullptr;
	isc_get_segmentType m_pIscGetSegment = nullptr;

	// Services API — for gbak/nbackup/sweep via fbclient without
	// shelling out to gbak.exe. Loaded best-effort: if the symbol
	// is missing (extremely old FB clients), the pointer stays
	// null and `ibFirebirdMaintenance` reports the operation as
	// unsupported instead of crashing.
	isc_service_attachType m_pIscServiceAttach = nullptr;
	isc_service_detachType m_pIscServiceDetach = nullptr;
	isc_service_startType  m_pIscServiceStart  = nullptr;
	isc_service_queryType  m_pIscServiceQuery  = nullptr;
};

#endif // __FIREBIRD_INTERFACES_H__
