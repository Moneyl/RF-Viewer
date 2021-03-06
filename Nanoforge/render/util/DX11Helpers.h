#pragma once
#include <ext/WindowsWrapper.h>
#include "Log.h"

#define ReleaseCOM(x) if(x) x->Release();

#define DxCheck(call, failureMessage) \
{ \
    HRESULT result = call; \
    if (result != S_OK) \
        THROW_EXCEPTION("DxCheck failed. Result: {}, Message: {}", result, failureMessage); \
} \


struct ID3D10Blob;
HRESULT CompileShaderFromFile(const WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3D10Blob** ppBlobOut, const char* defines = nullptr);