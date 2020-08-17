//	VQE
//	Copyright(C) 2020  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com
#pragma once

//
// Resources on memory management
//
// - https://akkadia.org/drepper/cpumemory.pdf
// - http://allenchou.net/memory-management-series/
// - https://www.gamasutra.com/blogs/MichaelKissner/20151104/258271/Writing_a_Game_Engine_from_Scratch__Part_2_Memory.php
// - https://gamasutra.com/blogs/MichaelKissner/20151120/259561/Writing_a_Game_Engine_from_Scratch__Part_3_Data__Cache.php
// - http://dmitrysoshnikov.com/compilers/writing-a-memory-allocator/
// - http://dmitrysoshnikov.com/compilers/writing-a-pool-allocator/
// - https://blog.molecular-matters.com/2012/09/17/memory-allocation-strategies-a-pool-allocator/
// 

// http://dmitrysoshnikov.com/compilers/writing-a-memory-allocator/
inline constexpr size_t AlignTo(size_t size, size_t alignment = 64)
{
	return (size + alignment - 1) & ~(alignment - 1);
}

#define MEMORY_POOL__ENABLE_DEBUG_LOG 1
#define MEMORY_POOL__LOG_VERBOSE      0
#if MEMORY_POOL__ENABLE_DEBUG_LOG
#include "../../Libs/VQUtils/Source/Log.h"
#include "../../Libs/VQUtils/Source/utils.h"
#endif


//
// MEMORY POOL
//
template<class TObject>
class MemoryPool
{
public:
	MemoryPool(size_t NumBlocks, size_t Alignment);
	~MemoryPool();
	
	TObject* Allocate(size_t NumBlocks = 1);
	void     Free(void* pBlock);


#if MEMORY_POOL__ENABLE_DEBUG_LOG
	void PrintDebugInfo() const;
#endif
private:
	struct Block { Block* pNext; };

	// memory
	Block* mpNextFreeBlock = nullptr;
	void*  mpAlloc         = nullptr;

	// header
	size_t mNumBlocks = 0;
	size_t mNumUsedBlocks = 0;
	size_t mAllocSize = 0;
};




//
// MemoryPool Template Implementation
//
template<class TObject>
inline MemoryPool<TObject>::MemoryPool(size_t NumBlocks, size_t Alignment)
	: mNumBlocks(NumBlocks)
{
	// calc alloc size
	const size_t AlignedObjSize = AlignTo(sizeof(TObject), Alignment);
	const size_t AllocSize = AlignedObjSize * NumBlocks;

	// alloc mem
	this->mpNextFreeBlock = reinterpret_cast<Block*>(malloc(AllocSize));
#if MEMORY_POOL__ENABLE_DEBUG_LOG 
	if (!this->mpNextFreeBlock) Log::Error("MemoryPool(NumBlocks=%d, Alignment=%d): malloc() failed", NumBlocks, Alignment); 
#endif
	assert(this->mpNextFreeBlock);
	this->mpAlloc = this->mpNextFreeBlock;
	this->mAllocSize = AllocSize;

	// setup list structure
	Block* pWalk = this->mpNextFreeBlock;
	Block* pNextBlock = (Block*)(((unsigned char*)this->mpNextFreeBlock) + AlignedObjSize);
	for (size_t i = 1; i < NumBlocks; ++i)
	{
		if (i == NumBlocks - 1)
		{
			pWalk->pNext = nullptr;
			break;
		}
		pWalk->pNext = pNextBlock;
		pWalk = pNextBlock;
		pNextBlock = (Block*)((unsigned char*)(pNextBlock) + AlignedObjSize);
	}
	

#if MEMORY_POOL__ENABLE_DEBUG_LOG
	Log::Info("MemoryPool: Created pool w/ ObjectSize=%s, Alignment=%s, AlignedObjectSize=%s, NumBlocks=%d, AllocSize=%s, mpAlloc=0x%x"
		, StrUtil::FormatByte(sizeof(TObject)).c_str()
		, StrUtil::FormatByte(Alignment).c_str()
		, StrUtil::FormatByte(AlignedObjSize).c_str()
		, NumBlocks
		, StrUtil::FormatByte(AllocSize).c_str()
		, mpAlloc
	);
	PrintDebugInfo();
#endif
}

template<class TObject>
inline MemoryPool<TObject>::~MemoryPool()
{
	if (mNumUsedBlocks != 0)
	{
		Log::Warning("~MemoryPool() : mNumUsedBlocks != 0, did you Free() all allocated objects from the Scene?");

		// if you hit this, Scene has 'leaked' memory.
		// The application will still deallocate the memory and won't really leak, 
		// but the pointers that weren't freed will be dangling.
		///assert(mNumUsedBlocks == 0); 
	}

	if (mpAlloc)
		free(mpAlloc);
}

template<class TObject>
inline TObject* MemoryPool<TObject>::Allocate(size_t NumBlocks)
{
	// TODO: alloc NumBlocks?
	//for (size_t i = 0; i < NumBlocks; ++i)
	//{
	//	;
	//}

	if (!mpNextFreeBlock)
	{
		assert(this->mNumUsedBlocks == this->mNumBlocks);
		Log::Error("MemoryPool is out of memory");
		assert(mpNextFreeBlock); // if you hit this, allocate a bigger pool on startup
	}

	++this->mNumUsedBlocks;

	// only alloc 1 for now
	TObject* pNewObj = (TObject*)mpNextFreeBlock;
	mpNextFreeBlock = mpNextFreeBlock->pNext;
	return pNewObj;
}

template<class TObject>
inline void MemoryPool<TObject>::Free(void* pBlock)
{
	assert(pBlock);
	Block* pMem = (Block*)pBlock;
	pMem->pNext = this->mpNextFreeBlock;
	this->mpNextFreeBlock = pMem;
	--this->mNumUsedBlocks;
}


#if MEMORY_POOL__ENABLE_DEBUG_LOG
template<class TObject>
inline void MemoryPool<TObject>::PrintDebugInfo() const
{
	Log::Info("-----------------");
	Log::Info("Memory Pool");
	Log::Info("Allocation Size : %s", StrUtil::FormatByte(this->mAllocSize).c_str());
	Log::Info("Total # Blocks  : %d", this->mNumBlocks);
	Log::Info("Used  # Blocks  : %d", this->mNumUsedBlocks);
	Log::Info("Next Available  : 0x%x %s", this->mpNextFreeBlock, (this->mpNextFreeBlock == this->mpAlloc ? "(HEAD)" : ""));
	Log::Info("-----------------");
#if MEMORY_POOL__LOG_VERBOSE
	size_t iBlock = 0;
	Block* pWalk = mpNextFreeBlock;
	while (pWalk)
	{
		Log::Info("[%d] 0x%x", iBlock, pWalk);
		
		++iBlock;
		pWalk = pWalk->pNext;
	}
	Log::Info("-----------------");
#endif
}
#endif