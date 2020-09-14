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

//----------------------------------------------------------------------------------
// Adopted from Diligent Graphics 
// https://github.com/DiligentGraphics/DiligentCore/blob/a9002525abad03a8e5aab27daf2c2a0093fb47f6/Graphics/GraphicsEngineD3D12/src/DescriptorHeap.cpp
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

#include "DescriptorManagement.h"

#include <iomanip>

// TODO: VQE adaptation
#define LOG_INFO_MESSAGE(...)do{}while(false);
#define LOG_ERROR(...)()do{}while(false);


#if defined(DILIGENT_DEBUG)
#    define DEV_CHECK_ERR VERIFY
#elif defined(DILIGENT_DEVELOPMENT)
#    define DEV_CHECK_ERR CHECK_ERR
#else
// clang-format off
#    define DEV_CHECK_ERR(...)do{}while(false)
// clang-format on
#endif

#ifdef DILIGENT_DEVELOPMENT

#    define DEV_CHECK_WARN CHECK_WARN
#    define DEV_CHECK_INFO CHECK_INFO

#else

// clang-format off
#    define DEV_CHECK_WARN(...)do{}while(false)
#    define DEV_CHECK_INFO(...)do{}while(false)
// clang-format on

#endif


namespace Diligent
{
template <class T> using STDAllocatorRawMem = STDAllocator<T, IMemoryAllocator>;
#define STD_ALLOCATOR_RAW_MEM(Type, Allocator, Description) STDAllocatorRawMem<Type>(Allocator, Description, __FILE__, __LINE__)


// The following basic requirement guarantees correctness of resource deallocation:
//
//        A resource is never released before the last draw command referencing it is submitted to the command queue
//

//
// CPU
//                       Last Reference
//                        of resource X
//                             |
//                             |     Submit Cmd       Submit Cmd            Submit Cmd
//                             |      List N           List N+1              List N+2
//                             V         |                |                     |
//    NextFenceValue       |   *  N      |      N+1       |          N+2        |
//
//
//    CompletedFenceValue       |     N-3      |      N-2      |        N-1        |        N       |
//                              .              .               .                   .                .
// -----------------------------.--------------.---------------.-------------------.----------------.-------------
//                              .              .               .                   .                .
//
// GPU                          | Cmd List N-2 | Cmd List N-1  |    Cmd List N     |   Cmd List N+1 |
//                                                                                 |
//                                                                                 |
//                                                                          Resource X can
//                                                                           be released
template <typename ObjectType, typename = typename std::enable_if<std::is_object<ObjectType>::value>::type>
void SafeReleaseDeviceObject(ObjectType&& Object, Uint64 QueueMask)
{
    assert(false);
    /*
    VERIFY(m_CommandQueues != nullptr, "Command queues have been destroyed. Are you releasing an object from the render device destructor?");

    QueueMask &= GetCommandQueueMask();

    VERIFY(QueueMask != 0, "At least one bit should be set in the command queue mask");
    if (QueueMask == 0)
        return;

    Atomics::Long NumReferences = PlatformMisc::CountOneBits(QueueMask);
    auto          Wrapper = DynamicStaleResourceWrapper::Create(std::move(Object), NumReferences);

    while (QueueMask != 0)
    {
        auto QueueIndex = PlatformMisc::GetLSB(QueueMask);
        VERIFY_EXPR(QueueIndex < m_CmdQueueCount);

        auto& Queue = m_CommandQueues[QueueIndex];
        // Do not use std::move on wrapper!!!
        Queue.ReleaseQueue.SafeReleaseResource(Wrapper, Queue.NextCmdBufferNumber);
        QueueMask &= ~(Uint64{ 1 } << Uint64{ QueueIndex });
        --NumReferences;
    }
    VERIFY_EXPR(NumReferences == 0);

    Wrapper.GiveUpOwnership();
    */
}


// Creates a new descriptor heap and reference the entire heap
DescriptorHeapAllocationManager::DescriptorHeapAllocationManager(IMemoryAllocator&                 Allocator,
                                                                 RenderDeviceD3D12Impl&            DeviceD3D12Impl,
                                                                 IDescriptorAllocator&             ParentAllocator,
                                                                 size_t                            ThisManagerId,
                                                                 const D3D12_DESCRIPTOR_HEAP_DESC& HeapDesc) :
    // clang-format off
    m_ParentAllocator            {ParentAllocator},
    m_DeviceD3D12Impl            {DeviceD3D12Impl},
    m_ThisManagerId              {ThisManagerId  },
    m_HeapDesc                   {HeapDesc       },
    m_DescriptorSize             {DeviceD3D12Impl.GetD3D12Device()->GetDescriptorHandleIncrementSize(m_HeapDesc.Type)},
    m_NumDescriptorsInAllocation {HeapDesc.NumDescriptors},
    m_FreeBlockManager           {HeapDesc.NumDescriptors, Allocator}
// clang-format on
{
    auto pDevice = DeviceD3D12Impl.GetD3D12Device();

    m_FirstCPUHandle.ptr = 0;
    m_FirstGPUHandle.ptr = 0;

    pDevice->CreateDescriptorHeap(&m_HeapDesc, __uuidof(m_pd3d12DescriptorHeap), reinterpret_cast<void**>(static_cast<ID3D12DescriptorHeap**>(&m_pd3d12DescriptorHeap)));
    m_FirstCPUHandle = m_pd3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    if (m_HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        m_FirstGPUHandle = m_pd3d12DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
}

// Uses subrange of descriptors in the existing D3D12 descriptor heap
// that starts at offset FirstDescriptor and uses NumDescriptors descriptors
DescriptorHeapAllocationManager::DescriptorHeapAllocationManager(IMemoryAllocator&      Allocator,
                                                                 RenderDeviceD3D12Impl& DeviceD3D12Impl,
                                                                 IDescriptorAllocator&  ParentAllocator,
                                                                 size_t                 ThisManagerId,
                                                                 ID3D12DescriptorHeap*  pd3d12DescriptorHeap,
                                                                 Uint32                 FirstDescriptor,
                                                                 Uint32                 NumDescriptors) :
    // clang-format off
    m_ParentAllocator            {ParentAllocator},
    m_DeviceD3D12Impl            {DeviceD3D12Impl},
    m_ThisManagerId              {ThisManagerId  },
    m_HeapDesc                   {pd3d12DescriptorHeap->GetDesc()},
    m_DescriptorSize             {DeviceD3D12Impl.GetD3D12Device()->GetDescriptorHandleIncrementSize(m_HeapDesc.Type)},
    m_NumDescriptorsInAllocation {NumDescriptors},
    m_FreeBlockManager           {NumDescriptors, Allocator},
    m_pd3d12DescriptorHeap       {pd3d12DescriptorHeap}
// clang-format on
{
    m_FirstCPUHandle = pd3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    m_FirstCPUHandle.ptr += m_DescriptorSize * FirstDescriptor;

    if (m_HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        m_FirstGPUHandle = pd3d12DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        m_FirstGPUHandle.ptr += m_DescriptorSize * FirstDescriptor;
    }
}


DescriptorHeapAllocationManager::~DescriptorHeapAllocationManager()
{
    DEV_CHECK_ERR(m_AllocationsCounter == 0, m_AllocationsCounter, " allocations have not been released. If these allocations are referenced by release queue, the app will crash when DescriptorHeapAllocationManager::FreeAllocation() is called.");
    DEV_CHECK_ERR(m_FreeBlockManager.GetFreeSize() == m_NumDescriptorsInAllocation, "Not all descriptors were released");
}

DescriptorHeapAllocation DescriptorHeapAllocationManager::Allocate(uint32_t Count)
{
    VERIFY_EXPR(Count > 0);

    std::lock_guard<std::mutex> LockGuard(m_FreeBlockManagerMutex);
    // Methods of VariableSizeAllocationsManager class are not thread safe!

    // Use variable-size GPU allocations manager to allocate the requested number of descriptors
    auto Allocation = m_FreeBlockManager.Allocate(Count, 1);
    if (!Allocation.IsValid())
        return DescriptorHeapAllocation{};

    VERIFY_EXPR(Allocation.Size == Count);

    // Compute the first CPU and GPU descriptor handles in the allocation by
    // offseting the first CPU and GPU descriptor handle in the range
    auto CPUHandle = m_FirstCPUHandle;
    CPUHandle.ptr += Allocation.UnalignedOffset * m_DescriptorSize;

    auto GPUHandle = m_FirstGPUHandle; // Will be null if the heap is not GPU-visible
    if (m_HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        GPUHandle.ptr += Allocation.UnalignedOffset * m_DescriptorSize;

    m_MaxAllocatedSize = std::max(m_MaxAllocatedSize, m_FreeBlockManager.GetUsedSize());

#ifdef DILIGENT_DEVELOPMENT
    ++m_AllocationsCounter;
#endif

    VERIFY(m_ThisManagerId < std::numeric_limits<Uint16>::max(), "ManagerID exceeds 16-bit range");
    return DescriptorHeapAllocation{m_ParentAllocator, m_pd3d12DescriptorHeap, CPUHandle, GPUHandle, Count, static_cast<Uint16>(m_ThisManagerId)};
}

void DescriptorHeapAllocationManager::FreeAllocation(DescriptorHeapAllocation&& Allocation)
{
    VERIFY(Allocation.GetAllocationManagerId() == m_ThisManagerId, "Invalid descriptor heap manager Id");

    if (Allocation.IsNull())
        return;

    std::lock_guard<std::mutex> LockGuard(m_FreeBlockManagerMutex);
    auto                        DescriptorOffset = (Allocation.GetCpuHandle().ptr - m_FirstCPUHandle.ptr) / m_DescriptorSize;
    // Methods of VariableSizeAllocationsManager class are not thread safe!
    m_FreeBlockManager.Free(DescriptorOffset, Allocation.GetNumHandles());

    // Clear the allocation
    Allocation.Reset();
#ifdef DILIGENT_DEVELOPMENT
    --m_AllocationsCounter;
#endif
}



//
// CPUDescriptorHeap implementation
//
CPUDescriptorHeap::CPUDescriptorHeap(IMemoryAllocator&           Allocator,
                                     RenderDeviceD3D12Impl&      DeviceD3D12Impl,
                                     Uint32                      NumDescriptorsInHeap,
                                     D3D12_DESCRIPTOR_HEAP_TYPE  Type,
                                     D3D12_DESCRIPTOR_HEAP_FLAGS Flags) :
    // clang-format off
    m_MemAllocator   {Allocator      },
    m_DeviceD3D12Impl{DeviceD3D12Impl},
    m_HeapPool       (/*STD_ALLOCATOR_RAW_MEM(DescriptorHeapAllocationManager, GetRawAllocator(), "Allocator for vector<DescriptorHeapAllocationManager>")*/),
    m_AvailableHeaps (/*STD_ALLOCATOR_RAW_MEM(size_t, GetRawAllocator(), "Allocator for unordered_set<size_t>")*/),
    m_HeapDesc
    {
        Type,
        NumDescriptorsInHeap,
        Flags,
        1   // NodeMask
    },
    m_DescriptorSize{DeviceD3D12Impl.GetD3D12Device()->GetDescriptorHandleIncrementSize(Type)}
// clang-format on
{
    // Create one pool
    m_HeapPool.emplace_back(m_MemAllocator, m_DeviceD3D12Impl, *this, 0, m_HeapDesc);
    m_AvailableHeaps.insert(0);
}

CPUDescriptorHeap::~CPUDescriptorHeap()
{
    DEV_CHECK_ERR(m_CurrentSize == 0, "Not all allocations released");

    DEV_CHECK_ERR(m_AvailableHeaps.size() == m_HeapPool.size(), "Not all descriptor heap pools are released");
    Uint32 TotalDescriptors = 0;
    for (auto& Heap : m_HeapPool)
    {
        DEV_CHECK_ERR(Heap.GetNumAvailableDescriptors() == Heap.GetMaxDescriptors(), "Not all descriptors in the descriptor pool are released");
        TotalDescriptors += Heap.GetMaxDescriptors();
    }

    LOG_INFO_MESSAGE(std::setw(38), std::left, GetD3D12DescriptorHeapTypeLiteralName(m_HeapDesc.Type), " CPU heap allocated pool count: ", m_HeapPool.size(),
                     ". Max descriptors: ", m_MaxSize, '/', TotalDescriptors,
                     " (", std::fixed, std::setprecision(2), m_MaxSize * 100.0 / std::max(TotalDescriptors, 1u), "%).");
}

#ifdef DILIGENT_DEVELOPMENT
int32_t CPUDescriptorHeap::DvpGetTotalAllocationCount()
{
    int32_t AllocationCount = 0;

    std::lock_guard<std::mutex> LockGuard(m_HeapPoolMutex);
    for (auto& Heap : m_HeapPool)
        AllocationCount += Heap.DvpGetAllocationsCounter();
    return AllocationCount;
}
#endif

DescriptorHeapAllocation CPUDescriptorHeap::Allocate(uint32_t Count)
{
    std::lock_guard<std::mutex> LockGuard(m_HeapPoolMutex);
    // Note that every DescriptorHeapAllocationManager object instance is itslef
    // thread-safe. Nested mutexes cannot cause a deadlock

    DescriptorHeapAllocation Allocation;
    // Go through all descriptor heap managers that have free descriptors
    auto AvailableHeapIt = m_AvailableHeaps.begin();
    while (AvailableHeapIt != m_AvailableHeaps.end())
    {
        auto NextIt = AvailableHeapIt;
        ++NextIt;
        // Try to allocate descriptor using the current descriptor heap manager
        Allocation = m_HeapPool[*AvailableHeapIt].Allocate(Count);
        // Remove the manager from the pool if it has no more available descriptors
        if (m_HeapPool[*AvailableHeapIt].GetNumAvailableDescriptors() == 0)
            m_AvailableHeaps.erase(*AvailableHeapIt);

        // Terminate the loop if descriptor was successfully allocated, otherwise
        // go to the next manager
        if (!Allocation.IsNull())
            break;
        AvailableHeapIt = NextIt;
    }

    // If there were no available descriptor heap managers or no manager was able
    // to suffice the allocation request, create a new manager
    if (Allocation.IsNull())
    {
        // Make sure the heap is large enough to accomodate the requested number of descriptors
        if (Count > m_HeapDesc.NumDescriptors)
        {
            LOG_INFO_MESSAGE("Number of requested CPU descriptors handles (", Count, ") exceeds the descriptor heap size (", m_HeapDesc.NumDescriptors, "). Increasing the number of descriptors in the heap");
        }
        m_HeapDesc.NumDescriptors = std::max(m_HeapDesc.NumDescriptors, static_cast<UINT>(Count));
        // Create a new descriptor heap manager. Note that this constructor creates a new D3D12 descriptor
        // heap and references the entire heap. Pool index is used as manager ID
        m_HeapPool.emplace_back(m_MemAllocator, m_DeviceD3D12Impl, *this, m_HeapPool.size(), m_HeapDesc);
        auto NewHeapIt = m_AvailableHeaps.insert(m_HeapPool.size() - 1);
        VERIFY_EXPR(NewHeapIt.second);

        // Use the new manager to allocate descriptor handles
        Allocation = m_HeapPool[*NewHeapIt.first].Allocate(Count);
    }

    m_CurrentSize += static_cast<Uint32>(Allocation.GetNumHandles());
    m_MaxSize = std::max(m_MaxSize, m_CurrentSize);

    return Allocation;
}

// Method is called from ~DescriptorHeapAllocation()
void CPUDescriptorHeap::Free(DescriptorHeapAllocation&& Allocation, Uint64 CmdQueueMask)
{
    struct StaleAllocation
    {
        DescriptorHeapAllocation Allocation;
        CPUDescriptorHeap*       Heap;

        // clang-format off
        StaleAllocation(DescriptorHeapAllocation&& _Allocation, CPUDescriptorHeap& _Heap)noexcept :
            Allocation{std::move(_Allocation)},
            Heap      {&_Heap                }
        {
        }

        StaleAllocation            (const StaleAllocation&)  = delete;
        StaleAllocation& operator= (const StaleAllocation&)  = delete;
        StaleAllocation& operator= (      StaleAllocation&&) = delete;
            
        StaleAllocation(StaleAllocation&& rhs)noexcept : 
            Allocation {std::move(rhs.Allocation)},
            Heap       {rhs.Heap                 }
        {
            rhs.Heap  = nullptr;
        }
        // clang-format on

        ~StaleAllocation()
        {
            if (Heap != nullptr)
                Heap->FreeAllocation(std::move(Allocation));
        }
    };
    SafeReleaseDeviceObject(StaleAllocation{std::move(Allocation), *this}, CmdQueueMask);
}

void CPUDescriptorHeap::FreeAllocation(DescriptorHeapAllocation&& Allocation)
{
    std::lock_guard<std::mutex> LockGuard(m_HeapPoolMutex);
    auto                        ManagerId = Allocation.GetAllocationManagerId();
    m_CurrentSize -= static_cast<Uint32>(Allocation.GetNumHandles());
    m_HeapPool[ManagerId].FreeAllocation(std::move(Allocation));
    // Return the manager to the pool of available managers
    VERIFY_EXPR(m_HeapPool[ManagerId].GetNumAvailableDescriptors() > 0);
    m_AvailableHeaps.insert(ManagerId);
}



GPUDescriptorHeap::GPUDescriptorHeap(IMemoryAllocator&           Allocator,
                                     RenderDeviceD3D12Impl&      Device,
                                     Uint32                      NumDescriptorsInHeap,
                                     Uint32                      NumDynamicDescriptors,
                                     D3D12_DESCRIPTOR_HEAP_TYPE  Type,
                                     D3D12_DESCRIPTOR_HEAP_FLAGS Flags) :
    // clang-format off
    m_DeviceD3D12Impl{Device},
    m_HeapDesc
    {
        Type,
        NumDescriptorsInHeap + NumDynamicDescriptors,
        Flags,
        1 // UINT NodeMask;
    },
    m_pd3d12DescriptorHeap
    {
        [&]
        {
              CComPtr<ID3D12DescriptorHeap> pHeap;
              Device.GetD3D12Device()->CreateDescriptorHeap(&m_HeapDesc, __uuidof(pHeap), reinterpret_cast<void**>(&pHeap));
              return pHeap;
        }()
    },
    m_DescriptorSize           {Device.GetD3D12Device()->GetDescriptorHandleIncrementSize(Type)},
    m_HeapAllocationManager    {Allocator, Device, *this, 0, m_pd3d12DescriptorHeap, 0, NumDescriptorsInHeap},
    m_DynamicAllocationsManager{Allocator, Device, *this, 1, m_pd3d12DescriptorHeap, NumDescriptorsInHeap, NumDynamicDescriptors}
// clang-format on
{
}

GPUDescriptorHeap::~GPUDescriptorHeap()
{
    auto TotalStaticSize  = m_HeapAllocationManager.GetMaxDescriptors();
    auto TotalDynamicSize = m_DynamicAllocationsManager.GetMaxDescriptors();
    auto MaxStaticSize    = m_HeapAllocationManager.GetMaxAllocatedSize();
    auto MaxDynamicSize   = m_DynamicAllocationsManager.GetMaxAllocatedSize();

    LOG_INFO_MESSAGE(std::setw(38), std::left, GetD3D12DescriptorHeapTypeLiteralName(m_HeapDesc.Type), " GPU heap max allocated size (static|dynamic): ",
                     MaxStaticSize, '/', TotalStaticSize, " (", std::fixed, std::setprecision(2), MaxStaticSize * 100.0 / TotalStaticSize, "%) | ",
                     MaxDynamicSize, '/', TotalDynamicSize, " (", std::fixed, std::setprecision(2), MaxDynamicSize * 100.0 / TotalDynamicSize, "%).");
}



void GPUDescriptorHeap::Free(DescriptorHeapAllocation&& Allocation, Uint64 CmdQueueMask)
{
    struct StaleAllocation
    {
        DescriptorHeapAllocation Allocation;
        GPUDescriptorHeap*       Heap;

        // clang-format off
        StaleAllocation(DescriptorHeapAllocation&& _Allocation, GPUDescriptorHeap& _Heap)noexcept :
            Allocation{std::move(_Allocation)},
            Heap      {&_Heap                }
        {
        }

        StaleAllocation            (const StaleAllocation&)  = delete;
        StaleAllocation& operator= (const StaleAllocation&)  = delete;
        StaleAllocation& operator= (      StaleAllocation&&) = delete;
            
        StaleAllocation(StaleAllocation&& rhs)noexcept : 
            Allocation{std::move(rhs.Allocation)},
            Heap      {rhs.Heap                 }
        {
            rhs.Heap  = nullptr;
        }
        // clang-format on

        ~StaleAllocation()
        {
            if (Heap != nullptr)
            {
                auto MgrId = Allocation.GetAllocationManagerId();
                VERIFY(MgrId == 0 || MgrId == 1, "Unexpected allocation manager ID");

                if (MgrId == 0)
                {
                    Heap->m_HeapAllocationManager.FreeAllocation(std::move(Allocation));
                }
                else
                {
                    Heap->m_DynamicAllocationsManager.FreeAllocation(std::move(Allocation));
                }
            }
        }
    };
    SafeReleaseDeviceObject(StaleAllocation{std::move(Allocation), *this}, CmdQueueMask);
}


DynamicSuballocationsManager::DynamicSuballocationsManager(IMemoryAllocator&  Allocator,
                                                           GPUDescriptorHeap& ParentGPUHeap,
                                                           Uint32             DynamicChunkSize,
                                                           String             ManagerName) :
    // clang-format off
    m_ParentGPUHeap   {ParentGPUHeap   },
    m_DynamicChunkSize{DynamicChunkSize},
    m_Suballocations  (/*STD_ALLOCATOR_RAW_MEM(DescriptorHeapAllocation, GetRawAllocator(), "Allocator for vector<DescriptorHeapAllocation>")*/),
    m_ManagerName     {std::move(ManagerName)}
// clang-format on
{
}

DynamicSuballocationsManager::~DynamicSuballocationsManager()
{
    DEV_CHECK_ERR(m_Suballocations.empty() && m_CurrDescriptorCount == 0 && m_CurrSuballocationsTotalSize == 0, "All dynamic suballocations must be released!");
    LOG_INFO_MESSAGE(m_ManagerName, " usage stats: peak descriptor count: ", m_PeakDescriptorCount, '/', m_PeakSuballocationsTotalSize);
}

void DynamicSuballocationsManager::ReleaseAllocations(Uint64 CmdQueueMask)
{
    // Clear the list and dispose all allocated chunks of GPU descriptor heap.
    // The chunks will be added to release queues and eventually returned to the
    // parent GPU heap.
    for (auto& Allocation : m_Suballocations)
    {
        m_ParentGPUHeap.Free(std::move(Allocation), CmdQueueMask);
    }
    m_Suballocations.clear();
    m_CurrDescriptorCount         = 0;
    m_CurrSuballocationsTotalSize = 0;
}

DescriptorHeapAllocation DynamicSuballocationsManager::Allocate(Uint32 Count)
{
    // This method is intentionally lock-free as it is expected to
    // be called through device context from single thread only

    // Check if there are no chunks or the last chunk does not have enough space
    if (m_Suballocations.empty() ||
        m_CurrentSuballocationOffset + Count > m_Suballocations.back().GetNumHandles())
    {
        // Request a new chunk from the parent GPU descriptor heap
        auto SuballocationSize       = std::max(m_DynamicChunkSize, Count);
        auto NewDynamicSubAllocation = m_ParentGPUHeap.AllocateDynamic(SuballocationSize);
        if (NewDynamicSubAllocation.IsNull())
        {
            assert(false);
            //LOG_ERROR("Dynamic space in ", GetD3D12DescriptorHeapTypeLiteralName(m_ParentGPUHeap.GetHeapDesc().Type), " GPU descriptor heap is exhausted.");
            return DescriptorHeapAllocation();
        }
        m_Suballocations.emplace_back(std::move(NewDynamicSubAllocation));
        m_CurrentSuballocationOffset = 0;

        m_CurrSuballocationsTotalSize += SuballocationSize;
        m_PeakSuballocationsTotalSize = std::max(m_PeakSuballocationsTotalSize, m_CurrSuballocationsTotalSize);
    }

    // Perform suballocation from the last chunk
    auto& CurrentSuballocation = m_Suballocations.back();

    auto ManagerId = CurrentSuballocation.GetAllocationManagerId();
    VERIFY(ManagerId < std::numeric_limits<Uint16>::max(), "ManagerID exceed allowed limit");
    DescriptorHeapAllocation Allocation(*this,
                                        CurrentSuballocation.GetDescriptorHeap(),
                                        CurrentSuballocation.GetCpuHandle(m_CurrentSuballocationOffset),
                                        CurrentSuballocation.GetGpuHandle(m_CurrentSuballocationOffset),
                                        Count,
                                        static_cast<Uint16>(ManagerId));
    m_CurrentSuballocationOffset += Count;
    m_CurrDescriptorCount += Count;
    m_PeakDescriptorCount = std::max(m_PeakDescriptorCount, m_CurrDescriptorCount);

    return Allocation;
}

} // namespace Diligent
