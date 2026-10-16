///////////////////////////////////////////////////////////////////////////////
// Alone In The Dark Re-Haunted
// Copyright (C) 2026 Infogrames / Spacefarer Retro Remasters LLC
// Based on FITD by yaz0r, Re-haunted is released under GPL
// Author: Jake Jackson (jake@spacefarergames.com)
//
// Exception and crash handler declarations
///////////////////////////////////////////////////////////////////////////////

#pragma once
#ifndef EXCEPTION_HANDLER_H
#define EXCEPTION_HANDLER_H

#ifdef _WIN32
#include <Windows.h>
#include <excpt.h>
#include <stdio.h>
#include <time.h>
#include <DbgHelp.h>
#include <TlHelp32.h>
#include <crtdbg.h>
#include <string.h>

#pragma comment(lib, "Dbghelp.lib")

class ExceptionHandler
{
private:
    static inline FILE* crashLogFile = nullptr;
    static inline bool initialized = false;
    static inline bool continueOnCorruption = true;
    static inline PVOID vehHandle = nullptr;
    static inline volatile long heapWarningCount = 0;
    static inline volatile long breakpointSkipCount = 0;

    static bool BuildDiagnosticPath(char* output, size_t outputSize, const char* fileName)
    {
        if (!output || outputSize == 0 || !fileName) return false;

        DWORD length = GetModuleFileNameA(nullptr, output, static_cast<DWORD>(outputSize));
        if (length == 0 || length >= outputSize) return false;

        char* slash = strrchr(output, '\\');
        if (!slash) slash = strrchr(output, '/');
        if (!slash) return strcpy_s(output, outputSize, fileName) == 0;

        *(slash + 1) = '\0';
        return strcat_s(output, outputSize, fileName) == 0;
    }

    static void OpenCrashLog(const char* mode)
    {
        if (crashLogFile) return;

        char path[32768];
        if (BuildDiagnosticPath(path, sizeof(path), "crash_log.txt"))
            fopen_s(&crashLogFile, path, mode);

        // Retain a working-directory fallback for unusual launch environments.
        if (!crashLogFile)
            fopen_s(&crashLogFile, "crash_log.txt", mode);
    }

    static void LogAddressModule(FILE* file, const char* label, DWORD64 address)
    {
        HMODULE module = nullptr;
        char modulePath[32768] = {};

        if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(address)), &module))
        {
            GetModuleFileNameA(module, modulePath, static_cast<DWORD>(sizeof(modulePath)));
            fprintf(file, "%s: %s\n", label, modulePath[0] ? modulePath : "(path unavailable)");
            fprintf(file, "%s Base: 0x%p\n", label, module);
            fprintf(file, "%s Offset: 0x%llX\n", label,
                address - reinterpret_cast<DWORD64>(module));
        }
        else
        {
            fprintf(file, "%s: (no loaded module owns 0x%016llX)\n", label, address);
        }
    }

    static void LogLoadedModules(FILE* file)
    {
        fprintf(file, "\nLoaded Modules:\n");
        HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            fprintf(file, "  (module snapshot failed: Win32 error %lu)\n", GetLastError());
            return;
        }

        MODULEENTRY32 module = {};
        module.dwSize = sizeof(module);
        if (Module32First(snapshot, &module))
        {
            do
            {
                fprintf(file, "  0x%p size=0x%08lX %s\n",
                    module.modBaseAddr, module.modBaseSize, module.szExePath);
            } while (Module32Next(snapshot, &module));
        }
        else
        {
            fprintf(file, "  (module enumeration failed: Win32 error %lu)\n", GetLastError());
        }
        CloseHandle(snapshot);
    }

    static bool WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo, char* dumpPath, size_t dumpPathSize,
        DWORD* errorCode)
    {
        if (errorCode) *errorCode = ERROR_SUCCESS;

        SYSTEMTIME now;
        GetLocalTime(&now);
        char dumpName[128];
        sprintf_s(dumpName, sizeof(dumpName),
            "crash_dump_%04u%02u%02u_%02u%02u%02u_p%lu_t%lu.dmp",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            GetCurrentProcessId(), GetCurrentThreadId());

        if (!BuildDiagnosticPath(dumpPath, dumpPathSize, dumpName))
        {
            if (errorCode) *errorCode = ERROR_BUFFER_OVERFLOW;
            return false;
        }

        HANDLE dumpFile = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dumpFile == INVALID_HANDLE_VALUE)
        {
            if (errorCode) *errorCode = GetLastError();
            return false;
        }

        MINIDUMP_EXCEPTION_INFORMATION exception = {};
        exception.ThreadId = GetCurrentThreadId();
        exception.ExceptionPointers = exceptionInfo;
        exception.ClientPointers = FALSE;

        MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpScanMemory |
            MiniDumpWithThreadInfo |
            MiniDumpWithUnloadedModules |
            MiniDumpWithFullMemoryInfo
#ifdef AITD_HEAP_STRICT
            // TatouScenarioStrict: globals and the whole heap, to find what corrupted it.
            | MiniDumpWithDataSegs | MiniDumpWithPrivateReadWriteMemory | MiniDumpWithHandleData
#endif
            );

        BOOL success = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
            dumpType, exceptionInfo ? &exception : nullptr, nullptr, nullptr);
        DWORD result = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(dumpFile);

        if (!success)
        {
            DeleteFileA(dumpPath);
            if (errorCode) *errorCode = result;
            return false;
        }
        return true;
    }

    // Vectored Exception Handler - fires BEFORE all other handlers.
    // This is critical for catching __debugbreak() raised by the debug CRT
    // when it detects heap corruption during _free_dbg. The CRT issues an
    // int3 (STATUS_BREAKPOINT 0x80000003) which would otherwise kill the
    // process or trigger the debugger. We intercept it here, log it, and
    // skip past it so the DOS-era game code can keep running.
    static LONG WINAPI VectoredHandler(EXCEPTION_POINTERS* exceptionInfo)
    {
        DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;

#ifdef AITD_HEAP_STRICT
        // TatouScenarioStrict: never continue on a corrupted heap; dump at the detection.
        if (code == 0xC0000374 || code == STATUS_BREAKPOINT)
            StrictStop(exceptionInfo, code == 0xC0000374 ? "HEAP_CORRUPTION" : "CRT_BREAKPOINT", nullptr);
#endif

        // STATUS_BREAKPOINT (0x80000003) - raised by __debugbreak() in CRT
        // debug heap when corruption is detected. Skip the int3 instruction
        // and continue execution so the game survives.
        if (code == STATUS_BREAKPOINT && continueOnCorruption)
        {
            long count = InterlockedIncrement(&breakpointSkipCount);
            // Log first few and then periodically to avoid flooding
            if (count <= 5 || (count % 100) == 0)
            {
                char msg[256];
                sprintf_s(msg, sizeof(msg),
                    "WARNING: CRT debug break #%ld at 0x%p skipped (DOS buffer overrun)\n",
                    count, exceptionInfo->ExceptionRecord->ExceptionAddress);
                OutputDebugStringA(msg);
                printf("%s", msg);

                LogExceptionQuiet(exceptionInfo, "CRT_BREAKPOINT_SKIP");
            }

            // Advance RIP past the int3 instruction (1 byte on x64 and x86)
#ifdef _M_AMD64
            exceptionInfo->ContextRecord->Rip += 1;
#else
            exceptionInfo->ContextRecord->Eip += 1;
#endif
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        // STATUS_HEAP_CORRUPTION (0xC0000374) - sometimes raised directly
        // by ntdll RtlReportCriticalFailure instead of going through int3.
        if (code == 0xC0000374 && continueOnCorruption)
        {
            long count = InterlockedIncrement(&heapWarningCount);
            if (count <= 5 || (count % 100) == 0)
            {
                char msg[256];
                sprintf_s(msg, sizeof(msg),
                    "WARNING: Heap corruption #%ld detected and skipped (DOS buffer overrun)\n",
                    count);
                OutputDebugStringA(msg);
                printf("%s", msg);

                LogExceptionQuiet(exceptionInfo, "HEAP_CORRUPTION_SKIP");
            }

            return EXCEPTION_CONTINUE_EXECUTION;
        }

        // Everything else: let other handlers deal with it
        return EXCEPTION_CONTINUE_SEARCH;
    }

    static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
    {
        LogException(exceptionInfo, "UNHANDLED");

        // Show message box
        char message[512];
        sprintf_s(message, sizeof(message),
            "A critical error has occurred (0x%08lX).\n\n"
            "Details have been logged beside Tatou.exe:\n"
            "- crash_log.txt\n"
            "- crash_dump_*.dmp\n\n"
            "The application will now terminate.",
            exceptionInfo->ExceptionRecord->ExceptionCode);

        MessageBoxA(NULL, message, "Critical Error", MB_OK | MB_ICONERROR);

        return EXCEPTION_EXECUTE_HANDLER; // Terminate
    }

    // Lightweight log that avoids heavy symbol resolution (safe for VEH context)
    static void LogExceptionQuiet(EXCEPTION_POINTERS* exceptionInfo, const char* category)
    {
        if (!crashLogFile)
        {
            OpenCrashLog("a");
        }
        if (!crashLogFile) return;

        time_t now = time(nullptr);
        char timeStr[64];
        ctime_s(timeStr, sizeof(timeStr), &now);
        timeStr[strlen(timeStr) - 1] = '\0';

        fprintf(crashLogFile, "[%s] %s: code=0x%08lX addr=0x%p\n",
            timeStr, category,
            exceptionInfo->ExceptionRecord->ExceptionCode,
            exceptionInfo->ExceptionRecord->ExceptionAddress);
        fflush(crashLogFile);
    }

#ifdef _DEBUG
    // CRT report hook - intercepts _CrtDbgReport calls (asserts, errors)
    // and suppresses the ones that would show dialogs or trigger breaks.
    static int __cdecl CrtReportHook(int reportType, char* message, int* returnValue)
    {
        if (!continueOnCorruption)
            return 0; // Let CRT handle it normally

        // Log the CRT message but don't break/abort
        if (!crashLogFile) OpenCrashLog("a");
        if (crashLogFile)
        {
            fprintf(crashLogFile, "[CRT_REPORT type=%d] %s\n", reportType, message ? message : "(null)");
            fflush(crashLogFile);
        }

        if (message)
        {
            OutputDebugStringA("CRT suppressed: ");
            OutputDebugStringA(message);
        }

        // returnValue = 0 means "don't trigger __debugbreak()" in _CrtDbgReport
        if (returnValue)
            *returnValue = 0;

        return 1; // 1 = we handled it, don't call default CRT handler
    }
#endif

    static void LogException(EXCEPTION_POINTERS* exceptionInfo, const char* category)
    {
        if (!crashLogFile)
        {
            OpenCrashLog("a");
        }

        if (!crashLogFile) return;

        time_t now = time(nullptr);
        char timeStr[64];
        ctime_s(timeStr, sizeof(timeStr), &now);
        timeStr[strlen(timeStr) - 1] = '\0'; // Remove newline

        fprintf(crashLogFile, "\n=== %s EXCEPTION: %s ===\n", category, timeStr);
        fprintf(crashLogFile, "Exception Code: 0x%08lX\n", exceptionInfo->ExceptionRecord->ExceptionCode);
        fprintf(crashLogFile, "Exception Address: 0x%p\n", exceptionInfo->ExceptionRecord->ExceptionAddress);
        fprintf(crashLogFile, "Exception Flags: 0x%08lX\n", exceptionInfo->ExceptionRecord->ExceptionFlags);
        fprintf(crashLogFile, "Process ID: %lu\n", GetCurrentProcessId());
        fprintf(crashLogFile, "Thread ID: %lu\n", GetCurrentThreadId());

        char executablePath[32768] = {};
        char workingDirectory[32768] = {};
        GetModuleFileNameA(nullptr, executablePath, static_cast<DWORD>(sizeof(executablePath)));
        GetCurrentDirectoryA(static_cast<DWORD>(sizeof(workingDirectory)), workingDirectory);
        fprintf(crashLogFile, "Executable: %s\n", executablePath);
        fprintf(crashLogFile, "Working Directory: %s\n", workingDirectory);
        fprintf(crashLogFile, "Command Line: %s\n", GetCommandLineA());
        LogAddressModule(crashLogFile, "Faulting Module",
            reinterpret_cast<DWORD64>(exceptionInfo->ExceptionRecord->ExceptionAddress));

        // Decode common exception codes
        switch (exceptionInfo->ExceptionRecord->ExceptionCode)
        {
        case EXCEPTION_ACCESS_VIOLATION:
        {
            fprintf(crashLogFile, "Type: ACCESS VIOLATION\n");
            if (exceptionInfo->ExceptionRecord->NumberParameters >= 2)
            {
                ULONG_PTR accessType = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
                ULONG_PTR address = exceptionInfo->ExceptionRecord->ExceptionInformation[1];
                fprintf(crashLogFile, "  %s at address 0x%p\n",
                    accessType == 0 ? "Read" : (accessType == 1 ? "Write" : "Execute"),
                    (void*)address);
            }
            break;
        }
        case 0xC0000374: // STATUS_HEAP_CORRUPTION
            fprintf(crashLogFile, "Type: HEAP CORRUPTION\n");
            fprintf(crashLogFile, "  The heap has been corrupted. This usually indicates:\n");
            fprintf(crashLogFile, "  - Buffer overrun/underrun\n");
            fprintf(crashLogFile, "  - Double-free\n");
            fprintf(crashLogFile, "  - Use-after-free\n");
            fprintf(crashLogFile, "  - Mismatched allocation/deallocation\n");
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            fprintf(crashLogFile, "Type: ARRAY BOUNDS EXCEEDED\n");
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            fprintf(crashLogFile, "Type: DATATYPE MISALIGNMENT\n");
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            fprintf(crashLogFile, "Type: ILLEGAL INSTRUCTION\n");
            break;
        case EXCEPTION_STACK_OVERFLOW:
            fprintf(crashLogFile, "Type: STACK OVERFLOW\n");
            break;
        default:
            fprintf(crashLogFile, "Type: UNKNOWN (0x%08lX)\n", exceptionInfo->ExceptionRecord->ExceptionCode);
            break;
        }

#ifdef _M_AMD64
        fprintf(crashLogFile,
            "\nRegisters:\n"
            "  RIP=0x%016llX RSP=0x%016llX RBP=0x%016llX\n"
            "  RAX=0x%016llX RBX=0x%016llX RCX=0x%016llX RDX=0x%016llX\n"
            "  RSI=0x%016llX RDI=0x%016llX R8 =0x%016llX R9 =0x%016llX\n"
            "  R10=0x%016llX R11=0x%016llX R12=0x%016llX R13=0x%016llX\n"
            "  R14=0x%016llX R15=0x%016llX EFlags=0x%08lX\n",
            exceptionInfo->ContextRecord->Rip, exceptionInfo->ContextRecord->Rsp,
            exceptionInfo->ContextRecord->Rbp, exceptionInfo->ContextRecord->Rax,
            exceptionInfo->ContextRecord->Rbx, exceptionInfo->ContextRecord->Rcx,
            exceptionInfo->ContextRecord->Rdx, exceptionInfo->ContextRecord->Rsi,
            exceptionInfo->ContextRecord->Rdi, exceptionInfo->ContextRecord->R8,
            exceptionInfo->ContextRecord->R9, exceptionInfo->ContextRecord->R10,
            exceptionInfo->ContextRecord->R11, exceptionInfo->ContextRecord->R12,
            exceptionInfo->ContextRecord->R13, exceptionInfo->ContextRecord->R14,
            exceptionInfo->ContextRecord->R15, exceptionInfo->ContextRecord->EFlags);
#endif

        // Get stack trace
        fprintf(crashLogFile, "\nStack Trace:\n");
        
        CONTEXT context = *exceptionInfo->ContextRecord;
        STACKFRAME64 stackFrame;
        memset(&stackFrame, 0, sizeof(STACKFRAME64));

#ifdef _M_AMD64
        DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
        stackFrame.AddrPC.Offset = context.Rip;
        stackFrame.AddrFrame.Offset = context.Rbp;
        stackFrame.AddrStack.Offset = context.Rsp;
#else
        DWORD machineType = IMAGE_FILE_MACHINE_I386;
        stackFrame.AddrPC.Offset = context.Eip;
        stackFrame.AddrFrame.Offset = context.Ebp;
        stackFrame.AddrStack.Offset = context.Esp;
#endif
        
        stackFrame.AddrPC.Mode = AddrModeFlat;
        stackFrame.AddrFrame.Mode = AddrModeFlat;
        stackFrame.AddrStack.Mode = AddrModeFlat;

        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        
        // Initialize symbol handling. RelWithDebInfo builds place Tatou.pdb next
        // to the executable, and DbgHelp also discovers symbols for DLL frames.
        SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        BOOL initializedSymbols = SymInitialize(process, nullptr, TRUE);
        if (!initializedSymbols && GetLastError() != ERROR_INVALID_PARAMETER)
            fprintf(crashLogFile, "  (symbol initialization failed: Win32 error %lu)\n", GetLastError());
        else
            SymRefreshModuleList(process);
        
        int frameNum = 0;
        DWORD64 previousAddress = 0;
        while (stackFrame.AddrPC.Offset != 0 && frameNum < 32)
        {
            fprintf(crashLogFile, "  [%d] 0x%016llX", frameNum, stackFrame.AddrPC.Offset);
            
            // Try to get symbol information
            char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            
            DWORD64 displacement = 0;
            if (SymFromAddr(process, stackFrame.AddrPC.Offset, &displacement, symbol))
            {
                fprintf(crashLogFile, " - %s+0x%llX", symbol->Name, displacement);
            }

            IMAGEHLP_MODULE64 moduleInfo = {};
            moduleInfo.SizeOfStruct = sizeof(moduleInfo);
            if (SymGetModuleInfo64(process, stackFrame.AddrPC.Offset, &moduleInfo))
                fprintf(crashLogFile, " [%s+0x%llX]", moduleInfo.ModuleName,
                    stackFrame.AddrPC.Offset - moduleInfo.BaseOfImage);
            
            // Try to get line information
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, stackFrame.AddrPC.Offset, &lineDisplacement, &line))
            {
                fprintf(crashLogFile, " (%s:%d)", line.FileName, line.LineNumber);
            }
            
            fprintf(crashLogFile, "\n");
            frameNum++;

            previousAddress = stackFrame.AddrPC.Offset;
            if (!StackWalk64(machineType, process, thread, &stackFrame, &context,
                nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                stackFrame.AddrPC.Offset == previousAddress)
                break;
        }

        LogLoadedModules(crashLogFile);

        char dumpPath[32768] = {};
        DWORD dumpError = ERROR_SUCCESS;
        fflush(crashLogFile);
        if (WriteMiniDump(exceptionInfo, dumpPath, sizeof(dumpPath), &dumpError))
            fprintf(crashLogFile, "\nMinidump: %s\n", dumpPath);
        else
            fprintf(crashLogFile, "\nMinidump creation failed: Win32 error %lu\n", dumpError);

        if (initializedSymbols)
            SymCleanup(process);

        fprintf(crashLogFile, "=== End of Exception Report ===\n\n");
        fflush(crashLogFile);
    }

#ifdef AITD_HEAP_STRICT
    // TatouScenarioStrict (Research/Crash_2026-10-16_createAITD1Mask.md): the stock handler
    // skips heap corruption, which hides the faulty write and crashes much later elsewhere.
    // This build stops instead, writes a full dump at the first detection, and checks the
    // heap itself every game frame (StrictHeapCheck) to narrow down when it was corrupted.
    static const int STRICT_TRAIL = 24;
    static inline char strictTrail[STRICT_TRAIL][192] = {};   // game-state changes, ring buffer
    static inline char strictLastState[160] = "";
    static inline int strictTrailCount = 0;
    static inline long long strictFrame = 0, strictLastGoodFrame = 0;
    static inline double strictLastGoodTime = 0.0;

    static double StrictNow()
    {
        static LARGE_INTEGER freq = {}, start = {};
        if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start); }
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        return double(t.QuadPart - start.QuadPart) / double(freq.QuadPart);
    }

    // The dump is written by a helper process (this exe, --strict-dump-helper <pid>) started at
    // launch: an in-process MiniDumpWriteDump allocates from the corrupted heap, re-raises the
    // corruption and deadlocks. Request/reply through a shared block and two events, so the
    // stop itself needs no heap.
    struct StrictShared
    {
        DWORD threadId;
        ULONG64 exceptionPointers;     // address in the game process (ClientPointers = TRUE)
        DWORD result;                  // Win32 error, 0 = written
        char dumpPath[2048];
    };
    static inline StrictShared* strictShared = nullptr;
    static inline HANDLE strictRequest = nullptr, strictDone = nullptr;
    static inline volatile DWORD strictStopThread = 0;

    static MINIDUMP_TYPE StrictDumpType()
    {
        return static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
            MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo |
            MiniDumpWithDataSegs | MiniDumpWithPrivateReadWriteMemory | MiniDumpWithHandleData);
    }

    static void StrictStartHelper()
    {
        DWORD pid = GetCurrentProcessId();
        char name[96];
        sprintf_s(name, sizeof(name), "Local\\AITDStrictMap_%lu", pid);
        HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(StrictShared), name);
        if (!map) return;
        strictShared = (StrictShared*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(StrictShared));
        sprintf_s(name, sizeof(name), "Local\\AITDStrictReq_%lu", pid);
        strictRequest = CreateEventA(nullptr, FALSE, FALSE, name);
        sprintf_s(name, sizeof(name), "Local\\AITDStrictDone_%lu", pid);
        strictDone = CreateEventA(nullptr, FALSE, FALSE, name);

        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        char cmd[MAX_PATH + 64];
        sprintf_s(cmd, sizeof(cmd), "\"%s\" --strict-dump-helper %lu", exe, pid);
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        else
        {
            strictShared = nullptr;      // no helper: StrictStop falls back to an in-process dump
        }
    }

public:
    // Helper process side. Returns true (and the process should exit) if this is the helper.
    static bool StrictDumpHelperMain()
    {
        const char* arg = strstr(GetCommandLineA(), "--strict-dump-helper ");
        if (!arg) return false;
        DWORD pid = strtoul(arg + 21, nullptr, 10);
        HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        char name[96];
        sprintf_s(name, sizeof(name), "Local\\AITDStrictMap_%lu", pid);
        HANDLE map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
        sprintf_s(name, sizeof(name), "Local\\AITDStrictReq_%lu", pid);
        HANDLE req = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
        sprintf_s(name, sizeof(name), "Local\\AITDStrictDone_%lu", pid);
        HANDLE done = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
        if (!proc || !map || !req || !done) return true;
        StrictShared* sh = (StrictShared*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(StrictShared));
        HANDLE waits[2] = { req, proc };
        if (!sh || WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) return true;

        HANDLE file = CreateFileA(sh->dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) { sh->result = GetLastError(); SetEvent(done); return true; }
        MINIDUMP_EXCEPTION_INFORMATION mei = {};
        mei.ThreadId = sh->threadId;
        mei.ExceptionPointers = (PEXCEPTION_POINTERS)sh->exceptionPointers;
        mei.ClientPointers = TRUE;
        BOOL ok = MiniDumpWriteDump(proc, pid, file, StrictDumpType(), sh->exceptionPointers ? &mei : nullptr, nullptr, nullptr);
        if (!ok)
        {
            // e.g. 0x8007012B (partial copy, 2026-10-16): retry without the private memory
            SetFilePointer(file, 0, nullptr, FILE_BEGIN);
            SetEndOfFile(file);
            ok = MiniDumpWriteDump(proc, pid, file, static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpScanMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo |
                MiniDumpWithDataSegs | MiniDumpWithHandleData), sh->exceptionPointers ? &mei : nullptr, nullptr, nullptr);
        }
        sh->result = ok ? 0 : GetLastError();
        CloseHandle(file);
        SetEvent(done);
        return true;
    }

private:
    [[noreturn]] static void StrictStop(EXCEPTION_POINTERS* exceptionInfo, const char* what, const char* detail)
    {
        DWORD self = GetCurrentThreadId();
        if (InterlockedCompareExchange((volatile LONG*)&strictStopThread, (LONG)self, 0) != 0)
        {
            if (strictStopThread == self)            // the report itself hit the corrupted heap
                TerminateProcess(GetCurrentProcess(), 0xC0000374);
            for (;;) Sleep(INFINITE);                // another thread is writing the report
        }

        char dumpPath[2048] = {};
        DWORD dumpError = ERROR_SUCCESS;
        bool dumped = false;
        SYSTEMTIME now;
        GetLocalTime(&now);
        char dumpName[128];
        sprintf_s(dumpName, sizeof(dumpName), "strict_dump_%04u%02u%02u_%02u%02u%02u_p%lu_t%lu.dmp",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId(), self);
        if (strictShared && BuildDiagnosticPath(dumpPath, sizeof(dumpPath), dumpName))
        {
            strcpy_s(strictShared->dumpPath, sizeof(strictShared->dumpPath), dumpPath);
            strictShared->threadId = self;
            strictShared->exceptionPointers = (ULONG64)exceptionInfo;
            strictShared->result = ERROR_TIMEOUT;
            SetEvent(strictRequest);
            if (WaitForSingleObject(strictDone, 10 * 60 * 1000) == WAIT_OBJECT_0)
                dumpError = strictShared->result;
            else
                dumpError = ERROR_TIMEOUT;
            dumped = dumpError == ERROR_SUCCESS;
        }
        else
        {
            dumped = WriteMiniDump(exceptionInfo, dumpPath, sizeof(dumpPath), &dumpError);
        }

        if (!crashLogFile) OpenCrashLog("a");
        if (crashLogFile)
        {
            time_t now = time(nullptr);
            char timeStr[64];
            ctime_s(timeStr, sizeof(timeStr), &now);
            timeStr[strlen(timeStr) - 1] = '\0';
            fprintf(crashLogFile, "\n=== STRICT STOP: %s at %s (%.3f s after start) ===\n", what, timeStr, StrictNow());
            if (exceptionInfo)
            {
                fprintf(crashLogFile, "Exception code 0x%08lX at 0x%p, thread %lu\n",
                    exceptionInfo->ExceptionRecord->ExceptionCode,
                    exceptionInfo->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
                LogAddressModule(crashLogFile, "Module",
                    reinterpret_cast<DWORD64>(exceptionInfo->ExceptionRecord->ExceptionAddress));
            }
            if (detail)
                fprintf(crashLogFile, "%s\n", detail);
            fprintf(crashLogFile, "Game frame %lld; last frame with a valid heap: %lld (%.3f s after start)\n",
                strictFrame, strictLastGoodFrame, strictLastGoodTime);
            fprintf(crashLogFile, "Recent game-state changes (oldest first):\n");
            int n = strictTrailCount < STRICT_TRAIL ? strictTrailCount : STRICT_TRAIL;
            for (int i = 0; i < n; i++)
                fprintf(crashLogFile, "  %s\n", strictTrail[(strictTrailCount - n + i) % STRICT_TRAIL]);
            if (dumped) fprintf(crashLogFile, "Minidump: %s\n", dumpPath);
            else fprintf(crashLogFile, "Minidump creation failed: Win32 error %lu\n", dumpError);
            fflush(crashLogFile);
            void eventTrailWrite(FILE*);
            eventTrailWrite(crashLogFile);
            fprintf(crashLogFile, "=== End of strict report ===\n");
            fflush(crashLogFile);
        }

        char message[1024];
        sprintf_s(message, sizeof(message),
            "TatouScenarioStrict stopped: %s.\n\nPlease send crash_log.txt and the newest "
            "crash_dump_*.dmp from the exe folder.\n\n%s", what, dumped ? dumpPath : "(minidump failed)");
        char selftest[8] = {};
        if (!GetEnvironmentVariableA("AITD_STRICT_SELFTEST", selftest, sizeof(selftest)))
            MessageBoxA(NULL, message, "TatouScenarioStrict", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SYSTEMMODAL);
        TerminateProcess(GetCurrentProcess(), 0xC0000374);
        for (;;) Sleep(INFINITE);
    }

public:
    // Called once per game frame (game thread) with a short game-state description.
    static void StrictHeapCheck(const char* state)
    {
        static int every = 0;
        static double total = 0.0, worst = 0.0, lastReport = 0.0;
        static long long checks = 0;
        if (!every)
        {
            char buf[32] = {};
            int v = GetEnvironmentVariableA("AITD_HEAP_CHECK_EVERY", buf, sizeof(buf)) ? atoi(buf) : 0;
            every = v > 0 ? v : 1;
        }
        strictFrame++;

        if (strcmp(strictLastState, state) != 0)
        {
            strcpy_s(strictLastState, sizeof(strictLastState), state);
            sprintf_s(strictTrail[strictTrailCount % STRICT_TRAIL], sizeof(strictTrail[0]),
                "frame %lld, %.3f s: %s", strictFrame, StrictNow(), state);
            strictTrailCount++;
        }

        if (strictFrame % every) return;
        double t0 = StrictNow();
        BOOL ok = HeapValidate(GetProcessHeap(), 0, nullptr);
        double dt = (StrictNow() - t0) * 1000.0;
        if (!ok)
        {
            char detail[320];
            sprintf_s(detail, sizeof(detail), "HeapValidate(process heap) FAILED in the game loop at: %s", state);
            StrictStop(nullptr, "HEAP_VALIDATE_FAILED", detail);
        }
        strictLastGoodFrame = strictFrame;
        strictLastGoodTime = StrictNow();
        checks++; total += dt; if (dt > worst) worst = dt;
        if (strictLastGoodTime - lastReport > 60.0)
        {
            lastReport = strictLastGoodTime;
            if (!crashLogFile) OpenCrashLog("a");
            if (crashLogFile)
            {
                fprintf(crashLogFile, "[strict] %.0f s: %lld heap checks (every %d frames), avg %.2f ms, max %.2f ms\n",
                    strictLastGoodTime, checks, every, total / checks, worst);
                fclose(crashLogFile);
                crashLogFile = nullptr;
            }
        }
    }

#endif

public:
    static void Initialize(bool continueOnHeapCorruption = true)
    {
        if (initialized) return;

        initialized = true;
        continueOnCorruption = continueOnHeapCorruption;

        // Register Vectored Exception Handler FIRST (1 = first in chain).
        // VEH fires before SEH and before SetUnhandledExceptionFilter, so it
        // can intercept __debugbreak() from the debug CRT before the process
        // is killed. This is essential for surviving DOS-era buffer overruns.
        vehHandle = AddVectoredExceptionHandler(1, VectoredHandler);

        // Set unhandled exception filter as fallback for anything VEH doesn't catch
        SetUnhandledExceptionFilter(UnhandledExceptionFilter);

#ifdef AITD_HEAP_STRICT
        StrictStartHelper();
#endif

#ifdef _DEBUG
        if (continueOnHeapCorruption)
        {
            // Reduce CRT debug heap checking to avoid constant __debugbreak()
            // calls. Keep leak checking at exit but disable per-operation checks.
            int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
            flags &= ~_CRTDBG_CHECK_ALWAYS_DF;  // Don't check heap on every alloc/free
            flags &= ~_CRTDBG_DELAY_FREE_MEM_DF; // Don't keep freed blocks
            flags |= _CRTDBG_ALLOC_MEM_DF;       // Keep basic allocation tracking
            _CrtSetDbgFlag(flags);

            // Install report hook to suppress CRT assert/error dialogs and breaks
            _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CrtReportHook);

            // Redirect CRT reports to debug output instead of popup dialogs
            _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
            _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
        }
#endif

        // Create or clear crash log
        OpenCrashLog("w");
        if (crashLogFile)
        {
            fprintf(crashLogFile, "=== Application Started - Exception Handler Active ===\n");
            fprintf(crashLogFile, "Continue on heap corruption: %s\n", continueOnHeapCorruption ? "YES" : "NO");
            fprintf(crashLogFile, "Vectored Exception Handler: %s\n", vehHandle ? "REGISTERED" : "FAILED");
            fprintf(crashLogFile, "Crash output directory: executable directory\n");
            fprintf(crashLogFile, "Minidumps: ENABLED\n");
#ifdef AITD_HEAP_STRICT
            fprintf(crashLogFile, "STRICT BUILD (TatouScenarioStrict): stops on heap corruption, full dumps, heap check every game frame\n");
            fprintf(crashLogFile, "Strict dump helper process: %s\n", strictShared ? "STARTED" : "FAILED (in-process dumps)");
#endif
#ifdef _DEBUG
            fprintf(crashLogFile, "CRT debug heap checks: REDUCED (DOS compatibility mode)\n");
            fprintf(crashLogFile, "CRT report hook: INSTALLED\n");
#endif
            fprintf(crashLogFile, "\n");
            fclose(crashLogFile);
            crashLogFile = nullptr; // Will be reopened on exception
        }
    }

    static void Shutdown()
    {
#ifdef _DEBUG
        if (continueOnCorruption)
        {
            _CrtSetReportHook2(_CRT_RPTHOOK_REMOVE, CrtReportHook);
        }
#endif

        if (vehHandle)
        {
            RemoveVectoredExceptionHandler(vehHandle);
            vehHandle = nullptr;
        }

        if (!crashLogFile) OpenCrashLog("a");
        if (crashLogFile)
        {
            fprintf(crashLogFile, "\n=== Shutdown ===\n");
            fprintf(crashLogFile, "Breakpoints skipped: %ld\n", breakpointSkipCount);
            fprintf(crashLogFile, "Heap corruptions skipped: %ld\n", heapWarningCount);
            fclose(crashLogFile);
            crashLogFile = nullptr;
        }
    }

    // Exception filter for critical sections (used by PROTECTED_CALL macro)
    static int HandleException(EXCEPTION_POINTERS* exceptionInfo, const char* context)
    {
        DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;

        // For heap corruption, skip and continue (lightweight log only)
        if (code == 0xC0000374) // STATUS_HEAP_CORRUPTION
        {
            LogExceptionQuiet(exceptionInfo, context);
            printf("WARNING: Heap corruption detected in %s - logged and continuing\n", context);
            return EXCEPTION_EXECUTE_HANDLER; // Execute the __except block
        }

        // For breakpoints from CRT debug heap, execute handler to skip (lightweight log only)
        if (code == STATUS_BREAKPOINT)
        {
            LogExceptionQuiet(exceptionInfo, context);
            printf("WARNING: CRT debug break in %s - skipped\n", context);
            return EXCEPTION_EXECUTE_HANDLER;
        }

#ifdef AITD_HEAP_STRICT
        if (code == EXCEPTION_ACCESS_VIOLATION)
            StrictStop(exceptionInfo, "ACCESS_VIOLATION", context);
#endif

        // For access violations and other real crashes, write FULL diagnostic log
        // with stack trace, symbol names, and access violation details (read/write addr)
        LogException(exceptionInfo, context);

        if (code == EXCEPTION_ACCESS_VIOLATION)
        {
            printf("ERROR: Access violation in %s - full stack trace written to crash_log.txt\n", context);
            return EXCEPTION_EXECUTE_HANDLER;
        }

        // For other critical exceptions, continue the search
        return EXCEPTION_CONTINUE_SEARCH;
    }
};

// Macro for protected execution
#define PROTECTED_CALL(code, context) \
    __try { \
        code; \
    } \
    __except(ExceptionHandler::HandleException(GetExceptionInformation(), context)) { \
        printf("Exception handled in " context "\n"); \
    }

#else // Non-Windows platforms

class ExceptionHandler
{
public:
    static void Initialize(bool continueOnHeapCorruption = true) {}
    static void Shutdown() {}
};

#define PROTECTED_CALL(code, context) code

#endif // _WIN32

#endif // EXCEPTION_HANDLER_H
