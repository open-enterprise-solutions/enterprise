/////////////////////////////////////////////////////////////////////////////
// Name:        wxmd5.h
// Purpose:     This implements the md5 hash algorithm
// Author:      Brice André
// Created:     2010/12/05
// RCS-ID:      $Id: mycomp.h 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2010 Brice André
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_MD5_H_
#define _WX_MD5_H_

#include "core/core.h"

#include "core/fstring.h"

class CORE_API ibMD5 {
public:
	static ibString ComputeMd5(const ibString& content);
	// ...the digest of the bytes themselves: a text already in bytes is not made a string first.
	static ibString ComputeMd5(const void* data, size_t size);
	static ibString ComputeKeyedMd5(const ibString& content, const ibString& key);
};

#endif /* _WX_MD5_H_ */
