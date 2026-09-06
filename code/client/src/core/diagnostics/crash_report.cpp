#include "crash_report.h"

#include "breadcrumbs.h"

#include <utils/safe_win32.h>

#include <dbghelp.h>
#include <psapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "shared/constants.h"
#include "shared/version.h"

#pragma comment(lib, "dbghelp.lib")

namespace VantaMP::Diagnostics {
    namespace {
        std::filesystem::path gDirectory;
        LPTOP_LEVEL_EXCEPTION_FILTER gPreviousFilter = nullptr;
        LONG gHandling                               = 0;

        // NOTHING HERE MAY ALLOCATE ONCE A CRASH IS UNDER WAY. The heap is among
        // the things that may be broken, so the report is built with a raw file
        // handle and stack buffers rather than streams and strings.
        class Report final {
          public:
            explicit Report(const std::filesystem::path &path) {
                _file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            }

            ~Report() {
                if (_file != INVALID_HANDLE_VALUE) {
                    ::CloseHandle(_file);
                }
            }

            Report(const Report &)            = delete;
            Report &operator=(const Report &) = delete;

            [[nodiscard]] bool IsOpen() const noexcept {
                return _file != INVALID_HANDLE_VALUE;
            }

            void Write(const char *text) {
                if (_file == INVALID_HANDLE_VALUE || !text) {
                    return;
                }

                DWORD written = 0;
                ::WriteFile(_file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
            }

            void Writef(const char *format, ...) {
                char buffer[1024];

                va_list args;
                va_start(args, format);
                const int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
                va_end(args);

                if (length > 0 && _file != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    ::WriteFile(_file, buffer, static_cast<DWORD>(length), &written, nullptr);
                }
            }

          private:
            HANDLE _file = INVALID_HANDLE_VALUE;
        };

        const char *ExceptionName(DWORD code) {
            switch (code) {
            case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
            case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
            case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
            case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
            case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
            default: return "UNKNOWN";
            }
        }

        // Which module a faulting address belongs to, and how far into it.
        //
        // This is the most useful line in the report: it says whether the fault
        // was in our code, in the game, or in something we called into.
        bool ResolveModule(std::uintptr_t address, char *name, std::size_t size, std::uintptr_t &offset) {
            const HANDLE process = ::GetCurrentProcess();

            HMODULE modules[512];
            DWORD needed = 0;

            if (!::EnumProcessModules(process, modules, sizeof(modules), &needed)) {
                return false;
            }

            const auto count = needed / sizeof(HMODULE);

            for (DWORD i = 0; i < count; ++i) {
                MODULEINFO info {};

                if (!::GetModuleInformation(process, modules[i], &info, sizeof(info))) {
                    continue;
                }

                const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);

                if (address < base || address >= base + info.SizeOfImage) {
                    continue;
                }

                char path[MAX_PATH] {};
                ::GetModuleFileNameExA(process, modules[i], path, MAX_PATH);

                // Filename only; the full path is listed in the module section.
                const char *filename = std::strrchr(path, '\\');
                filename             = filename ? filename + 1 : path;

                std::strncpy(name, filename, size - 1);
                name[size - 1] = '\0';

                offset = address - base;

                return true;
            }

            return false;
        }

        void WriteHeader(Report &report, EXCEPTION_POINTERS *info) {
            const auto *record = info->ExceptionRecord;
            const auto address = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);

            report.Write("============================================================\n");
            report.Write("VantaMP Client Crash\n");
            report.Write("============================================================\n\n");

            report.Writef("VantaMP: %s\n", Version::rel);
            report.Writef("Game:    %s\n\n", Shared::kGameVersion);

            report.Writef("Exception:\n0x%08X %s\n\n", record->ExceptionCode, ExceptionName(record->ExceptionCode));
            report.Writef("Address:\n0x%08X\n\n", static_cast<unsigned>(address));

            char moduleName[MAX_PATH] {};
            std::uintptr_t offset = 0;

            if (ResolveModule(address, moduleName, sizeof(moduleName), offset)) {
                report.Writef("Module:\n%s + 0x%X\n\n", moduleName, static_cast<unsigned>(offset));
            }
            else {
                report.Write("Module:\n<unknown - the address is not inside any loaded module>\n\n");
            }

            // For an access violation the operation and target usually identify
            // the fault outright: a read of a small address is a null
            // dereference at a field offset that names the field.
            if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
                const auto operation = record->ExceptionInformation[0];
                const auto target    = record->ExceptionInformation[1];

                const char *what = operation == 0 ? "READ" : operation == 1 ? "WRITE" : operation == 8 ? "EXECUTE (DEP)" : "UNKNOWN";

                report.Writef("Operation:\n%s\n\n", what);
                report.Writef("Memory address:\n0x%08X\n\n", static_cast<unsigned>(target));
            }
        }

        // What the client thought it was doing. This is the section that turns a
        // bare address into a suspect.
        void WriteVantaState(Report &report) {
            report.Writef("Crash occurred during:\n%s\n\n", ToString(Breadcrumbs::CurrentStage()));
            report.Writef("Last operation:\n%s\n\n", Breadcrumbs::LastOperation());
            report.Writef("Last patch:\n%s\n\n", Breadcrumbs::LastPatch());
            report.Writef("Last hook:\n%s\n\n", Breadcrumbs::LastHook());
            report.Writef("Last native call:\n%s\n\n", Breadcrumbs::LastNativeCall());
        }

        void WriteRegisters(Report &report, const CONTEXT *context) {
            report.Write("Registers:\n");
            report.Writef("eax=%08X ebx=%08X ecx=%08X edx=%08X\n", context->Eax, context->Ebx, context->Ecx, context->Edx);
            report.Writef("esi=%08X edi=%08X ebp=%08X esp=%08X\n", context->Esi, context->Edi, context->Ebp, context->Esp);
            report.Writef("eip=%08X eflags=%08X\n\n", context->Eip, context->EFlags);
        }

        void WriteStackTrace(Report &report, const CONTEXT *context) {
            report.Write("Stack trace:\n");

            const HANDLE process = ::GetCurrentProcess();
            const HANDLE thread  = ::GetCurrentThread();

            ::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
            ::SymInitialize(process, nullptr, TRUE);

            // StackWalk64 modifies the context it walks, so give it a copy: the
            // original is still needed for the minidump below.
            CONTEXT walked = *context;

            STACKFRAME64 frame {};
            frame.AddrPC.Offset    = walked.Eip;
            frame.AddrPC.Mode      = AddrModeFlat;
            frame.AddrFrame.Offset = walked.Ebp;
            frame.AddrFrame.Mode   = AddrModeFlat;
            frame.AddrStack.Offset = walked.Esp;
            frame.AddrStack.Mode   = AddrModeFlat;

            for (int depth = 0; depth < 64; ++depth) {
                if (!::StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame, &walked, nullptr, ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr)) {
                    break;
                }

                if (frame.AddrPC.Offset == 0) {
                    break;
                }

                const auto address = static_cast<std::uintptr_t>(frame.AddrPC.Offset);

                char moduleName[MAX_PATH] {};
                std::uintptr_t offset = 0;

                if (ResolveModule(address, moduleName, sizeof(moduleName), offset)) {
                    report.Writef("  [%02d] 0x%08X  %s + 0x%X", depth, static_cast<unsigned>(address), moduleName, static_cast<unsigned>(offset));
                }
                else {
                    report.Writef("  [%02d] 0x%08X  <unknown>", depth, static_cast<unsigned>(address));
                }

                // A symbol where one exists. GTA ships none, so most frames stay
                // module+offset, which is still enough to look up.
                alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] {};

                auto *symbol         = reinterpret_cast<SYMBOL_INFO *>(symbolStorage);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen   = MAX_SYM_NAME;

                DWORD64 displacement = 0;

                if (::SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                    report.Writef("  %s", symbol->Name);
                }

                report.Write("\n");
            }

            report.Write("\n");
            ::SymCleanup(process);
        }

        void WriteModules(Report &report) {
            report.Write("Loaded modules:\n");

            const HANDLE process = ::GetCurrentProcess();

            HMODULE modules[512];
            DWORD needed = 0;

            if (!::EnumProcessModules(process, modules, sizeof(modules), &needed)) {
                report.Write("  <unavailable>\n\n");
                return;
            }

            const auto count = needed / sizeof(HMODULE);

            for (DWORD i = 0; i < count; ++i) {
                MODULEINFO info {};

                if (!::GetModuleInformation(process, modules[i], &info, sizeof(info))) {
                    continue;
                }

                char path[MAX_PATH] {};

                if (::GetModuleFileNameExA(process, modules[i], path, MAX_PATH) == 0) {
                    continue;
                }

                report.Writef("  0x%08X  %8u  %s\n", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll)), static_cast<unsigned>(info.SizeOfImage), path);
            }

            report.Write("\n");
        }

        void WriteMinidump(const std::filesystem::path &path, EXCEPTION_POINTERS *info) {
            const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (file == INVALID_HANDLE_VALUE) {
                return;
            }

            MINIDUMP_EXCEPTION_INFORMATION exceptionInfo {};
            exceptionInfo.ThreadId          = ::GetCurrentThreadId();
            exceptionInfo.ExceptionPointers = info;
            exceptionInfo.ClientPointers    = FALSE;

            // DELIBERATELY NOT MiniDumpWithFullMemory: a full dump of GTA plus
            // CEF runs to hundreds of megabytes, which nobody will ever send.
            // These flags keep what explains a fault - the memory the registers
            // and stack actually point at - and discard the rest.
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo);

            ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, type, &exceptionInfo, nullptr, nullptr);

            ::CloseHandle(file);
        }

        void Timestamp(wchar_t *buffer, std::size_t size) {
            SYSTEMTIME now {};
            ::GetLocalTime(&now);

            std::swprintf(buffer, size, L"%04u-%02u-%02u_%02u-%02u-%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        }

        LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS *info) {
            // A fault inside the handler must not recurse into it.
            if (::InterlockedExchange(&gHandling, 1) != 0) {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            std::error_code ec;
            std::filesystem::create_directories(gDirectory, ec);

            wchar_t stamp[64] {};
            Timestamp(stamp, std::size(stamp));

            const auto logPath  = gDirectory / (std::wstring(stamp) + L".log");
            const auto dumpPath = gDirectory / (std::wstring(stamp) + L".dmp");

            {
                Report report(logPath);

                if (report.IsOpen()) {
                    WriteHeader(report, info);
                    WriteVantaState(report);
                    WriteRegisters(report, info->ContextRecord);
                    WriteStackTrace(report, info->ContextRecord);
                    WriteModules(report);

                    report.Write("============================================================\n");
                }
            }

            WriteMinidump(dumpPath, info);

            // Also visible to an attached debugger, which is often the fastest
            // way to notice a crash happened at all.
            ::OutputDebugStringW(L"[VantaMP] crash report written to ");
            ::OutputDebugStringW(logPath.c_str());
            ::OutputDebugStringW(L"\n");

            return gPreviousFilter ? gPreviousFilter(info) : EXCEPTION_EXECUTE_HANDLER;
        }
    } // namespace

    namespace CrashReport {
        void Install(const std::filesystem::path &directory) {
            gDirectory      = directory;
            gPreviousFilter = ::SetUnhandledExceptionFilter(&OnUnhandledException);
        }

        void Uninstall() {
            ::SetUnhandledExceptionFilter(gPreviousFilter);
            gPreviousFilter = nullptr;
        }
    } // namespace CrashReport
} // namespace VantaMP::Diagnostics
