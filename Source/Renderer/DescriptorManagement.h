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

//----------------------------------------------------------------------------------
// Adopted from Diligent Graphics 
// https://github.com/DiligentGraphics/DiligentCore/blob/a9002525abad03a8e5aab27daf2c2a0093fb47f6/Graphics/GraphicsEngineD3D12/include/DescriptorHeap.hpp
// See http://diligentgraphics.com/diligent-engine/architecture/d3d12/managing-descriptor-heaps/ for details
/*
 *  Copyright 2019-2020 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */
//----------------------------------------------------------------------------------
#define NOMINMAX

#include "Renderer.h"

#include "../Application/Types.h"

#include <d3d12.h>
#include <atlbase.h>

#include <utility>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <map>

#ifdef DILIGENT_DEBUG // TODO: 

#    include <typeinfo>

#    define ASSERTION_FAILED(Message, ...)                                       \
        do                                                                       \
        {                                                                        \
            auto msg = Diligent::FormatString(Message, ##__VA_ARGS__);           \
            DebugAssertionFailed(msg.c_str(), __FUNCTION__, __FILE__, __LINE__); \
        } while (false)

#    define VERIFY(Expr, Message, ...)                    \
        do                                                \
        {                                                 \
            if (!(Expr))                                  \
            {                                             \
                ASSERTION_FAILED(Message, ##__VA_ARGS__); \
            }                                             \
        } while (false)

#    define UNEXPECTED  ASSERTION_FAILED
#    define UNSUPPORTED ASSERTION_FAILED

#    define VERIFY_EXPR(Expr) VERIFY(Expr, "Debug exression failed:\n", #    Expr)


template <typename DstType, typename SrcType>
void CheckDynamicType(SrcType* pSrcPtr)
{
    VERIFY(pSrcPtr == nullptr || dynamic_cast<DstType*>(pSrcPtr) != nullptr, "Dynamic type cast failed. Src typeid: \'", typeid(*pSrcPtr).name(), "\' Dst typeid: \'", typeid(DstType).name(), '\'');
}
#    define CHECK_DYNAMIC_TYPE(DstType, pSrcPtr) \
        do                                       \
        {                                        \
            CheckDynamicType<DstType>(pSrcPtr);  \
        } while (false)


#else

// clang-format off
#    define CHECK_DYNAMIC_TYPE(...) do{}while(false)
#    define VERIFY(...)do{}while(false)
#    define UNEXPECTED(...)do{}while(false)
#    define UNSUPPORTED(...)do{}while(false)
#    define VERIFY_EXPR(...)do{}while(false)
// clang-format on

#endif // DILIGENT_DEBUG


namespace Diligent
{
// Type mapping with VQE
using String = std::string;
using Char = char;
using Int32 = int32;
using Uint32 = uint32;
using Uint16 = uint16;
using Uint64 = uint64;
using RenderDeviceD3D12Impl = VQRenderer;



//----------------------------------------------------------------------------------
//
// MemoryAllocator.h
//
//----------------------------------------------------------------------------------
/// Base interface for a raw memory allocator
struct IMemoryAllocator
{
    /// Allocates block of memory
    virtual void* Allocate(size_t Size, const Char* dbgDescription, const char* dbgFileName, const Int32 dbgLineNumber) = 0;

    /// Releases memory
    virtual void Free(void* Ptr) = 0;
};



//----------------------------------------------------------------------------------
//
// Align.hpp
//
//----------------------------------------------------------------------------------
template <typename T>
bool IsPowerOfTwo(T val)
{
    return val > 0 && (val & (val - 1)) == 0;
}

template <typename T>
inline T Align(T val, T alignment)
{
    VERIFY(IsPowerOfTwo(alignment), "Alignment (", alignment, ") must be power of 2");
    return (val + (alignment - 1)) & ~(alignment - 1);
}

template <typename T>
inline T AlignDown(T val, T alignment)
{
    VERIFY(IsPowerOfTwo(alignment), "Alignment (", alignment, ") must be power of 2");
    return val & ~(alignment - 1);
}



//----------------------------------------------------------------------------------
//
// STDAllocator.hpp
//
//----------------------------------------------------------------------------------
template <typename T>
typename std::enable_if<std::is_destructible<T>::value, void>::type Destruct(T* ptr)
{
    ptr->~T();
}

template <typename T>
typename std::enable_if<!std::is_destructible<T>::value, void>::type Destruct(T* ptr)
{
}

template <typename T, typename AllocatorType>
struct STDAllocator
{
    using value_type      = T;
    using pointer         = value_type*;
    using const_pointer   = const value_type*;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    STDAllocator(AllocatorType& Allocator, const Char* Description, const Char* FileName, const Int32 LineNumber) noexcept :
        // clang-format off
        m_Allocator     {Allocator}
#ifdef DILIGENT_DEVELOPMENT
      , m_dvpDescription{Description}
      , m_dvpFileName   {FileName   }
      , m_dvpLineNumber {LineNumber }
#endif
    // clang-format on
    {
    }

    template <class U>
    STDAllocator(const STDAllocator<U, AllocatorType>& other) noexcept :
        // clang-format off
        m_Allocator     {other.m_Allocator}
#ifdef DILIGENT_DEVELOPMENT
      , m_dvpDescription{other.m_dvpDescription}
      , m_dvpFileName   {other.m_dvpFileName   }
      , m_dvpLineNumber {other.m_dvpLineNumber }
#endif
    // clang-format on
    {
    }

    template <class U>
    STDAllocator(STDAllocator<U, AllocatorType>&& other) noexcept :
        // clang-format off
        m_Allocator     {other.m_Allocator}
#ifdef DILIGENT_DEVELOPMENT
      , m_dvpDescription{other.m_dvpDescription}
      , m_dvpFileName   {other.m_dvpFileName   }
      , m_dvpLineNumber {other.m_dvpLineNumber }
#endif
    // clang-format on
    {
    }

    template <class U>
    STDAllocator& operator=(STDAllocator<U, AllocatorType>&& other) noexcept
    {
        // Android build requires this operator to be defined - I have no idea why.
        // There is no default constructor to create null allocator, so all fields must be
        // initialized.
        DEV_CHECK_ERR(&m_Allocator == &other.m_Allocator, "Inconsistent allocators");
#ifdef DILIGENT_DEVELOPMENT
        DEV_CHECK_ERR(m_dvpDescription == other.m_dvpDescription, "Incosistent allocator descriptions");
        DEV_CHECK_ERR(m_dvpFileName == other.m_dvpFileName, "Incosistent allocator file names");
        DEV_CHECK_ERR(m_dvpLineNumber == other.m_dvpLineNumber, "Incosistent allocator line numbers");
#endif
        return *this;
    }

    template <class U> struct rebind
    {
        typedef STDAllocator<U, AllocatorType> other;
    };

    T* allocate(std::size_t count)
    {
#ifndef DILIGENT_DEVELOPMENT
        static constexpr const char* m_dvpDescription = "<Unavailable in release build>";
        static constexpr const char* m_dvpFileName    = "<Unavailable in release build>";
        static constexpr Int32       m_dvpLineNumber  = -1;
#endif
        return reinterpret_cast<T*>(m_Allocator.Allocate(count * sizeof(T), m_dvpDescription, m_dvpFileName, m_dvpLineNumber));
    }

    pointer       address(reference r) { return &r; }
    const_pointer address(const_reference r) { return &r; }

    void deallocate(T* p, std::size_t count)
    {
        m_Allocator.Free(p);
    }

    inline size_type max_size() const
    {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    //    construction/destruction
    template <class U, class... Args>
    void construct(U* p, Args&&... args)
    {
        ::new (p) U(std::forward<Args>(args)...);
    }

    inline void destroy(pointer p)
    {
        p->~T();
    }

    AllocatorType& m_Allocator;
#ifdef DILIGENT_DEVELOPMENT
    const Char* const m_dvpDescription;
    const Char* const m_dvpFileName;
    Int32 const       m_dvpLineNumber;
#endif
};

#define STD_ALLOCATOR(Type, AllocatorType, Allocator, Description) STDAllocator<Type, AllocatorType>(Allocator, Description, __FILE__, __LINE__)

template <class T, class U, class A>
bool operator==(const STDAllocator<T, A>& left, const STDAllocator<U, A>& right)
{
    return &left.m_Allocator == &right.m_Allocator;
}

template <class T, class U, class A>
bool operator!=(const STDAllocator<T, A>& left, const STDAllocator<U, A>& right)
{
    return !(left == right);
}

template <class T> using STDAllocatorRawMem = STDAllocator<T, IMemoryAllocator>;
#define STD_ALLOCATOR_RAW_MEM(Type, Allocator, Description) STDAllocatorRawMem<Type>(Allocator, Description, __FILE__, __LINE__)

template <class T, typename AllocatorType>
struct STDDeleter
{
    STDDeleter() noexcept {}

    STDDeleter(AllocatorType& Allocator) noexcept :
        m_Allocator{&Allocator}
    {}

    STDDeleter(const STDDeleter&) = default;
    STDDeleter& operator=(const STDDeleter&) = default;

    STDDeleter(STDDeleter&& rhs) noexcept :
        m_Allocator{rhs.m_Allocator}
    {
        rhs.m_Allocator = nullptr;
    }

    STDDeleter& operator=(STDDeleter&& rhs) noexcept
    {
        m_Allocator     = rhs.m_Allocator;
        rhs.m_Allocator = nullptr;
        return *this;
    }

    void operator()(T* ptr) noexcept
    {
        VERIFY(m_Allocator != nullptr, "The deleter has been moved away or never initialized, and can't be used");
        Destruct(ptr);
        m_Allocator->Free(ptr);
    }

private:
    AllocatorType* m_Allocator = nullptr;
};
template <class T> using STDDeleterRawMem = STDDeleter<T, IMemoryAllocator>;


//----------------------------------------------------------------------------------
//
// VariableSizeAllocationsManager.hpp
//
//----------------------------------------------------------------------------------
// The class handles free memory block management to accommodate variable-size allocation requests.
// It keeps track of free blocks only and does not record allocation sizes. The class uses two ordered maps
// to facilitate operations. The first map keeps blocks sorted by their offsets. The second multimap keeps blocks
// sorted by their sizes. The elements of the two maps reference each other, which enables efficient block
// insertion, removal and merging.
//
//   8                 32                       64                           104
//   |<---16--->|       |<-----24------>|        |<---16--->|                 |<-----32----->|
//
//
//        m_FreeBlocksBySize      m_FreeBlocksByOffset
//           size->offset            offset->size
//
//                16 ------------------>  8  ---------->  {size = 16, &m_FreeBlocksBySize[0]}
//
//                16 ------.   .-------> 32  ---------->  {size = 24, &m_FreeBlocksBySize[2]}
//                          '.'
//                24 -------' '--------> 64  ---------->  {size = 16, &m_FreeBlocksBySize[1]}
//
//                32 ------------------> 104 ---------->  {size = 32, &m_FreeBlocksBySize[3]}
//
class VariableSizeAllocationsManager
{
public:
    using OffsetType = size_t;

private:
    struct FreeBlockInfo;

    // Type of the map that keeps memory blocks sorted by their offsets
    using TFreeBlocksByOffsetMap =
        std::map<OffsetType,
                 FreeBlockInfo,
                 std::less<OffsetType>,                                         // Standard ordering
                 STDAllocatorRawMem<std::pair<const OffsetType, FreeBlockInfo>> // Raw memory allocator
                 >;

    // Type of the map that keeps memory blocks sorted by their sizes
    using TFreeBlocksBySizeMap =
        std::multimap<OffsetType,
                      TFreeBlocksByOffsetMap::iterator,
                      std::less<OffsetType>,                                                            // Standard ordering
                      STDAllocatorRawMem<std::pair<const OffsetType, TFreeBlocksByOffsetMap::iterator>> // Raw memory allocator
                      >;

    struct FreeBlockInfo
    {
        // Block size (no reserved space for the size of the allocation)
        OffsetType Size;

        // Iterator referencing this block in the multimap sorted by the block size
        TFreeBlocksBySizeMap::iterator OrderBySizeIt;

        FreeBlockInfo(OffsetType _Size) :
            Size(_Size) {}
    };

public:
    VariableSizeAllocationsManager(OffsetType MaxSize, IMemoryAllocator& Allocator) :
        m_FreeBlocksByOffset(STD_ALLOCATOR_RAW_MEM(TFreeBlocksByOffsetMap::value_type, Allocator, "Allocator for map<OffsetType, FreeBlockInfo>")),
        m_FreeBlocksBySize(STD_ALLOCATOR_RAW_MEM(TFreeBlocksBySizeMap::value_type, Allocator, "Allocator for multimap<OffsetType, TFreeBlocksByOffsetMap::iterator>")),
        m_MaxSize(MaxSize),
        m_FreeSize(MaxSize)
    {
        // Insert single maximum-size block
        AddNewBlock(0, m_MaxSize);
        ResetCurrAlignment();

#ifdef DILIGENT_DEBUG
        DbgVerifyList();
#endif
    }

    ~VariableSizeAllocationsManager()
    {
#ifdef DILIGENT_DEBUG
        if (!m_FreeBlocksByOffset.empty() || !m_FreeBlocksBySize.empty())
        {
            VERIFY(m_FreeBlocksByOffset.size() == 1, "Single free block is expected");
            VERIFY(m_FreeBlocksByOffset.begin()->first == 0, "Head chunk offset is expected to be 0");
            VERIFY(m_FreeBlocksByOffset.begin()->second.Size == m_MaxSize, "Head chunk size is expected to be ", m_MaxSize);
            VERIFY_EXPR(m_FreeBlocksByOffset.begin()->second.OrderBySizeIt == m_FreeBlocksBySize.begin());
            VERIFY(m_FreeBlocksBySize.size() == m_FreeBlocksByOffset.size(), "Sizes of the two maps must be equal");

            VERIFY(m_FreeBlocksBySize.size() == 1, "Single free block is expected");
            VERIFY(m_FreeBlocksBySize.begin()->first == m_MaxSize, "Head chunk size is expected to be ", m_MaxSize);
            VERIFY(m_FreeBlocksBySize.begin()->second == m_FreeBlocksByOffset.begin(), "Incorrect first block");
        }
#endif
    }

    // clang-format off
    VariableSizeAllocationsManager(VariableSizeAllocationsManager&& rhs) noexcept :
        m_FreeBlocksByOffset {std::move(rhs.m_FreeBlocksByOffset)},
        m_FreeBlocksBySize   {std::move(rhs.m_FreeBlocksBySize)  },
        m_MaxSize            {rhs.m_MaxSize      },
        m_FreeSize           {rhs.m_FreeSize     },
        m_CurrAlignment      {rhs.m_CurrAlignment}
    {
        // clang-format on
        rhs.m_MaxSize       = 0;
        rhs.m_FreeSize      = 0;
        rhs.m_CurrAlignment = 0;
    }

    // clang-format off
    VariableSizeAllocationsManager& operator = (VariableSizeAllocationsManager&& rhs) = default;
    VariableSizeAllocationsManager             (const VariableSizeAllocationsManager&) = delete;
    VariableSizeAllocationsManager& operator = (const VariableSizeAllocationsManager&) = delete;
    // clang-format on

    // Offset returned by Allocate() may not be aligned, but the size of the allocation
    // is sufficient to properly align it
    struct Allocation
    {
        // clang-format off
        Allocation(OffsetType offset, OffsetType size) :
            UnalignedOffset{offset},
            Size           {size  }
        {}
        // clang-format on

        Allocation() {}

        static constexpr OffsetType InvalidOffset = static_cast<OffsetType>(-1);
        static Allocation           InvalidAllocation()
        {
            return Allocation{InvalidOffset, 0};
        }

        bool IsValid() const
        {
            return UnalignedOffset != InvalidAllocation().UnalignedOffset;
        }

        bool operator==(const Allocation& rhs) const
        {
            return UnalignedOffset == rhs.UnalignedOffset &&
                Size == rhs.Size;
        }

        OffsetType UnalignedOffset = InvalidOffset;
        OffsetType Size            = 0;
    };

    Allocation Allocate(OffsetType Size, OffsetType Alignment)
    {
        VERIFY_EXPR(Size > 0);
        VERIFY(IsPowerOfTwo(Alignment), "Alignment (", Alignment, ") must be power of 2");
        Size = Align(Size, Alignment);
        if (m_FreeSize < Size)
            return Allocation::InvalidAllocation();

        auto AlignmentReserve = (Alignment > m_CurrAlignment) ? Alignment - m_CurrAlignment : 0;
        // Get the first block that is large enough to encompass Size + AlignmentReserve bytes
        // lower_bound() returns an iterator pointing to the first element that
        // is not less (i.e. >= ) than key
        auto SmallestBlockItIt = m_FreeBlocksBySize.lower_bound(Size + AlignmentReserve);
        if (SmallestBlockItIt == m_FreeBlocksBySize.end())
            return Allocation::InvalidAllocation();

        auto SmallestBlockIt = SmallestBlockItIt->second;
        VERIFY_EXPR(Size + AlignmentReserve <= SmallestBlockIt->second.Size);
        VERIFY_EXPR(SmallestBlockIt->second.Size == SmallestBlockItIt->first);

        //     SmallestBlockIt.Offset
        //        |                                  |
        //        |<------SmallestBlockIt.Size------>|
        //        |<------Size------>|<---NewSize--->|
        //        |                  |
        //      Offset              NewOffset
        //
        auto Offset = SmallestBlockIt->first;
        VERIFY_EXPR(Offset % m_CurrAlignment == 0);
        auto AlignedOffset = Align(Offset, Alignment);
        auto AdjustedSize  = Size + (AlignedOffset - Offset);
        VERIFY_EXPR(AdjustedSize <= Size + AlignmentReserve);
        auto NewOffset = Offset + AdjustedSize;
        auto NewSize   = SmallestBlockIt->second.Size - AdjustedSize;
        VERIFY_EXPR(SmallestBlockItIt == SmallestBlockIt->second.OrderBySizeIt);
        m_FreeBlocksBySize.erase(SmallestBlockItIt);
        m_FreeBlocksByOffset.erase(SmallestBlockIt);
        if (NewSize > 0)
        {
            AddNewBlock(NewOffset, NewSize);
        }

        m_FreeSize -= AdjustedSize;

        if ((Size & (m_CurrAlignment - 1)) != 0)
        {
            if (IsPowerOfTwo(Size))
            {
                VERIFY_EXPR(Size >= Alignment && Size < m_CurrAlignment);
                m_CurrAlignment = Size;
            }
            else
            {
                m_CurrAlignment = std::min(m_CurrAlignment, Alignment);
            }
        }

#ifdef DILIGENT_DEBUG
        DbgVerifyList();
#endif
        return Allocation{Offset, AdjustedSize};
    }

    void Free(Allocation&& allocation)
    {
        Free(allocation.UnalignedOffset, allocation.Size);
        allocation = Allocation{};
    }

    void Free(OffsetType Offset, OffsetType Size)
    {
        VERIFY_EXPR(Offset + Size <= m_MaxSize);

        // Find the first element whose offset is greater than the specified offset.
        // upper_bound() returns an iterator pointing to the first element in the
        // container whose key is considered to go after k.
        auto NextBlockIt = m_FreeBlocksByOffset.upper_bound(Offset);
#ifdef DILIGENT_DEBUG
        {
            auto LowBnd = m_FreeBlocksByOffset.lower_bound(Offset); // First element whose offset is  >=
            // Since zero-size allocations are not allowed, lower bound must always be equal to the upper bound
            VERIFY_EXPR(LowBnd == NextBlockIt);
        }
#endif
        // Block being deallocated must not overlap with the next block
        VERIFY_EXPR(NextBlockIt == m_FreeBlocksByOffset.end() || Offset + Size <= NextBlockIt->first);
        auto PrevBlockIt = NextBlockIt;
        if (PrevBlockIt != m_FreeBlocksByOffset.begin())
        {
            --PrevBlockIt;
            // Block being deallocated must not overlap with the previous block
            VERIFY_EXPR(Offset >= PrevBlockIt->first + PrevBlockIt->second.Size);
        }
        else
            PrevBlockIt = m_FreeBlocksByOffset.end();

        OffsetType NewSize, NewOffset;
        if (PrevBlockIt != m_FreeBlocksByOffset.end() && Offset == PrevBlockIt->first + PrevBlockIt->second.Size)
        {
            //  PrevBlock.Offset             Offset
            //       |                          |
            //       |<-----PrevBlock.Size----->|<------Size-------->|
            //
            NewSize   = PrevBlockIt->second.Size + Size;
            NewOffset = PrevBlockIt->first;

            if (NextBlockIt != m_FreeBlocksByOffset.end() && Offset + Size == NextBlockIt->first)
            {
                //   PrevBlock.Offset           Offset            NextBlock.Offset
                //     |                          |                    |
                //     |<-----PrevBlock.Size----->|<------Size-------->|<-----NextBlock.Size----->|
                //
                NewSize += NextBlockIt->second.Size;
                m_FreeBlocksBySize.erase(PrevBlockIt->second.OrderBySizeIt);
                m_FreeBlocksBySize.erase(NextBlockIt->second.OrderBySizeIt);
                // Delete the range of two blocks
                ++NextBlockIt;
                m_FreeBlocksByOffset.erase(PrevBlockIt, NextBlockIt);
            }
            else
            {
                //   PrevBlock.Offset           Offset                     NextBlock.Offset
                //     |                          |                             |
                //     |<-----PrevBlock.Size----->|<------Size-------->| ~ ~ ~  |<-----NextBlock.Size----->|
                //
                m_FreeBlocksBySize.erase(PrevBlockIt->second.OrderBySizeIt);
                m_FreeBlocksByOffset.erase(PrevBlockIt);
            }
        }
        else if (NextBlockIt != m_FreeBlocksByOffset.end() && Offset + Size == NextBlockIt->first)
        {
            //   PrevBlock.Offset                   Offset            NextBlock.Offset
            //     |                                  |                    |
            //     |<-----PrevBlock.Size----->| ~ ~ ~ |<------Size-------->|<-----NextBlock.Size----->|
            //
            NewSize   = Size + NextBlockIt->second.Size;
            NewOffset = Offset;
            m_FreeBlocksBySize.erase(NextBlockIt->second.OrderBySizeIt);
            m_FreeBlocksByOffset.erase(NextBlockIt);
        }
        else
        {
            //   PrevBlock.Offset                   Offset                     NextBlock.Offset
            //     |                                  |                            |
            //     |<-----PrevBlock.Size----->| ~ ~ ~ |<------Size-------->| ~ ~ ~ |<-----NextBlock.Size----->|
            //
            NewSize   = Size;
            NewOffset = Offset;
        }

        AddNewBlock(NewOffset, NewSize);

        m_FreeSize += Size;
        if (IsEmpty())
        {
            // Reset current alignment
            VERIFY_EXPR(GetNumFreeBlocks() == 1);
            ResetCurrAlignment();
        }

#ifdef DILIGENT_DEBUG
        DbgVerifyList();
#endif
    }

    // clang-format off
    bool IsFull() const{ return m_FreeSize==0; };
    bool IsEmpty()const{ return m_FreeSize==m_MaxSize; };
    OffsetType GetMaxSize() const{return m_MaxSize;}
    OffsetType GetFreeSize()const{return m_FreeSize;}
    OffsetType GetUsedSize()const{return m_MaxSize - m_FreeSize;}
    // clang-format on

    size_t GetNumFreeBlocks() const
    {
        return m_FreeBlocksByOffset.size();
    }

    void Extend(size_t ExtraSize)
    {
        size_t NewBlockOffset = m_MaxSize;
        size_t NewBlockSize   = ExtraSize;

        if (!m_FreeBlocksByOffset.empty())
        {
            auto LastBlockIt = m_FreeBlocksByOffset.end();
            --LastBlockIt;

            const auto LastBlockOffset = LastBlockIt->first;
            const auto LastBlockSize   = LastBlockIt->second.Size;
            if (LastBlockOffset + LastBlockSize == m_MaxSize)
            {
                // Extend the last block
                NewBlockOffset = LastBlockOffset;
                NewBlockSize += LastBlockSize;

                VERIFY_EXPR(LastBlockIt->second.OrderBySizeIt->first == LastBlockSize &&
                            LastBlockIt->second.OrderBySizeIt->second == LastBlockIt);
                m_FreeBlocksBySize.erase(LastBlockIt->second.OrderBySizeIt);
                m_FreeBlocksByOffset.erase(LastBlockIt);
            }
        }

        AddNewBlock(NewBlockOffset, NewBlockSize);

        m_MaxSize += ExtraSize;
        m_FreeSize += ExtraSize;

#ifdef DILIGENT_DEBUG
        DbgVerifyList();
#endif
    }

private:
    void AddNewBlock(OffsetType Offset, OffsetType Size)
    {
        auto NewBlockIt = m_FreeBlocksByOffset.emplace(Offset, Size);
        VERIFY_EXPR(NewBlockIt.second);
        auto OrderIt                           = m_FreeBlocksBySize.emplace(Size, NewBlockIt.first);
        NewBlockIt.first->second.OrderBySizeIt = OrderIt;
    }

    void ResetCurrAlignment()
    {
        for (m_CurrAlignment = 1; m_CurrAlignment * 2 <= m_MaxSize; m_CurrAlignment *= 2)
        {}
    }

#ifdef DILIGENT_DEBUG
    void DbgVerifyList()
    {
        OffsetType TotalFreeSize = 0;

        VERIFY_EXPR(IsPowerOfTwo(m_CurrAlignment));
        auto BlockIt     = m_FreeBlocksByOffset.begin();
        auto PrevBlockIt = m_FreeBlocksByOffset.end();
        VERIFY_EXPR(m_FreeBlocksByOffset.size() == m_FreeBlocksBySize.size());
        while (BlockIt != m_FreeBlocksByOffset.end())
        {
            VERIFY_EXPR(BlockIt->first >= 0 && BlockIt->first + BlockIt->second.Size <= m_MaxSize);
            VERIFY((BlockIt->first & (m_CurrAlignment - 1)) == 0, "Block offset (", BlockIt->first, ") is not ", m_CurrAlignment, "-aligned");
            if (BlockIt->first + BlockIt->second.Size < m_MaxSize)
                VERIFY((BlockIt->second.Size & (m_CurrAlignment - 1)) == 0, "All block sizes except for the last one must be ", m_CurrAlignment, "-aligned");
            VERIFY_EXPR(BlockIt == BlockIt->second.OrderBySizeIt->second);
            VERIFY_EXPR(BlockIt->second.Size == BlockIt->second.OrderBySizeIt->first);
            //   PrevBlock.Offset                   BlockIt.first
            //     |                                  |
            // ~ ~ |<-----PrevBlock.Size----->| ~ ~ ~ |<------Size-------->| ~ ~ ~
            //
            VERIFY(PrevBlockIt == m_FreeBlocksByOffset.end() || BlockIt->first > PrevBlockIt->first + PrevBlockIt->second.Size, "Unmerged adjacent or overlapping blocks detected");
            TotalFreeSize += BlockIt->second.Size;

            PrevBlockIt = BlockIt;
            ++BlockIt;
        }

        auto OrderIt = m_FreeBlocksBySize.begin();
        while (OrderIt != m_FreeBlocksBySize.end())
        {
            VERIFY_EXPR(OrderIt->first == OrderIt->second->second.Size);
            ++OrderIt;
        }

        VERIFY_EXPR(TotalFreeSize == m_FreeSize);
    }
#endif

    TFreeBlocksByOffsetMap m_FreeBlocksByOffset;
    TFreeBlocksBySizeMap   m_FreeBlocksBySize;

    OffsetType m_MaxSize       = 0;
    OffsetType m_FreeSize      = 0;
    OffsetType m_CurrAlignment = 0;
    // When adding new members, do not forget to update move ctor
};


class DescriptorHeapAllocation;
class DescriptorHeapAllocationManager;

class IDescriptorAllocator
{
public:
    // Allocate Count descriptors
    virtual DescriptorHeapAllocation Allocate(uint32 Count)                                         = 0;
    virtual void                     Free(DescriptorHeapAllocation&& Allocation, uint64 CmdQueueMask) = 0;
    virtual uint32                   GetDescriptorSize() const                                        = 0;
};


// The class represents descriptor heap allocation (continuous descriptor range in a descriptor heap)
//
//                  m_FirstCpuHandle
//                   |
//  | ~  ~  ~  ~  ~  X  X  X  X  X  X  X  ~  ~  ~  ~  ~  ~ |  D3D12 Descriptor Heap
//                   |
//                  m_FirstGpuHandle
//
class DescriptorHeapAllocation
{
public:
    // Creates null allocation
    DescriptorHeapAllocation() noexcept :
        // clang-format off
        m_NumHandles      {1      }, // One null descriptor handle
        m_pDescriptorHeap {nullptr},
        m_DescriptorSize  {0      }
    // clang-format on
    {
        m_FirstCpuHandle.ptr = 0;
        m_FirstGpuHandle.ptr = 0;
    }

    // Initializes non-null allocation
    DescriptorHeapAllocation(IDescriptorAllocator&       Allocator,
                             ID3D12DescriptorHeap*       pHeap,
                             D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle,
                             D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle,
                             uint32                      NHandles,
                             uint16                      AllocationManagerId) noexcept :
        // clang-format off
        m_FirstCpuHandle      {CpuHandle          }, 
        m_FirstGpuHandle      {GpuHandle          },
        m_pAllocator          {&Allocator         },
        m_NumHandles          {NHandles           },
        m_pDescriptorHeap     {pHeap              },
        m_AllocationManagerId {AllocationManagerId}
    // clang-format on
    {
        VERIFY_EXPR(m_pAllocator != nullptr && m_pDescriptorHeap != nullptr);
        auto DescriptorSize = m_pAllocator->GetDescriptorSize();
        VERIFY(DescriptorSize < std::numeric_limits<uint16>::max(), "DescriptorSize exceeds allowed limit");
        m_DescriptorSize = static_cast<uint16>(DescriptorSize);
    }

    // Move constructor (copy is not allowed)
    DescriptorHeapAllocation(DescriptorHeapAllocation&& Allocation) noexcept :
        // clang-format off
        m_FirstCpuHandle      {std::move(Allocation.m_FirstCpuHandle)     },
        m_FirstGpuHandle      {std::move(Allocation.m_FirstGpuHandle)     },
        m_NumHandles          {std::move(Allocation.m_NumHandles)         },
        m_pAllocator          {std::move(Allocation.m_pAllocator)         },
        m_AllocationManagerId {std::move(Allocation.m_AllocationManagerId)},
        m_pDescriptorHeap     {std::move(Allocation.m_pDescriptorHeap)    },
        m_DescriptorSize      {std::move(Allocation.m_DescriptorSize)     }
    // clang-format on
    {
        Allocation.Reset();
    }

    // Move assignment (assignment is not allowed)
    DescriptorHeapAllocation& operator=(DescriptorHeapAllocation&& Allocation) noexcept
    {
        m_FirstCpuHandle      = std::move(Allocation.m_FirstCpuHandle);
        m_FirstGpuHandle      = std::move(Allocation.m_FirstGpuHandle);
        m_NumHandles          = std::move(Allocation.m_NumHandles);
        m_pAllocator          = std::move(Allocation.m_pAllocator);
        m_AllocationManagerId = std::move(Allocation.m_AllocationManagerId);
        m_pDescriptorHeap     = std::move(Allocation.m_pDescriptorHeap);
        m_DescriptorSize      = std::move(Allocation.m_DescriptorSize);

        Allocation.Reset();

        return *this;
    }

    void Reset()
    {
        m_FirstCpuHandle.ptr  = 0;
        m_FirstGpuHandle.ptr  = 0;
        m_pAllocator          = nullptr;
        m_pDescriptorHeap     = nullptr;
        m_NumHandles          = 0;
        m_AllocationManagerId = static_cast<uint16>(-1);
        m_DescriptorSize      = 0;
    }

    // clang-format off
    DescriptorHeapAllocation           (const DescriptorHeapAllocation&) = delete;
    DescriptorHeapAllocation& operator=(const DescriptorHeapAllocation&) = delete;
    // clang-format on


    // Destructor automatically releases this allocation through the allocator
    ~DescriptorHeapAllocation()
    {
        if (!IsNull() && m_pAllocator)
            m_pAllocator->Free(std::move(*this), ~uint64{0});
        // Allocation must have been disposed by the allocator
        VERIFY(IsNull(), "Non-null descriptor is being destroyed");
    }

    // Returns CPU descriptor handle at the specified offset
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(uint32 Offset = 0) const
    {
        VERIFY_EXPR(Offset >= 0 && Offset < m_NumHandles);

        D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = m_FirstCpuHandle;
        CPUHandle.ptr += m_DescriptorSize * Offset;

        return CPUHandle;
    }

    // Returns GPU descriptor handle at the specified offset
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32 Offset = 0) const
    {
        VERIFY_EXPR(Offset >= 0 && Offset < m_NumHandles);
        D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = m_FirstGpuHandle;
        GPUHandle.ptr += m_DescriptorSize * Offset;

        return GPUHandle;
    }

    // Returns pointer to D3D12 descriptor heap that contains this allocation
    ID3D12DescriptorHeap* GetDescriptorHeap() { return m_pDescriptorHeap; }


    // clang-format off
    size_t GetNumHandles()          const { return m_NumHandles;              }
    bool   IsNull()                 const { return m_FirstCpuHandle.ptr == 0; }
	bool   IsShaderVisible()        const { return m_FirstGpuHandle.ptr != 0; }
    size_t GetAllocationManagerId() const { return m_AllocationManagerId;     }
    UINT  GetDescriptorSize()       const { return m_DescriptorSize;          }
    // clang-format on

private:
    // First CPU descriptor handle in this allocation
    D3D12_CPU_DESCRIPTOR_HANDLE m_FirstCpuHandle = {0};

    // First GPU descriptor handle in this allocation
    D3D12_GPU_DESCRIPTOR_HANDLE m_FirstGpuHandle = {0};

    // Keep strong reference to the parent heap to make sure it is alive while allocation is alive - TOO EXPENSIVE
    //RefCntAutoPtr<IDescriptorAllocator> m_pAllocator;

    // Pointer to the descriptor heap allocator that created this allocation
    IDescriptorAllocator* m_pAllocator = nullptr;

    // Pointer to the D3D12 descriptor heap that contains descriptors in this allocation
    ID3D12DescriptorHeap* m_pDescriptorHeap = nullptr;

    // Number of descriptors in the allocation
    uint32 m_NumHandles = 0;

    // Allocation manager ID. One allocator may support several
    // allocation managers. This field is required to identify
    // the manager within the allocator that was used to create
    // this allocation
    uint16 m_AllocationManagerId = static_cast<uint16>(-1);

    // Descriptor size
    uint16 m_DescriptorSize = 0;
};


// The class performs suballocations within one D3D12 descriptor heap.
// It uses VariableSizeAllocationsManager to manage free space in the heap
//
// |  X  X  X  X  O  O  O  X  X  O  O  X  O  O  O  O  |  D3D12 descriptor heap
//
//  X - used descriptor
//  O - available descriptor
//
class DescriptorHeapAllocationManager
{
public:
    // Creates a new D3D12 descriptor heap
    DescriptorHeapAllocationManager(IMemoryAllocator&                 Allocator,
                                    RenderDeviceD3D12Impl&            DeviceD3D12Impl,
                                    IDescriptorAllocator&             ParentAllocator,
                                    size_t                            ThisManagerId,
                                    const D3D12_DESCRIPTOR_HEAP_DESC& HeapDesc);

    // Uses subrange of descriptors in the existing D3D12 descriptor heap
    // that starts at offset FirstDescriptor and uses NumDescriptors descriptors
    DescriptorHeapAllocationManager(IMemoryAllocator&      Allocator,
                                    RenderDeviceD3D12Impl& DeviceD3D12Impl,
                                    IDescriptorAllocator&  ParentAllocator,
                                    size_t                 ThisManagerId,
                                    ID3D12DescriptorHeap*  pd3d12DescriptorHeap,
                                    uint32                 FirstDescriptor,
                                    uint32                 NumDescriptors);


    // = default causes compiler error when instantiating std::vector::emplace_back() in Visual Studio 2015 (Version 14.0.23107.0 D14REL)
    DescriptorHeapAllocationManager(DescriptorHeapAllocationManager&& rhs) noexcept :
        // clang-format off
        m_ParentAllocator           {rhs.m_ParentAllocator           },
        m_DeviceD3D12Impl           {rhs.m_DeviceD3D12Impl           },
        m_ThisManagerId             {rhs.m_ThisManagerId             },
        m_HeapDesc                  {rhs.m_HeapDesc                  },
        m_DescriptorSize            {rhs.m_DescriptorSize            },
        m_NumDescriptorsInAllocation{rhs.m_NumDescriptorsInAllocation},
	    m_FirstCPUHandle            {rhs.m_FirstCPUHandle            },
        m_FirstGPUHandle            {rhs.m_FirstGPUHandle            },
        m_MaxAllocatedSize          {rhs.m_MaxAllocatedSize          },
        // Mutex is not movable
        //m_FreeBlockManagerMutex     (std::move(rhs.m_FreeBlockManagerMutex))
        m_FreeBlockManager          {std::move(rhs.m_FreeBlockManager)    },
        m_pd3d12DescriptorHeap      {std::move(rhs.m_pd3d12DescriptorHeap)}
    // clang-format on
    {
        rhs.m_NumDescriptorsInAllocation = 0; // Must be set to zero so that debug check in dtor passes
        rhs.m_ThisManagerId              = static_cast<size_t>(-1);
        rhs.m_FirstCPUHandle.ptr         = 0;
        rhs.m_FirstGPUHandle.ptr         = 0;
        rhs.m_MaxAllocatedSize           = 0;
#ifdef DILIGENT_DEVELOPMENT
        m_AllocationsCounter.store(rhs.m_AllocationsCounter.load());
        rhs.m_AllocationsCounter = 0;
#endif
    }

    // clang-format off
    // No copies or move-assignments
    DescriptorHeapAllocationManager& operator = (DescriptorHeapAllocationManager&&)      = delete;
    DescriptorHeapAllocationManager             (const DescriptorHeapAllocationManager&) = delete;
    DescriptorHeapAllocationManager& operator = (const DescriptorHeapAllocationManager&) = delete;
    // clang-format on

    ~DescriptorHeapAllocationManager();

    // Allocates Count descriptors
    DescriptorHeapAllocation Allocate(uint32 Count);
    void                     FreeAllocation(DescriptorHeapAllocation&& Allocation);

    // clang-format off
    size_t GetNumAvailableDescriptors()const { return m_FreeBlockManager.GetFreeSize(); }
	uint32 GetMaxDescriptors()         const { return m_NumDescriptorsInAllocation;     }
    size_t GetMaxAllocatedSize()       const { return m_MaxAllocatedSize;               }
    // clang-format on

#ifdef DILIGENT_DEVELOPMENT
    int32_t DvpGetAllocationsCounter() const
    {
        return m_AllocationsCounter;
    }
#endif

private:
    IDescriptorAllocator&  m_ParentAllocator;
    RenderDeviceD3D12Impl& m_DeviceD3D12Impl;

    // External ID assigned to this descriptor allocations manager
    size_t m_ThisManagerId = static_cast<size_t>(-1);

    // Heap description
    const D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;

    const UINT m_DescriptorSize = 0;

    // Number of descriptors in the allocation.
    // If this manager was initialized as a subrange in the existing heap,
    // this value may be different from m_HeapDesc.NumDescriptors
    uint32 m_NumDescriptorsInAllocation = 0;

    // Allocations manager used to handle descriptor allocations within the heap
    std::mutex                     m_FreeBlockManagerMutex;
    VariableSizeAllocationsManager m_FreeBlockManager;

    // Strong reference to D3D12 descriptor heap object
    CComPtr<ID3D12DescriptorHeap> m_pd3d12DescriptorHeap;

    // First CPU descriptor handle in the available descriptor range
    D3D12_CPU_DESCRIPTOR_HANDLE m_FirstCPUHandle = {0};

    // First GPU descriptor handle in the available descriptor range
    D3D12_GPU_DESCRIPTOR_HANDLE m_FirstGPUHandle = {0};

    size_t m_MaxAllocatedSize = 0;

#ifdef DILIGENT_DEVELOPMENT
    std::atomic_int32_t m_AllocationsCounter = 0;
#endif

    // Note: when adding new members, do not forget to update move ctor
};

// CPU descriptor heap is intended to provide storage for resource view descriptor handles.
// It contains a pool of DescriptorHeapAllocationManager object instances, where every instance manages
// its own CPU-only D3D12 descriptor heap:
//
//           m_HeapPool[0]                m_HeapPool[1]                 m_HeapPool[2]
//   |  X  X  X  X  X  X  X  X |, |  X  X  X  O  O  X  X  O  |, |  X  O  O  O  O  O  O  O  |
//
//    X - used descriptor                m_AvailableHeaps = {1,2}
//    O - available descriptor
//
// Allocation routine goes through the list of managers that have available descriptors and tries to process
// the request using every manager. If there are no available managers or no manager was able to handle the request,
// the function creates a new descriptor heap manager and lets it handle the request
//
// Render device contains four CPUDescriptorHeap object instances (one for each D3D12 heap type). The heaps are accessed
// when a texture or a buffer view is created.
//
class CPUDescriptorHeap final : public IDescriptorAllocator
{
public:
    // Initializes the heap
    CPUDescriptorHeap(IMemoryAllocator&           Allocator,
                      RenderDeviceD3D12Impl&      DeviceD3D12Impl,
                      uint32                      NumDescriptorsInHeap,
                      D3D12_DESCRIPTOR_HEAP_TYPE  Type,
                      D3D12_DESCRIPTOR_HEAP_FLAGS Flags);

    // clang-format off
    CPUDescriptorHeap             (const CPUDescriptorHeap&) = delete;
    CPUDescriptorHeap             (CPUDescriptorHeap&&)      = delete;
    CPUDescriptorHeap& operator = (const CPUDescriptorHeap&) = delete;
    CPUDescriptorHeap& operator = (CPUDescriptorHeap&&)      = delete;
    // clang-format on

    ~CPUDescriptorHeap();

    virtual DescriptorHeapAllocation Allocate(uint32 Count) override final;
    virtual void                     Free(DescriptorHeapAllocation&& Allocation, uint64 CmdQueueMask) override final;
    virtual uint32                   GetDescriptorSize() const override final { return m_DescriptorSize; }

#ifdef DILIGENT_DEVELOPMENT
    int32_t DvpGetTotalAllocationCount();
#endif

private:
    void FreeAllocation(DescriptorHeapAllocation&& Allocation);

    IMemoryAllocator&      m_MemAllocator;
    RenderDeviceD3D12Impl& m_DeviceD3D12Impl;

    // Pool of descriptor heap managers
    std::mutex                                                                                        m_HeapPoolMutex;
    std::vector<DescriptorHeapAllocationManager/*, STDAllocatorRawMem<DescriptorHeapAllocationManager>*/> m_HeapPool;
    // Indices of available descriptor heap managers
    std::unordered_set<size_t, std::hash<size_t>, std::equal_to<size_t>/*, STDAllocatorRawMem<size_t>*/> m_AvailableHeaps;

    D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
    const UINT                 m_DescriptorSize = 0;

    // Maximum heap size during the application lifetime - for statistic purposes
    uint32 m_MaxSize     = 0;
    uint32 m_CurrentSize = 0;
};

// GPU descriptor heap provides storage for shader-visible descriptors
// The heap contains single D3D12 descriptor heap that is split into two parts.
// The first part stores static and mutable resource descriptor handles.
// The second part is intended to provide temporary storage for dynamic resources.
// Space for dynamic resources is allocated in chunks, and then descriptors are suballocated within every
// chunk. DynamicSuballocationsManager facilitates this process.
//
//
//     static and mutable handles      ||                 dynamic space
//                                     ||    chunk 0     chunk 1     chunk 2     unused
//  | X O O X X O X O O O O X X X X O  ||  | X X X O | | X X O O | | O O O O |  O O O O  ||
//                                               |         |
//                                     suballocation       suballocation
//                                    within chunk 0       within chunk 1
//
// Render device contains two GPUDescriptorHeap instances (CBV_SRV_UAV and SAMPLER). The heaps
// are used to allocate GPU-visible descriptors for shader resource binding objects. The heaps
// are also used by the command contexts (through DynamicSuballocationsManager to allocated dynamic descriptors)
//
//  _______________________________________________________________________________________________________________________________
// | Render Device                                                                                                                 |
// |                                                                                                                               |
// | m_CPUDescriptorHeaps[CBV_SRV_UAV] |  X  X  X  X  X  X  X  X  |, |  X  X  X  X  X  X  X  X  |, |  X  O  O  X  O  O  O  O  |    |
// | m_CPUDescriptorHeaps[SAMPLER]     |  X  X  X  X  O  O  O  X  |, |  X  O  O  X  O  O  O  O  |                                  |
// | m_CPUDescriptorHeaps[RTV]         |  X  X  X  O  O  O  O  O  |, |  O  O  O  O  O  O  O  O  |                                  |
// | m_CPUDescriptorHeaps[DSV]         |  X  X  X  O  X  O  X  O  |                                                                |
// |                                                                               ctx1        ctx2                                |
// | m_GPUDescriptorHeaps[CBV_SRV_UAV]  | X O O X X O X O O O O X X X X O  ||  | X X X O | | X X O O | | O O O O |  O O O O  ||    |
// | m_GPUDescriptorHeaps[SAMPLER]      | X X O O X O X X X O O X O O O O  ||  | X X O O | | X O O O | | O O O O |  O O O O  ||    |
// |                                                                                                                               |
// |_______________________________________________________________________________________________________________________________|
//
//  ________________________________________________               ________________________________________________
// |Device Context 1                                |             |Device Context 2                                |
// |                                                |             |                                                |
// | m_DynamicGPUDescriptorAllocator[CBV_SRV_UAV]   |             | m_DynamicGPUDescriptorAllocator[CBV_SRV_UAV]   |
// | m_DynamicGPUDescriptorAllocator[SAMPLER]       |             | m_DynamicGPUDescriptorAllocator[SAMPLER]       |
// |________________________________________________|             |________________________________________________|
//
class GPUDescriptorHeap final : public IDescriptorAllocator
{
public:
    GPUDescriptorHeap(IMemoryAllocator&           Allocator,
                      RenderDeviceD3D12Impl&      Device,
                      uint32                      NumDescriptorsInHeap,
                      uint32                      NumDynamicDescriptors,
                      D3D12_DESCRIPTOR_HEAP_TYPE  Type,
                      D3D12_DESCRIPTOR_HEAP_FLAGS Flags);

    // clang-format off
    GPUDescriptorHeap             (const GPUDescriptorHeap&) = delete;
    GPUDescriptorHeap             (GPUDescriptorHeap&&)      = delete;
    GPUDescriptorHeap& operator = (const GPUDescriptorHeap&) = delete;
    GPUDescriptorHeap& operator = (GPUDescriptorHeap&&)      = delete;
    // clang-format on

    ~GPUDescriptorHeap();

    virtual DescriptorHeapAllocation Allocate(uint32 Count) override final
    {
        return m_HeapAllocationManager.Allocate(Count);
    }

    virtual void   Free(DescriptorHeapAllocation&& Allocation, uint64 CmdQueueMask) override final;
    virtual uint32 GetDescriptorSize() const override final { return m_DescriptorSize; }

    DescriptorHeapAllocation AllocateDynamic(uint32 Count)
    {
        return m_DynamicAllocationsManager.Allocate(Count);
    }

    const D3D12_DESCRIPTOR_HEAP_DESC& GetHeapDesc() const { return m_HeapDesc; }
    uint32                            GetMaxStaticDescriptors() const { return m_HeapAllocationManager.GetMaxDescriptors(); }
    uint32                            GetMaxDynamicDescriptors() const { return m_DynamicAllocationsManager.GetMaxDescriptors(); }

#ifdef DILIGENT_DEVELOPMENT
    int32_t DvpGetTotalAllocationCount() const
    {
        return m_HeapAllocationManager.DvpGetAllocationsCounter() +
            m_DynamicAllocationsManager.DvpGetAllocationsCounter();
    }
#endif

protected:
    RenderDeviceD3D12Impl& m_DeviceD3D12Impl;

    const D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
    CComPtr<ID3D12DescriptorHeap>    m_pd3d12DescriptorHeap;

    const UINT m_DescriptorSize;

    // Allocation manager for static/mutable part
    DescriptorHeapAllocationManager m_HeapAllocationManager;

    // Allocation manager for dynamic part
    DescriptorHeapAllocationManager m_DynamicAllocationsManager;
};


// The class facilitates allocation of dynamic descriptor handles. It requests a chunk of heap
// from the master GPU descriptor heap and then performs linear suballocation within the chunk
// At the end of the frame all allocations are disposed.

//     static and mutable handles     ||                 dynamic space
//                                    ||    chunk 0                 chunk 2
//  |                                 ||  | X X X O |             | O O O O |           || GPU Descriptor Heap
//                                        |                       |
//                                        m_Suballocations[0]     m_Suballocations[1]
//
class DynamicSuballocationsManager final : public IDescriptorAllocator
{
public:
    DynamicSuballocationsManager(IMemoryAllocator&  Allocator,
                                 GPUDescriptorHeap& ParentGPUHeap,
                                 uint32             DynamicChunkSize,
                                 String             ManagerName);

    // clang-format off
    DynamicSuballocationsManager             (const DynamicSuballocationsManager&) = delete;
    DynamicSuballocationsManager             (DynamicSuballocationsManager&&)      = delete;
    DynamicSuballocationsManager& operator = (const DynamicSuballocationsManager&) = delete;
    DynamicSuballocationsManager& operator = (DynamicSuballocationsManager&&)      = delete;
    // clang-format on

    ~DynamicSuballocationsManager();

    void ReleaseAllocations(uint64 CmdQueueMask);

    virtual DescriptorHeapAllocation Allocate(uint32 Count) override final;
    virtual void                     Free(DescriptorHeapAllocation&& Allocation, uint64 CmdQueueMask) override final
    {
        // Do nothing. Dynamic allocations are not disposed individually, but as whole chunks
        // at the end of the frame by ReleaseAllocations()
        Allocation.Reset();
    }

    virtual uint32 GetDescriptorSize() const override final { return m_ParentGPUHeap.GetDescriptorSize(); }

    size_t GetSuballocationCount() const { return m_Suballocations.size(); }

private:
    // Parent GPU descriptor heap that is used to allocate chunks
    GPUDescriptorHeap& m_ParentGPUHeap;
    const String       m_ManagerName;

    // List of chunks allocated from the master GPU descriptor heap. All chunks are disposed at the end
    // of the frame
    std::vector<DescriptorHeapAllocation/*, STDAllocatorRawMem<DescriptorHeapAllocation>*/> m_Suballocations;

    uint32 m_CurrentSuballocationOffset = 0;
    uint32 m_DynamicChunkSize           = 0;

    uint32 m_CurrDescriptorCount         = 0;
    uint32 m_PeakDescriptorCount         = 0;
    uint32 m_CurrSuballocationsTotalSize = 0;
    uint32 m_PeakSuballocationsTotalSize = 0;
};

} // namespace Diligent
