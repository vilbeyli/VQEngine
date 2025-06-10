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

#include <vector>
#include "Libs/VQUtils/Include/Log.h"
#include "Libs/VQUtils/Include/utils.h"

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

#define MEMORY_POOL__ENABLE_DEBUG_LOG 0
#define MEMORY_POOL__LOG_VERBOSE      0


//
// MEMORY POOL
//
static const size_t INVALID_HANDLE = static_cast<size_t>(-1);
template<class TObject>
class MemoryPool
{
public:
	MemoryPool(size_t NumBlocks, size_t Alignment);
	~MemoryPool();
	
	size_t Allocate();
	std::vector<size_t> Allocate(size_t NumBlocks);

	void Free(size_t Handle);
	void Free(const std::vector<size_t>&  Handle);
	void FreeAll();

	TObject* Get(size_t Handle) const;
	inline size_t GetAliveObjectCount() const { return mNumUsedBlocks; };
	std::vector<const TObject*> GetAllAliveObjects() const;
	std::vector<size_t> GetAllAliveObjectHandles() const;

#if MEMORY_POOL__ENABLE_DEBUG_LOG
	void PrintDebugInfo() const;
#endif

private:
	struct Block { Block* pNext; };

	// memory
	Block* mpNextFreeBlock = nullptr;
	void*  mpAlloc         = nullptr;

	// header
	size_t mNumMaxBlocks = 0;
	size_t mNumUsedBlocks = 0;
	size_t mAllocSize = 0;
	size_t mAlignedObjSize = 0;

	// handles
	std::vector<bool> mActiveHandles;
};




//
// MemoryPool Template Implementation
//
template<class TObject>
inline MemoryPool<TObject>::MemoryPool(size_t NumBlocks, size_t Alignment)
	: mNumMaxBlocks(NumBlocks)
{
	// calc alloc size
	this->mAlignedObjSize = AlignTo(sizeof(TObject), Alignment);
	const size_t AllocSize = this->mAlignedObjSize * NumBlocks;

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
	Block* pNextBlock = (Block*)(((unsigned char*)this->mpNextFreeBlock) + mAlignedObjSize);
	for (size_t i = 0; i < NumBlocks; ++i)
	{
		if (i == NumBlocks - 1)
		{
			pWalk->pNext = nullptr;
			break;
		}
		pWalk->pNext = pNextBlock;
		pWalk = pNextBlock;
		pNextBlock = (Block*)((unsigned char*)(pNextBlock) +mAlignedObjSize);
	}
	

#if MEMORY_POOL__ENABLE_DEBUG_LOG
	Log::Info("MemoryPool: Created pool w/ ObjectSize=%s, Alignment=%s, AlignedObjectSize=%s, NumBlocks=%d, AllocSize=%s, mpAlloc=0x%x"
		, StrUtil::FormatByte(sizeof(TObject)).c_str()
		, StrUtil::FormatByte(Alignment).c_str()
		, StrUtil::FormatByte(mAlignedObjSize).c_str()
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
inline size_t MemoryPool<TObject>::Allocate()
{
	if (!mpNextFreeBlock)
	{
		assert(this->mNumUsedBlocks == this->mNumMaxBlocks);
		Log::Warning("MemoryPool is out of memory (%s), perhaps consider a larger initial size?", StrUtil::FormatByte(this->mAllocSize).c_str());
#if 1
		assert(mpNextFreeBlock); // if you hit this, allocate a bigger pool on startup
#else
		// --------------------------------------------------------------------
		// TODO: FIX DANGLING POINTERS IN SCENE GAME OBJECT POINTER VECTOR
		// --------------------------------------------------------------------
		// dynamically grow (double) the pool size
		{
			const size_t Alignment = mAllocSize / mNumMaxBlocks;
			MemoryPool<TObject> NewPool(mNumMaxBlocks * 2, Alignment);
			Log::Info("\tAllocated new pool of size: %s", StrUtil::FormatByte(NewPool.mAllocSize).c_str());

			// advance the next free block pointer in the new pool before we copy the entire allocation
			for (int b = 0; b < mNumUsedBlocks; ++b)
				NewPool.mpNextFreeBlock = NewPool.mpNextFreeBlock->pNext;
			
			// copy the current objects to the new pool
			memcpy(NewPool.mpAlloc, this->mpAlloc, this->mAllocSize);
			
			// free the memory of the 'old'
			free(mpAlloc);
			mpAlloc = nullptr;
			
			// update the pointers
			std::swap(mpAlloc, NewPool.mpAlloc);
			mNumMaxBlocks = NewPool.mNumMaxBlocks;
			mAllocSize = NewPool.mAllocSize;
			mpNextFreeBlock = NewPool.mpNextFreeBlock;
		}
#endif

		return INVALID_HANDLE;
	}
	size_t Handle = (reinterpret_cast<unsigned char*>(mpNextFreeBlock) - reinterpret_cast<unsigned char*>(mpAlloc)) / mAlignedObjSize;

	// mark handle active
	if (Handle >= mActiveHandles.size()) {
		mActiveHandles.resize(Handle + 1, false);
	}
	mActiveHandles[Handle] = true;

	// update free list
	Block* pAllocatedBlock = mpNextFreeBlock;
	mpNextFreeBlock = mpNextFreeBlock->pNext;

	++this->mNumUsedBlocks;
	return Handle;
}

template<class TObject>
inline std::vector<size_t> MemoryPool<TObject>::Allocate(size_t NumBlocks)
{
	std::vector<size_t> Handles(NumBlocks, INVALID_HANDLE);
	for (size_t i = 0; i < NumBlocks; ++i)
		Handles[i] = Allocate();
	return Handles;
}

template<class TObject>
inline void MemoryPool<TObject>::Free(size_t Handle)
{
	if (Handle > mActiveHandles.size() || !mActiveHandles[Handle])
	{
		return;
	}
	
	// update free list
	Block* pBlockToFree = reinterpret_cast<Block*>(Get(Handle));
	assert(pBlockToFree);
	pBlockToFree->pNext = mpNextFreeBlock;
	mpNextFreeBlock = pBlockToFree;

	// update handle & state
	mActiveHandles[Handle] = false;
	--mNumUsedBlocks;
}

template<class TObject>
inline void MemoryPool<TObject>::Free(const std::vector<size_t>& Handles)
{
	for(size_t Handle : Handles)
		Free(Handle);
}

template<class TObject>
inline void MemoryPool<TObject>::FreeAll()
{
	for (size_t handle = 0; handle < this->mActiveHandles.size(); ++handle)
		if (this->mActiveHandles[handle])
			Free(handle);
}

template<class TObject> 
inline TObject* MemoryPool<TObject>::Get(size_t Handle) const
{
	if (Handle > mActiveHandles.size() || !mActiveHandles[Handle])
	{
		Log::Warning("Invalid handle requested from MemoryPool.");
		return nullptr;
	}

	return reinterpret_cast<TObject*>(
		reinterpret_cast<char*>(mpAlloc) + Handle * mAlignedObjSize
	);
}

template<class TObject>
inline std::vector<const TObject*> MemoryPool<TObject>::GetAllAliveObjects() const
{
	std::vector<const TObject*> Objects(this->mNumUsedBlocks, nullptr);
	for (size_t i = 0; i < mActiveHandles.size(); ++i)
	{
		if (mActiveHandles[i])
			Objects[i] = Get(i);
	}
	return Objects;
}

template<class TObject>
inline std::vector<size_t> MemoryPool<TObject>::GetAllAliveObjectHandles() const
{
	std::vector<size_t> Handles(this->mNumUsedBlocks, -1);
	size_t iHandle = 0;
	for (size_t i = 0; i < mActiveHandles.size(); ++i)
	{
		if (mActiveHandles[i])
			Handles[iHandle++] = i;
	}
	return Handles;
}

#if MEMORY_POOL__ENABLE_DEBUG_LOG
template<class TObject>
inline void MemoryPool<TObject>::PrintDebugInfo() const
{
	Log::Info("-----------------");
	Log::Info("Memory Pool");
	Log::Info("Allocation Size : %s", StrUtil::FormatByte(this->mAllocSize).c_str());
	Log::Info("Total # Blocks  : %d", this->mNumMaxBlocks);
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