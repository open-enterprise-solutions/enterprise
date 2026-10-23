#ifndef _LZ_H_
#define _LZ_H_

#include <cstdint>

unsigned	_writeLZ		(int hf, void* d, unsigned size);
unsigned	_readLZ			(int hf, void* &d, unsigned size);

void		_compressLZ		(std::uint8_t** dest, unsigned* dest_sz, void* src, unsigned src_sz);
void		_decompressLZ	(std::uint8_t** dest, unsigned* dest_sz, void* src, unsigned src_sz);

#endif
