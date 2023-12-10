#include <application_controller.hpp>

#include <iostream>

#include <diagnostics.hpp>
#include <basic_debugger.hpp>

#include <console.hpp>
#include <file.hpp>
#include <core.hpp>
#include <json.hpp>

using namespace diagnostics;

class ccov_debugger : public basic_debugger 
{
public:
    ccov_debugger(const std::string& project_path, std::vector<std::string> interesting_files);
public:
    var m_coverage_info = var::map();
    std::vector<std::string> m_interesting_files;
    std::map<std::string, var*> m_covered_files;
private:
    std::string m_project_path;
    bool m_is_covering = false;
protected:
    var none = null;
    var& find_relevant_file(const std::string& source);
    var& find_line_on_file(var& file, int number);
    std::string to_relative_path(const std::string& path);
    virtual void on_execution_started() override;
    virtual void on_break_point(const break_point& break_point) override;
    virtual void on_debug_string(std::string s) override;
    virtual void on_step() override;
    virtual void on_exit_process(size_t exit_code) override;
};


ccov_debugger::ccov_debugger(const std::string& project_path, std::vector<std::string> interesting_files)
    : basic_debugger(), m_project_path(project_path), m_interesting_files(std::move(interesting_files))
{
    m_coverage_info["run"] = time(NULL);
    m_coverage_info["hits"] = var::map();

    var& files = m_coverage_info["files"] = var::array();
    
    //using an array of files instead of a map will slow down a little bit, but since an application will not have like millions of file,
    //the difference will be unnoticeable and the simplicity will worth it.
    for(size_t i = 0; i < m_interesting_files.size(); ++i) 
    {
        files.push_back(var::map({
            //name of source file
            { "path",               m_interesting_files[i] },
            { "relativePath",       to_relative_path(m_interesting_files[i]) },
            //array of line objects
            { "lines",              var::array()           },
            //total lines on the file
            { "totalLines",         0                      },
            //only the relevant lines
            { "totalRelevantLines", 0                      },
            //how many lines have been hit
            { "relevantLinesHit",       0                      },
            { "avarageHitsPerLine",  0                      },
            //sum of hits on all lines
            { "totalHits",          0                      },
            { "lastHit",          0                      },
            
            //percentage of how many relevant lines have been hit against the total of relevant lines 
            { "coverage",           0                      },
        }));

        m_covered_files.insert({ m_interesting_files[i], &files.back() });
    }
}

var &ccov_debugger::find_relevant_file(const std::string &source)
{
    auto it = m_covered_files.find(source);

    if(it == m_covered_files.end()) {
        return none;
    }

    return *(it->second);
}

var &ccov_debugger::find_line_on_file(var &file, int number)
{
    var& lines = file["lines"];
    for(size_t i = 0; i < lines.size(); ++i) {
        var& line = lines[i];

        if(line["number"] == number) {
            return line;
        }
    }

    return none;
}

std::string ccov_debugger::to_relative_path(const std::string &__path)
{
    std::string path;
    if(__path.starts_with(m_project_path)) {
        path = __path.substr(m_project_path.size());

        if(path.starts_with('/') || path.starts_with('\\')) {
            path.erase(0, 1);
        }
    } else {
        path = __path;
    }

    return path;
}

void ccov_debugger::on_execution_started()
{
    for(const auto& source : m_source_files) {
        var& file = find_relevant_file(source.source);
        if(file != none) {
            file["totalRelevantLines"] = source.lines.size();
            var& lines = file["lines"];

            std::string content = uva::file::read_all_text<char>(source.source);
            std::string_view content_view = content;

            if(content.size()) {

                size_t line_num = 1;
                std::string line;

                while(content_view.size()) {
                    const char& c = content_view.front();

                    if(c == '\n') {
                        if(line.ends_with('\r')) {
                            line.pop_back();
                        }

                        auto it = std::lower_bound(source.lines.begin(), source.lines.end(), line_num, [](const source_line& source, const size_t& line) {
                            return source.line < line;
                        });

                        bool is_relevant = it != source.lines.end() && it->line == line_num; 

                        lines.push_back(var::map({
                            { "number", line_num },
                            { "text", line },
                            { "hits", 0 },
                            { "isRelevant", is_relevant }
                        }));

                        if(is_relevant) {
                            append_break_point(source.source, it->line);
                        }

                        line_num++;
                        line.clear();
                    } else {
                        line.push_back(c);
                    }

                    content_view.remove_prefix(1);
                }

                // for(const auto& line : source.lines) {
                //     lines.push_back(var::map({
                //         { "number", line.line },
                //         { "text", null },
                //         { "hits", 0 },
                //         { "isRelevant", true }
                //     }));
                //     append_break_point(source.source, line.line);
                // }
            }
        }
    }
}

void ccov_debugger::on_break_point(const break_point &break_point)
{
    std::string source;
    size_t line_number = 0;

    if(get_current_line_and_source(source, line_number)) {
        var& file = find_relevant_file(source);

        if(file != none) {
            var& line = find_line_on_file(file, line_number);
            
            if(line != none) {
                var& hits = line["hits"];
                ++hits;

                line["lastHit"] = time(NULL);

                file["lastHit"] = time(NULL);
                ++file["totalHits"];

                //first hit on this line
                if(hits == 1) {
                    var& relevantLinesHit = file["relevantLinesHit"];
                    ++relevantLinesHit;

                    //as we are here already...
                    file["coverage"] = (relevantLinesHit.to_f() / file["totalRelevantLines"].to_f()) * 100.0;
                }
            }
        }
    }
}

void ccov_debugger::on_debug_string(std::string s)
{
    // if(s == "begin coverage") {
    //     m_is_covering = true;
    //     set_trap_flag();
    // } else if(s == "end coverage") {
    //     m_is_covering = false;
    // }
}

void ccov_debugger::on_step()
{
    // std::string file;
    // size_t line = 0;

    // if(get_current_line_and_source(file, line)) {
    //     auto it = m_coverage_info.find(file);

    //     if(it == m_coverage_info.end()) {
    //         auto insert = m_coverage_info.insert({file, { }});
    //         insert.first->second.add_hit(line);
    //     } else {
    //         it->second.add_hit(line);
    //     }

    //     std::cout << file << ":" << line << std::endl;
    // } else {
    //     //log_error(get_last_error_msg());
    // }

    // if(m_is_covering) {
    //     set_trap_flag();
    // }
}

void ccov_debugger::on_exit_process(size_t exit_code)
{
    //for(var& file : m_coverage_info["files"]) {

    //}

    std::string coverage_info = uva::json::enconde(m_coverage_info, true);
    uva::file::write_all_text("ccov-info.json", coverage_info);

    std::filesystem::path template_path = std::filesystem::absolute("coverage") / "index.template.html";
    std::string __template = uva::file::read_all_text<char>(template_path);

    if(!std::filesystem::exists(template_path)) {
        log_error("coverage/template.html not found, skiping index.html output.");
        return;
    }

    const std::string coverage_info_tag = "m_coverage_info";
    size_t coverage_info_index = __template.find(coverage_info_tag);

    if(coverage_info_index == std::string::npos) {
        log_error("coverage/template.html do not have the m_coverage_info, skiping index.html output.");
        return;
    }

    std::string right = __template.substr(coverage_info_index+coverage_info_tag.size());

    //len needed to everything on left + coverage info
    size_t left_len = coverage_info.size()+coverage_info_index;

    __template.resize(left_len);

    memcpy(__template.data()+coverage_info_index, coverage_info.data(), coverage_info.size());

    __template += right;

    uva::file::write_all_text("coverage/index.html", __template);

    exit(0);
}

void application_controller::run()
{
    std::string debugee_path = "uva-core-spec.exe";
    //std::string debugee_path = params[0].to_s();

    std::ifstream source_files("ccov-files.txt", std::ios::in);

    std::vector<std::string> sources;
    std::string source;
    std::string project_path;

    while(std::getline(source_files, source))
    {
        if(project_path.empty()) {
            project_path = source;
        } else {
            sources.push_back(std::move(source));
        }
    }

    ccov_debugger debugger(project_path, std::move(sources));

    if(!debugger.create_and_attach_debugee_process(debugee_path)) {
        log_error(debugger.get_last_error_msg());
        return;
    }

    debugger.run();
}
