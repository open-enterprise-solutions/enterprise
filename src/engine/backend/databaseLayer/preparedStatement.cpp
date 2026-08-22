////////////////////////////////////////////////////////////////////////////
//	Description : ibPreparedStatement — the parts that need the layer complete
//	              (preparedStatement.h)
////////////////////////////////////////////////////////////////////////////

#include "preparedStatement.h"

#include "databaseLayer.h"   // ibDatabaseLayer::ForgetStatement — the owner told in the dtor below

ibPreparedStatement::~ibPreparedStatement()
{
	// Out-of-line because it needs the layer complete, and in ITS OWN translation unit because it
	// belongs to this class — sitting in the layer's file only because that is where the layer was
	// already included would put one class's body in another's story.
	//
	// Every statement is made through ibDatabaseLayer, so the owner is set in exactly one place and
	// this needs no branch beyond "was anybody keeping books on me".
	if (m_owner != nullptr)
		m_owner->ForgetStatement(this);
}
