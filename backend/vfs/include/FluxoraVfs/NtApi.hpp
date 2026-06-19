#pragma once

// Native (Nt*) file-system surface the virtual file system hooks intercept.
//
// The hooks are installed at the ntdll layer because that is the single choke
// point every higher-level Win32 file API (CreateFileW, FindFirstFileEx,
// GetFileAttributes, the CRT, .NET, the game engine, the script extender, ...)
// funnels through. This mirrors how Mod Organizer 2 / usvfs hook the system: by
// owning the native layer you own everything above it.
//
// winternl.h already declares NTSTATUS, UNICODE_STRING, OBJECT_ATTRIBUTES and
// IO_STATUS_BLOCK. The directory-information structures are not all exposed there
// across SDK versions, so the ones the enumeration merge needs are declared here
// with unique names to avoid clashing with any SDK definitions.

#include <windows.h>
#include <winternl.h>

namespace fluxora::vfs::nt
{
    // --- status codes -------------------------------------------------------
    inline constexpr NTSTATUS StatusSuccess = 0x00000000;
    inline constexpr NTSTATUS StatusBufferOverflow = 0x80000005;
    inline constexpr NTSTATUS StatusNoMoreFiles = 0x80000006;
    inline constexpr NTSTATUS StatusNoSuchFile = 0xC000000F;
    inline constexpr NTSTATUS StatusObjectNameNotFound = 0xC0000034;
    inline constexpr NTSTATUS StatusObjectPathNotFound = 0xC000003A;
    inline constexpr NTSTATUS StatusBufferTooSmall = 0xC0000023;

    // --- NtQueryDirectoryFileEx query flags ---------------------------------
    inline constexpr ULONG QueryFlagRestartScan = 0x00000001;
    inline constexpr ULONG QueryFlagReturnSingleEntry = 0x00000002;

    // --- file information classes used by the enumeration merge -------------
    inline constexpr int FileDirectoryInformation = 1;
    inline constexpr int FileFullDirectoryInformation = 2;
    inline constexpr int FileBothDirectoryInformation = 3;
    inline constexpr int FileNamesInformation = 12;
    inline constexpr int FileRenameInformation = 10;
    inline constexpr int FileDispositionInformation = 13;
    inline constexpr int FileIdBothDirectoryInformation = 37;
    inline constexpr int FileIdFullDirectoryInformation = 38;
    inline constexpr int FileIdExtdDirectoryInformation = 60;
    inline constexpr int FileDispositionInformationEx = 64;
    inline constexpr int FileRenameInformationEx = 65;
    inline constexpr int FileIdExtdBothDirectoryInformation = 63;
    inline constexpr int FileId64ExtdDirectoryInformation = 78;
    inline constexpr int FileId64ExtdBothDirectoryInformation = 79;
    inline constexpr int FileIdAllExtdDirectoryInformation = 80;
    inline constexpr int FileIdAllExtdBothDirectoryInformation = 81;

    // --- directory-information layouts --------------------------------------
    // Every directory-information record begins with NextEntryOffset + FileIndex,
    // ends with FileNameLength + FileName[], and differs only in the middle.

    struct FileDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        WCHAR FileName[1];
    };

    struct FileFullDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        WCHAR FileName[1];
    };

    struct FileBothDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        CCHAR ShortNameLength;
        WCHAR ShortName[12];
        WCHAR FileName[1];
    };

    struct FileIdBothDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        CCHAR ShortNameLength;
        WCHAR ShortName[12];
        LARGE_INTEGER FileId;
        WCHAR FileName[1];
    };

    struct FileIdFullDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        LARGE_INTEGER FileId;
        WCHAR FileName[1];
    };

    struct FileIdExtdDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        ULONG ReparsePointTag;
        BYTE FileId[16];
        WCHAR FileName[1];
    };

    struct FileIdExtdBothDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        ULONG ReparsePointTag;
        BYTE FileId[16];
        CCHAR ShortNameLength;
        WCHAR ShortName[12];
        WCHAR FileName[1];
    };

    struct FileId64ExtdDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        ULONG ReparsePointTag;
        LARGE_INTEGER FileId;
        WCHAR FileName[1];
    };

    struct FileId64ExtdBothDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        ULONG ReparsePointTag;
        LARGE_INTEGER FileId;
        CCHAR ShortNameLength;
        CCHAR Reserved1;
        WCHAR ShortName[12];
        WCHAR FileName[1];
    };

    struct FileIdAllExtdDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        ULONG ReparsePointTag;
        LARGE_INTEGER FileId;
        BYTE FileId128[16];
        WCHAR FileName[1];
    };

    struct FileIdAllExtdBothDirectoryInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;
        ULONG FileNameLength;
        ULONG EaSize;
        ULONG ReparsePointTag;
        LARGE_INTEGER FileId;
        BYTE FileId128[16];
        CCHAR ShortNameLength;
        CCHAR Reserved1;
        WCHAR ShortName[12];
        WCHAR FileName[1];
    };

    struct FileNamesInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG FileIndex;
        ULONG FileNameLength;
        WCHAR FileName[1];
    };

    struct FileRenameInformationData
    {
        BOOLEAN ReplaceIfExists;
        HANDLE RootDirectory;
        ULONG FileNameLength;
        WCHAR FileName[1];
    };

    struct FileRenameInformationExData
    {
        ULONG Flags;
        HANDLE RootDirectory;
        ULONG FileNameLength;
        WCHAR FileName[1];
    };

    struct FileDispositionInformationData
    {
        BOOLEAN DeleteFile;
    };

    struct FileDispositionInformationExData
    {
        ULONG Flags;
    };

    struct FileBasicInformationData
    {
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        ULONG FileAttributes;
    };

    struct FileNetworkOpenInformationData
    {
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER AllocationSize;
        LARGE_INTEGER EndOfFile;
        ULONG FileAttributes;
    };

    // --- hooked function prototypes ----------------------------------------
    using NtCreateFileFn = NTSTATUS(NTAPI*)(
        PHANDLE FileHandle,
        ACCESS_MASK DesiredAccess,
        POBJECT_ATTRIBUTES ObjectAttributes,
        PIO_STATUS_BLOCK IoStatusBlock,
        PLARGE_INTEGER AllocationSize,
        ULONG FileAttributes,
        ULONG ShareAccess,
        ULONG CreateDisposition,
        ULONG CreateOptions,
        PVOID EaBuffer,
        ULONG EaLength);

    using NtOpenFileFn = NTSTATUS(NTAPI*)(
        PHANDLE FileHandle,
        ACCESS_MASK DesiredAccess,
        POBJECT_ATTRIBUTES ObjectAttributes,
        PIO_STATUS_BLOCK IoStatusBlock,
        ULONG ShareAccess,
        ULONG OpenOptions);

    using NtQueryAttributesFileFn = NTSTATUS(NTAPI*)(
        POBJECT_ATTRIBUTES ObjectAttributes,
        FileBasicInformationData* FileInformation);

    using NtQueryFullAttributesFileFn = NTSTATUS(NTAPI*)(
        POBJECT_ATTRIBUTES ObjectAttributes,
        FileNetworkOpenInformationData* FileInformation);

    using PIoApcRoutine = VOID(NTAPI*)(PVOID, PIO_STATUS_BLOCK, ULONG);

    using NtQueryDirectoryFileFn = NTSTATUS(NTAPI*)(
        HANDLE FileHandle,
        HANDLE Event,
        PIoApcRoutine ApcRoutine,
        PVOID ApcContext,
        PIO_STATUS_BLOCK IoStatusBlock,
        PVOID FileInformation,
        ULONG Length,
        ULONG FileInformationClass,
        BOOLEAN ReturnSingleEntry,
        PUNICODE_STRING FileName,
        BOOLEAN RestartScan);

    using NtQueryDirectoryFileExFn = NTSTATUS(NTAPI*)(
        HANDLE FileHandle,
        HANDLE Event,
        PIoApcRoutine ApcRoutine,
        PVOID ApcContext,
        PIO_STATUS_BLOCK IoStatusBlock,
        PVOID FileInformation,
        ULONG Length,
        ULONG FileInformationClass,
        ULONG QueryFlags,
        PUNICODE_STRING FileName);

    using NtSetInformationFileFn = NTSTATUS(NTAPI*)(
        HANDLE FileHandle,
        PIO_STATUS_BLOCK IoStatusBlock,
        PVOID FileInformation,
        ULONG Length,
        ULONG FileInformationClass);

    using NtDeleteFileFn = NTSTATUS(NTAPI*)(
        POBJECT_ATTRIBUTES ObjectAttributes);

    using NtCloseFn = NTSTATUS(NTAPI*)(HANDLE Handle);
}
