////////////////////////////////////////////////////////////////////////////
//	ibTransactionTicket — what lives until a transaction ends (ibDatabaseLayer::AddTicket). The transaction keeps
//	its tickets and lets them go once the outermost Commit or RollBack has resolved, whichever it was; L2 knows
//	nothing of what a ticket holds — a lock's rows (ibLockHandle) go in its destructor.
////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TRANSACTION_TICKET_H_
#define _IB_TRANSACTION_TICKET_H_

#include "backend/backend.h"

class BACKEND_API ibTransactionTicket
{
public:
	virtual ~ibTransactionTicket() = default;
};

#endif // _IB_TRANSACTION_TICKET_H_
