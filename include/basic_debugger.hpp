#include <string>
#include <map>

#include <uva.hpp>

#ifdef __UVA_WIN__
    #include <windows.h>
    #include <DbgHelp.h>
    #include <Psapi.h>
#endif

namespace uva
{
    namespace diagnostics
    {
        class basic_debugger
        {
#ifdef __UVA_WIN__
        private:
            /// @brief On Windows, once WaitForDebugEvent is called, it will generate a first event wich need to be ignored.
            bool m_has_hit_entry_breakpoint = false;
            bool m_need_to_continue = false;
            PROCESS_INFORMATION m_process_info;
            STARTUPINFO m_startup_info;
            DEBUG_EVENT m_debug_event = { 0 };

            /// @brief Windows: process the DEBUG_EVENT which has been fed by run/run_one
            void process_event();
            /// @brief Windows: load file names and lines from module
            /// @param __module The module to load info
            /// @param process_info The process from which the module was loaded
            /// @param start_address The start address from the process
            /// @return True on success, otherwise false.
            bool load_module_info(DWORD64 __module, PROCESS_INFORMATION& process_info);
            /// @brief Moves back the Eip one byte
            void go_back_instruction_pointer();
            /// @brief Replace memmory
            /// @param replace The byte to be placed in the memmory
            /// @param addr The address where byte will be placed
            /// @return The old byte which has been replaced by replace
            uint8_t replace_byte_at_address(uint8_t replace, LPVOID addr);
        public:
            using pid_t  = HANDLE;
            using addr_t = DWORD_PTR;
#else
#endif
        public:
            struct source_line {
                size_t line;
                addr_t address;
            };
            struct source_file
            {
                source_file() = default;
                source_file(const std::string& __source, const std::string& __object) ;

                std::string source;
                std::string object;

                std::vector<source_line> lines;
            };
            struct break_point
            {
                source_file file;
                size_t line;
                uint8_t byte;
            };
        public:
            /// @brief The debugger is initialized without knowing the debugee. 
            basic_debugger();
        public:
            /// @brief Creates the debugee process withoutt attaching to it.
            /// @param path The path to the executable which will be started.
            /// @return The platform dependent process id
            pid_t create_debugee_process(const std::string& path);
            /// @brief Attach to a process created with create_debugee_process
            /// @param __pid The platform dependent process id
            /// @return True if the process was started successfully, false otherwise.
            bool attach_to_process(pid_t __pid);
            /// @brief Creates the debugee process and then attach to it
            /// @param path The path to the executable which will be started.
            /// @return if create_debugee_process and attach_to_process succeed, true is returned, otherwise false.
            bool create_and_attach_debugee_process(const std::string& path);
            /// @brief Get last error message from the OS
            /// @return the platform dependent error message
            std::string get_last_error_msg();
            /// @brief Checks if there's debugger events, call callbacks and then returns.
            void run_one();
            /// @brief Run on this thread untill the debugging session is finished.
            void run();
            /// @brief Add a breakpoint to a specific file and line
            /// @param file The file where to put the breakpoint
            /// @param line The line where to put breakpoint in file
            /// @return The address of the breakpoint
            addr_t append_break_point(const std::string& file, size_t line);
            /// @brief Add a breakpoint to a specific address
            /// @param file The address where to put the breakpoint
            /// @param line The line where to put breakpoint in file
            /// @return The file and line of the breakpoint
            std::string append_break_point(uint64_t addr);
            /// @brief Find an source file
            /// @param filename The path to search for
            /// @return The found source file or nullptr
            source_file* find_source_file(const std::string& filename);
            /// @brief Find or create an source file
            /// @param filename The path to search for
            /// @param obj The path to the object
            /// @return The found or created source file
            source_file& find_or_create_source_file(const std::string& filename, const std::string& obj);
            /// @brief Read char from memmory
            /// @param addr The address of the char
            /// @return The char at address
            uint8_t read_char_at(uint64_t addr);

            void set_trap_flag();

            bool get_current_line_and_source(std::string& source, size_t line);
        public:
            /// @brief The source line mapped by address. Public for easy access from platform specific code. It is internal.
            //std::map<addr_t, source_file> m_source_files;

            std::vector<source_file> m_source_files;
        public:
            addr_t current_address;
        protected:
            virtual void on_new_process(const std::string& path, addr_t address, bool symbols)
            {

            }
            virtual void on_loaded_dll(const std::string& path, addr_t address, bool symbols)
            {

            }
            virtual void on_execution_started()
            {
                
            }
            virtual void on_break_point(const break_point& break_point)
            {

            }
            virtual void on_step()
            {
                
            }
            virtual void on_exit_process(size_t exit_code)
            {

            }
        protected:
            /// @brief The path of the process or application that is beign debugged.
            std::string m_debugee_path;
            /// @brief The break points mapped by address
            std::map<addr_t, break_point> m_break_points;
        };// class debugger
    }; // namespace diagnostics
}; // namespace uva