//==============================================================================
// Copyright (c) 2017-2021 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief  This file contains three macro definitions to wrap existing PIX3
///         functions with AMD RGP marker support.
///
///         This requires AMD driver with a device level marker extension
///         support (driver 17.30.1081 or newer).
///
///         This requires a WinPixEventRuntime version of at least
///         1.0.200127001.
///
///         To use: Update the PIXEvents.h file to #include this header file.
///                 Then prefix the existing calls to PIXBeginEventOnContextCpu,
///                 PIXEndEventOnContextCpu and PIXSetMarkerOnContextCpu within
///                 that file to add an "Rgp" prefix (so the calls become
///                 RgpPIXBeginEventOnContextCpu, RgpPIXEndEventOnContextCpu and
///                 RgpPIXSetMarkerOnContextCpu). Also check the RGP user
///                 documentation for a complete user guide.
///
/// \warning Only supports the unicode character set (does not support multi-byte)
//==============================================================================
#ifndef _AMD_PIX3_H_
#define _AMD_PIX3_H_

#include <stdio.h>
#include <stdarg.h>
#include "AmdExtD3D.h"
#include "AmdExtD3DDeviceApi.h"


#define MAX_MARKER_STRING_LENGTH 1024

// per thread amd ext device object using TLS
static __declspec(thread) IAmdExtD3DDevice1* tls_pAmdExtDeviceObject = nullptr;
static __declspec(thread) bool tls_checkAmdDriver                    = true;

// this function will initialize the tls_pAmdExtDeviceObject per thread
// tls_pAmdExtDeviceObject contains the marker API
inline void InitializeAmdExtDeviceObject(ID3D12GraphicsCommandList* pCommandList)
{
    // return immediately if the device extension object has been created for the thread
    if (nullptr != tls_pAmdExtDeviceObject)
    {
        return;
    }

    // return immediately if on non-AMD system
    if (!tls_checkAmdDriver)
    {
        return;
    }

    HMODULE hpAmdD3dDl2 = nullptr;
    BOOL    loaded      = FALSE;

    if (tls_checkAmdDriver)
    {
        loaded = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, "amdxc64.dll", &hpAmdD3dDl2);
    }

    if (FALSE != loaded && nullptr != hpAmdD3dDl2)
    {
        PFNAmdExtD3DCreateInterface pAmdExtD3dCreateFunc = (PFNAmdExtD3DCreateInterface)GetProcAddress(hpAmdD3dDl2, "AmdExtD3DCreateInterface");

        if (nullptr != pAmdExtD3dCreateFunc)
        {
            ID3D12Device* pDevice = nullptr;

            if (nullptr != pCommandList)
            {
                pCommandList->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&pDevice));
            }

            if (nullptr != pDevice)
            {
                // create the extension object factory
                IAmdExtD3DFactory* pAmdExtObject = nullptr;
                pAmdExtD3dCreateFunc(pDevice, __uuidof(IAmdExtD3DFactory), reinterpret_cast<void**>(&pAmdExtObject));

                if (nullptr != pAmdExtObject)
                {
                    // use the extension factory object to create a device extension object that contains the marker API
                    pAmdExtObject->CreateInterface(pDevice, __uuidof(IAmdExtD3DDevice1), reinterpret_cast<void**>(&tls_pAmdExtDeviceObject));
                }
            }
        }
    }
    else
    {
        // running on non-amd hardware or missing amd driver install
        tls_checkAmdDriver = false;
    }
}

inline void RgpSetMarker(ID3D12GraphicsCommandList* pCommandList, PCWSTR formatString, ...)
{
    InitializeAmdExtDeviceObject(pCommandList);

    if (nullptr != tls_pAmdExtDeviceObject)
    {
        // convert from wchar_t to char string
        char   formatStringInChar[MAX_MARKER_STRING_LENGTH];
        size_t retValue = 0;
        wcstombs_s(&retValue, formatStringInChar, MAX_MARKER_STRING_LENGTH, formatString, MAX_MARKER_STRING_LENGTH);

        // create a new marker string that includes all the variadic args
        char    markerString[MAX_MARKER_STRING_LENGTH];
        va_list args;
        va_start(args, formatString);
        vsprintf_s(markerString, formatStringInChar, args);
        va_end(args);

        // set the rgp marker
        tls_pAmdExtDeviceObject->SetMarker(pCommandList, markerString);
    }
}

#if USE_WSTR
inline void RgpPushMarker(ID3D12GraphicsCommandList* pCommandList, PCWSTR formatString, ...)
#else
inline void RgpPushMarker(ID3D12GraphicsCommandList* pCommandList, PCSTR formatString, ...)
#endif
{
    InitializeAmdExtDeviceObject(pCommandList);

    if (nullptr != tls_pAmdExtDeviceObject)
    {
#if USE_WSTR
        // convert from wchar_t to char string
        char   formatStringInChar[MAX_MARKER_STRING_LENGTH];
        size_t retValue = 0;
        wcstombs_s(&retValue, formatStringInChar, MAX_MARKER_STRING_LENGTH, formatString, MAX_MARKER_STRING_LENGTH);
#else
        PCSTR& formatStringInChar = formatString;
#endif
        // create a new marker string that includes all the variadic args
        char    markerString[MAX_MARKER_STRING_LENGTH];
        va_list args;
        va_start(args, formatString);
        vsprintf_s(markerString, formatStringInChar, args);
        va_end(args);

        // push the rgp marker
        tls_pAmdExtDeviceObject->PushMarker(pCommandList, markerString);
    }
}

inline void RgpPopMarker(ID3D12GraphicsCommandList* pCommandList)
{
    InitializeAmdExtDeviceObject(pCommandList);

    if (nullptr != tls_pAmdExtDeviceObject)
    {
        tls_pAmdExtDeviceObject->PopMarker(pCommandList);
    }
}

inline void RgpSetMarker(ID3D12CommandQueue*, PCWSTR, ...)
{
    // there is no queue-based marker yet
}

#if USE_WSTR
inline void RgpPushMarker(ID3D12CommandQueue*, PCWSTR, ...)
#else
inline void RgpPushMarker(ID3D12CommandQueue*, PCSTR, ...)
#endif
{
    // there is no queue-based marker yet
}

inline void RgpPopMarker(ID3D12CommandQueue*)
{
    // there is no queue-based marker yet
}

// define three macros to wrap existing PIX3 functions
#define RgpPIXBeginEventOnContextCpu(context, color, formatString, ...) \
    RgpPushMarker(context, formatString, args...);                      \
    PIXBeginEventOnContextCpu(context, color, formatString, args...);

#define RgpPIXEndEventOnContextCpu(context) \
    RgpPopMarker(context);                  \
    PIXEndEventOnContextCpu(context);

#define RgpPIXSetMarkerOnContextCpu(context, color, formatString, ...) \
    RgpSetMarker(context, formatString, args...);               \
    PIXSetMarkerOnContextCpu(context, color, formatString, args...);


#endif  //_AMD_PIX3_H_
