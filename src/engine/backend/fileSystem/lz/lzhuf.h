#ifndef _LZ_H_
#define _LZ_H_

#include "fileSystem/types.h"

unsigned	_writeLZ		(int hf, void* d, unsigned size);
unsigned	_readLZ			(int hf, void* &d, unsigned size);

void		_compressLZ		(u8** dest, unsigned* dest_sz, void* src, unsigned src_sz);
void		_decompressLZ	(u8** dest, unsigned* dest_sz, void* src, unsigned src_sz);

#endif
