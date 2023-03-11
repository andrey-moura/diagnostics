#include <application_controller.hpp>

#include <iostream>

#include <diagnostics.hpp>
#include <basic_debugger.hpp>

#include <console.hpp>
#include <file.hpp>
#include <core.hpp>

using namespace diagnostics;

struct line_hits {
    size_t line;
    size_t hits;
};

struct file_hits {
    std::string path;
    std::map<size_t, line_hits> hits;

    void add_hit(size_t line)
    {   
        auto it = hits.find(line);

        if(it == hits.end()) {
            hits.insert({line, { line, 1 }});
        } else {
            it->second.hits++;
        }
    }
};

class ccov_debugger : public basic_debugger 
{
public:
    ccov_debugger();
public:
    std::map<std::string, file_hits> m_file_hits;
protected:
    virtual void on_execution_started() override;
    virtual void on_step() override;
};


ccov_debugger::ccov_debugger()
    : basic_debugger()
{
    
}

void ccov_debugger::on_execution_started()
{

}

void ccov_debugger::on_step()
{
    std::string file;
    size_t line;

    if(get_current_line_and_source(file, line)) {
        auto it = m_file_hits.find(file);

        if(it == m_file_hits.end()) {
            auto insert = m_file_hits.insert({file, { }});
            insert.first->second.add_hit(line);
        } else {
            it->second.add_hit(line);
        }
    }

    std::cout << file << ":" << line << std::endl;
}

void application_controller::run()
{
    std::string debugee_path = params[0].to_s();

    ccov_debugger debugger;
    if(!debugger.create_and_attach_debugee_process(debugee_path)) {
        log_error(debugger.get_last_error_msg());
    }

    debugger.run();
}
