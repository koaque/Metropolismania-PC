// The difftest replay's child processes (see difftest.cpp): a function whose replay cannot share the process with the
// others is replayed by this program started again with MMPC_DIFFTEST_ONLY=<name>. Kept apart from difftest.cpp so the
// Windows headers stay out of the port's other code.

#include "difftest.h"

#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace port
{
    int replayInChildProcess(const char *name)
    {
#ifdef _WIN32
        std::cout.flush();
        std::cerr.flush();
        // The same command line and environment, plus the function and the mark that stops the child from starting one itself.
        std::wstring command = GetCommandLineW();
        const std::wstring wideName(name, name + std::strlen(name));
        wchar_t previousOnly[4096];
        const DWORD previousLength = GetEnvironmentVariableW(L"MMPC_DIFFTEST_ONLY", previousOnly, 4096);
        SetEnvironmentVariableW(L"MMPC_DIFFTEST_ONLY", wideName.c_str());
        SetEnvironmentVariableW(L"MMPC_DIFFTEST_CHILD", L"1");
        // The child writes to the same output (the log the replay goes to).
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        for (HANDLE handle : {startup.hStdOutput, startup.hStdError})
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
                SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        PROCESS_INFORMATION process{};
        const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process);
        SetEnvironmentVariableW(L"MMPC_DIFFTEST_ONLY", previousLength > 0 && previousLength < 4096 ? previousOnly : nullptr);
        SetEnvironmentVariableW(L"MMPC_DIFFTEST_CHILD", nullptr);
        if (!started)
            return -1;
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(code);
#else
        (void)name;
        return -1;
#endif
    }
}
