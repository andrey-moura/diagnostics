#include <basic_debugger.hpp>

#include <locale.h>
#include <string.h>

#include <iostream>
#include <string>
#include <filesystem>

using namespace uva;
using namespace diagnostics;

#ifdef __UVA_WIN__
std::string get_last_error_message() {
    //Get the error message ID, if any.
    DWORD errorMessageID = ::GetLastError();
    if(errorMessageID == 0) {
        return std::string(); //No error message has been recorded
    }
    
    LPSTR messageBuffer = nullptr;

    //Ask Win32 to give us the string version of that message ID.
    //The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
    
    //Copy the error message into a std::string.
    std::string message(messageBuffer, size);
    
    //Free the Win32's string's buffer.
    LocalFree(messageBuffer);
            
    return message;
}

std::string GetFileNameFromHandle(HANDLE hFile) 
{
    BOOL bSuccess = FALSE;
    TCHAR buffer[MAX_PATH+1];

    // Get the file size.
    DWORD dwFileSizeHi = 0;
    DWORD dwFileSizeLo = GetFileSize(hFile, &dwFileSizeHi); 

    if( dwFileSizeLo != 0 || dwFileSizeHi != 0 )
    {     
        // Create a file mapping object.
        HANDLE hFileMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0,  1, NULL);

        if (hFileMap) 
        {
            // Create a file mapping to get the file name.
            void* pMem = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 1);

            if (pMem) 
            {
                // Gets the full path of the file, instead of the drive letter (ie C://), it has the device name
                if (GetMappedFileName (GetCurrentProcess(), pMem,  buffer, MAX_PATH)) 
                {
                    TCHAR drive_letters[MAX_PATH+1];
                    const char* it = drive_letters;
                    if (GetLogicalDriveStrings(MAX_PATH, drive_letters)) 
                    {
                        std::string drive_path = "x:";
                        char drive_letter = *it;

                        std::string file_name = buffer;
                        while(drive_letter) {
                            drive_path[0] = drive_letter;

                            if (QueryDosDevice(drive_path.data(), buffer, MAX_PATH))
                            {
                                std::string_view view = buffer;
                                if(file_name.starts_with(view)) {
                                    return drive_path + file_name.substr(view.size());
                                }
                            } else {
                                printf("failed to query dos device for drive letter '%c': %s\n", drive_letter, get_last_error_message().c_str());
                                break;
                            }

                            //removes the drive name and the null termination
                            while (*it++);
                        }
                    } else {
                        printf("failed to get logical drives: '%s'\n", get_last_error_message().c_str());
                    }
                }
                else {
                    printf("failed to get get mapped file names: '%s'\n", get_last_error_message().c_str());
                }
            }
        }
    }

    return "unknow path";
}

DWORD64 load_module(HANDLE process, std::string_view path, DWORD64 base_addr)
{
    return SymLoadModuleEx(process, NULL, path.data(), NULL, base_addr, 0, 0, 0);
}

void basic_debugger::process_event()
{
    DWORD status = DBG_CONTINUE;

    switch (m_debug_event.dwDebugEventCode) 
    { 
        case CREATE_PROCESS_DEBUG_EVENT:
        {
            std::string process_name = GetFileNameFromHandle(m_debug_event.u.CreateProcessInfo.hFile);

            DWORD64 __module = load_module(m_process_info.hProcess, process_name, (DWORD64)m_debug_event.u.CreateProcessInfo.lpBaseOfImage);

            if(__module) {
                bool modules_has_been_loaded = load_module_info(__module, m_process_info);

                on_new_process(process_name, (addr_t)m_debug_event.u.CreateProcessInfo.lpStartAddress, modules_has_been_loaded);

            } else {
                on_new_process(process_name, (addr_t)m_debug_event.u.CreateProcessInfo.lpStartAddress, false);
            }
        
            break;
        }
        case LOAD_DLL_DEBUG_EVENT:
        {
            std::string dll_name = GetFileNameFromHandle(m_debug_event.u.LoadDll.hFile);
        
            DWORD64 __module = load_module(m_process_info.hProcess, dll_name.c_str(), (DWORD64)m_debug_event.u.LoadDll.lpBaseOfDll);

            if(__module) {
                bool modules_has_been_loaded = load_module_info(__module, m_process_info);

                on_loaded_dll(dll_name, (addr_t)m_debug_event.u.LoadDll.lpBaseOfDll, modules_has_been_loaded);
            } else {
                on_loaded_dll(dll_name, (addr_t)m_debug_event.u.LoadDll.lpBaseOfDll, false);
            }

            break;
        }
        case EXCEPTION_DEBUG_EVENT: 
            status = DBG_EXCEPTION_NOT_HANDLED;
            switch(m_debug_event.u.Exception.ExceptionRecord.ExceptionCode)
            {
                case EXCEPTION_BREAKPOINT: {
                    if(m_has_hit_entry_breakpoint) {
                        DWORD_PTR dwExceptionAddress = (DWORD_PTR)m_debug_event.u.Exception.ExceptionRecord.ExceptionAddress;

                        auto it = m_break_points.find(dwExceptionAddress);
                        status = DBG_CONTINUE;

                        if(it != m_break_points.end()) {
                            //user defined break point

                            replace_byte_at_address(it->second.byte, (void*)it->first);
                            go_back_instruction_pointer();

                            on_break_point(it->second);
                        }
                    } else {
                        m_has_hit_entry_breakpoint = true;
                        m_need_to_continue = true;
                        return;
                    }
                }
                break;
                case EXCEPTION_SINGLE_STEP:
                    status = DBG_CONTINUE;
                    if(m_debug_event.u.Exception.dwFirstChance) {
                        on_step();
                    }
                break;
            }
            break;
        break;
        case EXIT_PROCESS_DEBUG_EVENT:
        {
            on_exit_process((size_t)m_debug_event.u.ExitProcess.dwExitCode);
        }
        break;
        default:
        break;
    }

    ContinueDebugEvent(m_debug_event.dwProcessId, m_debug_event.dwThreadId, status);
}
bool basic_debugger::load_module_info(DWORD64 __module, PROCESS_INFORMATION &process_info)
{
    IMAGEHLP_MODULE64 module_info;
    module_info.SizeOfStruct = sizeof(module_info);
    BOOL bSuccess = SymGetModuleInfo64(process_info.hProcess, __module, &module_info);

    struct SymEnumSourceFilesContext {
        basic_debugger* self;
        HANDLE process;
        DWORD64 start_address;
        basic_debugger::source_file* last_file = nullptr;
    } context;

    context.self = this;
    context.process = process_info.hProcess;
    context.start_address = __module;

    if(bSuccess && module_info.SymType == SymPdb)
    {
        SymEnumSourceFiles(context.process, context.start_address, NULL, [](PSOURCEFILE source_file, PVOID user_context) -> BOOL {
            //----------------------------------------------------^
            // Can filter files, like "*.cpp"
            // Todo: let virtualize the mask

            SymEnumSourceFilesContext* context = (SymEnumSourceFilesContext*)user_context;

            SymEnumLines(context->process, context->start_address, NULL, source_file->FileName, [](PSRCCODEINFO line, PVOID user_context) -> BOOL {

                SymEnumSourceFilesContext* context = (SymEnumSourceFilesContext*)user_context;

                if(!context->last_file || context->last_file->source != line->FileName) {

                    auto& source_file = context->self->find_or_create_source_file(line->FileName, line->Obj);
                    context->last_file = & source_file;
                }

                context->last_file->lines.push_back({
                    line->LineNumber,
                    line->Address
                });
                return true;
            }, (void*)context);

            return true;
        }, &context);

        return true;
    }

    return false;
}

void basic_debugger::go_back_instruction_pointer()
{
    CONTEXT lcContext;
    lcContext.ContextFlags = CONTEXT_ALL;
    GetThreadContext(m_process_info.hThread, &lcContext);

#ifdef _M_IX86
    lcContext.Eip--;
#else
    lcContext.Rip--;
#endif

    SetThreadContext(m_process_info.hThread, &lcContext);
}

uint8_t basic_debugger::replace_byte_at_address(uint8_t replace, LPVOID address)
{
    uint8_t byte;
    SIZE_T bytes_read;

    ReadProcessMemory(m_process_info.hProcess, address, &byte, 1, &bytes_read);
    WriteProcessMemory(m_process_info.hProcess, address, &replace, 1, &bytes_read);

    return byte;
}
#endif

basic_debugger::basic_debugger()
{
#ifdef __UVA_WIN__
    ZeroMemory( &m_startup_info, sizeof(m_startup_info) ); 
    m_startup_info.cb = sizeof(m_startup_info); 

    ZeroMemory( &m_process_info, sizeof(m_process_info) );
#endif
}

basic_debugger::pid_t basic_debugger::create_debugee_process(const std::string &path)
{
    return basic_debugger::pid_t();
}

bool basic_debugger::attach_to_process(basic_debugger::pid_t __pid)
{
    return false;
}

bool basic_debugger::create_and_attach_debugee_process(const std::string &path)
{
#ifdef __UVA_WIN__
    if(!CreateProcess (path.c_str(), NULL, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL,NULL, &m_startup_info, &m_process_info )) {
        return false;
    }

    BOOL could_initialize_sym = SymInitialize(m_process_info.hProcess, NULL, false);

    if(!could_initialize_sym) {
        return false;
    }

    //the first run call only runs untill the entry breakpoint
    run();
#endif

    return false;
}

void basic_debugger::run_one()
{
#ifdef __UVA_WIN__
    if(m_need_to_continue) {
        ContinueDebugEvent(m_debug_event.dwProcessId, m_debug_event.dwThreadId, DBG_CONTINUE);
    }
    WaitForDebugEvent(&m_debug_event, 0); 
    process_event();
#endif
}

void basic_debugger::run()
{
#ifdef __UVA_WIN__
    bool had_initialized_break_point_on_enter_loop = m_has_hit_entry_breakpoint;
#endif
    while(1) {
#ifdef __UVA_WIN__
        if(!had_initialized_break_point_on_enter_loop && m_has_hit_entry_breakpoint) {
            break;
        }
        if(m_need_to_continue) {
            ContinueDebugEvent(m_debug_event.dwProcessId, m_debug_event.dwThreadId, DBG_CONTINUE);
        }
        WaitForDebugEvent(&m_debug_event, INFINITE);
        process_event();
#endif
    }
}

basic_debugger::addr_t basic_debugger::append_break_point(const std::string &file, size_t line)
{
#ifdef __UVA_WIN__
    
#endif
    auto* source_file = find_source_file(file);
    
    if(!source_file || !source_file->lines.size()) return 0;

    auto it = std::lower_bound(source_file->lines.begin(), source_file->lines.end(), line, [](const source_line& source, const size_t& line) {
        return source.line < line;
    });

    if(it == source_file->lines.end()) {
        return 0;
    }

    uint8_t byte = replace_byte_at_address(0xcc, (void*)it->address);

    m_break_points.insert({it->address, { *source_file, it->line, byte } });

    return it->address;
}

std::string uva::diagnostics::basic_debugger::append_break_point(uint64_t address)
{
    std::string path;
    for(const auto& file : m_source_files)
    {
        for(const auto& line : file.lines)
        {
            if(address == line.address) {
                std::string path;
                size_t to_reserve = file.source.size()+30;
                path.reserve(to_reserve);

                path = file.source;
                path.push_back(':');
                path += std::to_string(line.line);

                UVA_CHECK_RESERVED_BUFFER(path, to_reserve);

                break;
            }
        }
    }

    uint8_t byte = replace_byte_at_address(0xcc, (void*)address);
    m_break_points.insert({address, { {}, 0, byte } });

    return path;
}

basic_debugger::source_file *basic_debugger::find_source_file(const std::string &filename)
{
    for(auto& source_file : m_source_files) {
        if(source_file.source.ends_with(filename)) {
            return &source_file;
        }
    }

    return nullptr;
}

basic_debugger::source_file & basic_debugger::find_or_create_source_file(const std::string & filename, const std::string & obj)
{
    auto* source_file = find_source_file(filename);

    if(source_file) {
        return *source_file;
    }

    m_source_files.push_back({filename, obj});

    return m_source_files.back();
}

uint8_t uva::diagnostics::basic_debugger::read_char_at(uint64_t address)
{
    uint8_t byte;
#ifdef __UVA_WIN__
    SIZE_T bytes_read;
    ReadProcessMemory(m_process_info.hProcess, (void*)address, &byte, 1, &bytes_read);
#endif

    return byte;
}

class test_debugger : public basic_debugger
{
public:
    virtual void on_new_process(const std::string& path, addr_t address, bool symbols) override
    {
        printf("New process '%s' loaded at '%p'", path.c_str(), (void*)address);

        if(symbols) {
            puts(" with symbols.");
        } else {
            puts(".");
        }
    }

    virtual void on_loaded_dll(const std::string& path, addr_t address, bool symbols) override
    {
        printf("Load DLL '%s' at '%p'", path.c_str(), (void*)address);

        if(symbols) {
            puts(" with symbols.");
        } else {
            puts(".");
        }
    }

    virtual void on_break_point(const break_point& break_point) override
    {
        printf("got a breakpoint at %s:%zi\n", break_point.file.source.c_str(), break_point.line);
    }

    virtual void on_step() override
    {
        puts("got a step");
    }

    virtual void on_exit_process(size_t exit_code) override
    {
        printf("process exited with exit code: %x\n", (unsigned int)exit_code);
    }

};

int main()
{
    std::string process_name;
    puts("l filename         - load process at filename and attach to it.");
    puts("r                  - run the loaded process.");
    puts("ab file line       - add breakpoint at file, line.");
    puts("ab address         - add breakpoint at address.");
    puts("p [c|i|cs] address - print char, integer or c-string at address");

    test_debugger debugger;

    std::thread* thread = nullptr;

    std::condition_variable wait_variable;

    while(1) {
        std::string command;
        std::cin >> command;

        if(command == "l") {
            if(thread) {
                puts("error: currently debugging, stop first.");
                continue;
            }

            std::cin >> process_name;
            thread = new std::thread([&debugger, &process_name, &wait_variable](){
                debugger.create_and_attach_debugee_process(process_name);

                wait_variable.notify_one();

                std::mutex wait_mutex;

                std::unique_lock<std::mutex> ul(wait_mutex);
                wait_variable.wait(ul);

                puts("run signal received.");
                debugger.run();
            });

            std::mutex wait_mutex;
            std::unique_lock<std::mutex> ul(wait_mutex);
            wait_variable.wait(ul);

            puts("loaded signal received.");
        } else if(command == "r") {
            if(!thread) {
                puts("error: not currently debugging, load first.");
                continue;
            }

            wait_variable.notify_one();
        } else if(command == "ab") {
            std::string first_argument;
            size_t line;

            std::cin >> first_argument;

            if(first_argument.starts_with("0x")) {
                first_argument.erase(0, 2);

                uint64_t address = std::stoll(first_argument, nullptr, 16);
                std::string file = debugger.append_break_point(address);

                if(file.size()) {
                    printf("break point added at '%s'\n", file.c_str());
                    //todo show source
                } else {
                    printf("break point added at %p, but it may be not relevant to any line.\n", (void*)address);
                }
            } else if(isdigit(first_argument.front()))
            {
                uint64_t address = std::stoll(first_argument);
                std::string file = debugger.append_break_point(address);

                if(file.size()) {
                    printf("break point added at '%s'\n", file.c_str());
                    //todo show source
                } else {
                    printf("break point added at %p, but it may be not relevant to any line.\n", (void*)address);
                }
            } else {
                std::cin >> line;
                basic_debugger::addr_t break_addr = debugger.append_break_point(first_argument, line);

                if(break_addr) {
                    printf("break point added at %p\n", (void*)break_addr);
                    //todo show source
                } else {
                    printf("failed to put break point at %s:%zi: the file does not exists or does not have any breakable line.\n", first_argument.c_str(), line);
                }
            }

        } else if(command == "p") {
            std::string format;
            std::string __address;

            std::cin >> format;
            std::cin >> __address;

            uint64_t address;

            if(__address.starts_with("0x")) {
                __address.erase(0, 2);

                address = std::stoll(__address, nullptr, 16);
            } else {
                address = std::stoll(__address);
            }

            if(format == "c") {
                
                uint8_t c = debugger.read_char_at(address);
                printf("'%c' 0x%02hhx\n", c, (unsigned int)c);
            }
        }
        else if(command == "&&") {
            continue;
        }
    }

    return 0;
}

basic_debugger::source_file::source_file(const std::string &__source, const std::string &__object)
    : source(__source), object(__object)
{

}
