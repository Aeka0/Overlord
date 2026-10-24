// Optional offline asset helper. Never linked into the game client.
#define WIN32_LEAN_AND_MEAN
#define CASCLIB_NO_AUTO_LINK_LIBRARY
#include <windows.h>
#include <cstdint>
#include "CascLib.h"

extern "C" __declspec(dllexport) void* open_casc(const wchar_t* root)
{
    HANDLE storage{};
    CASC_OPEN_STORAGE_ARGS args{};
    args.Size = sizeof(args);
    args.dwLocaleMask = CASC_LOCALE_ALL;
    return CascOpenStorageEx(root, &args, false, &storage) ? storage : nullptr;
}
extern "C" __declspec(dllexport) void close_casc(void* storage)
{
    if (storage) CascCloseStorage(storage);
}
extern "C" __declspec(dllexport) unsigned read_casc_slice(
    void* storage, const char* name, std::uint64_t offset, unsigned bytes, void* destination)
{
    if (!storage || !name || !destination || !bytes || bytes > 64u*1024u*1024u || offset > INT64_MAX)
        return ERROR_INVALID_PARAMETER;
    HANDLE file{};
    if (!CascOpenFile(storage, name, CASC_LOCALE_ALL, CASC_OPEN_BY_NAME, &file)) return GetLastError();
    unsigned result{};
    ULONGLONG size{};
    if (!CascGetFileSize64(file, &size) || offset > size || bytes > size-offset)
        result = ERROR_HANDLE_EOF;
    else if (!CascSetFilePointer64(file, static_cast<LONGLONG>(offset), nullptr, FILE_BEGIN))
        result = GetLastError();
    else
    {
        DWORD read{};
        if (!CascReadFile(file, destination, bytes, &read)) result = GetLastError();
        else if (read != bytes) result = ERROR_HANDLE_EOF;
    }
    CascCloseFile(file);
    return result;
}
