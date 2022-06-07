#pragma once

#include <string>
#include <map>
#include <thread>
#include "boundedbuffer.hpp"
#include <algorithm>
#include <regex>
#include <mutex>

namespace LogLevel {
    enum Level { TRACE, DEBUG, INFO, WARNING, ERROR, CRITICAL };
    std::string to_str(Level a) {
        switch (a) {
        case TRACE:
            return std::string("TRACE");
        case DEBUG:
            return std::string("DEBUG");
        case INFO:
            return std::string("INFO");
        case WARNING:
            return std::string("WARNING");
        case ERROR:
            return std::string("ERROR");
        case CRITICAL:
            return std::string("CRITICAL");
        default:
            return std::string("IT IS NON_FULL SWITCH!!!");
        }
    }
}

class PrimitiveMsg {
    private:
        std::string msg;
        LogLevel::Level log_level;
        std::thread::id thr_id;
        std::time_t t;
    public:
        PrimitiveMsg(const std::string & rhs, LogLevel::Level l): 
            msg(rhs), 
            log_level(l), 
            thr_id(std::this_thread::get_id()), 
            t(std::time(0)) {}
        PrimitiveMsg(PrimitiveMsg && rhs): 
            msg(rhs.msg), 
            log_level(rhs.log_level), 
            thr_id(rhs.thr_id), 
            t(rhs.t) {}
        PrimitiveMsg(const PrimitiveMsg&) = delete;
        const std::string & get_msg() { return msg; }
        LogLevel::Level get_level() const { return log_level; }
        std::thread::id get_thr() const { return thr_id; }
        std::time_t get_time() const { return t; }
        ~PrimitiveMsg() {}
};

std::string get_iso_time() {
    auto timepoint = std::chrono::system_clock::now();
    auto coarse = std::chrono::system_clock::to_time_t(timepoint);
    auto fine = std::chrono::time_point_cast<std::chrono::milliseconds>(timepoint);

    char buffer[sizeof "9999-12-31T23:59:59.999Z"];
    std::snprintf(buffer + std::strftime(buffer, sizeof buffer - 4,
                                         "%FT%T.", std::localtime(&coarse)),
                  5, "%03luZ", fine.time_since_epoch().count() % 1000);
    return buffer;
}

class Parser {
    private:
        std::vector<std::string> simple_text;
        std::vector< std::pair<std::string, std::string> > vars;
        bool is_true(const std::string & format) const {
            int res = 0;
            for(const char& c : format) {
                if (c == '<') res++;
                else if (c == '>') res--;

                if (res > 1 || res < 0) return false;
            }
            return res == 0;
        }
    public:
        Parser(const std::string & f) {
            std::string format = std::regex_replace(f, std::regex("\\s+"), ""); 
            if (!is_true(format)) throw std::string("Bad format: bad parentheses sequence");
            std::size_t found = 0;
            std::size_t found_prev = 0;
            while (found != std::string::npos) {
                found = format.find('<', found);
                simple_text.emplace_back(format.substr(found_prev, found - found_prev));
                if (found == std::string::npos) return;
                found_prev = found + 1;
                found = format.find('>', found);
                vars.emplace_back(format.substr(found_prev, found - found_prev), "");
                found_prev = found + 1;
            }
        }
        Parser& operator=(const Parser&) = default;
        bool set_value(const std::string & name, const std::string & value) {
            ptrdiff_t pos = std::distance(vars.begin(), std::find_if(vars.begin(), vars.end(), [&name](const std::pair<std::string, std::string>& el){ return el.first == name; }));
            if (pos < static_cast<long int>(vars.size())) {
                vars[pos].second = value;
                return true;
            }
            return false;
        }
        std::string get_string() const {
            std::string res;
            for(size_t i = 0; i < vars.size(); i++) {
                res += simple_text[i] + vars[i].second;
            }
            res += simple_text[vars.size()];
            return res;
        }
        ~Parser() {};
};

class StructMsg {
    private:
        Parser p;
    public:
        StructMsg(std::string format = ""): p(format) {}
        void set_format(const std::string &f) {
            p = Parser(f);
        }
        void set_format(StructMsg &f) {
            p = f.p;
        }
        
        bool set_value(const std::string & name, const std::string & value){ 
            return p.set_value(name, value); 
        }
        std::string serialize(PrimitiveMsg * msg) {
            p.set_value("MSG", msg->get_msg());
            p.set_value("LEVEL", LogLevel::to_str(msg->get_level()));
            p.set_value("TIME", std::to_string(msg->get_time()));

            return p.get_string(); 
        }
        ~StructMsg(){}
};

using ThrTempls = std::map<std::thread::id, std::unique_ptr<StructMsg> >;

class BaseOutputter {
    protected:
        bounded_buffer<PrimitiveMsg *> & buffer;
        virtual void write_msg(const std::string&) = 0;
        std::atomic<bool> & is_worked;
        ThrTempls & log_templs;
    public:
        BaseOutputter(
            bounded_buffer<PrimitiveMsg *> & in_buf, 
            std::atomic<bool> & in_a, 
            ThrTempls &l
        ): 
            buffer(in_buf), 
            is_worked(in_a),
            log_templs(l) {}
        void work() {
            while(is_worked.load()) {
                PrimitiveMsg * msg;
                buffer.pop_back(&msg);
                write_msg(log_templs[msg->get_thr()]->serialize(msg));
                delete msg;
            }
        }
        virtual ~BaseOutputter() {}
};

class ConsoleOutputter: public BaseOutputter {
    protected:
        void write_msg(const std::string & msg) override { std::cout << msg + "\n"; }
    public:
        ConsoleOutputter(
            bounded_buffer<PrimitiveMsg *> & in_buf, 
            std::atomic<bool> & in_a, 
            ThrTempls &l
        ): BaseOutputter(in_buf, in_a, l) {}
};

// class ThreadData {
//     private:
//         std::list<PrimitiveMsg> queue;
//         StructMsg thr_templ;
//     public:
//         ThreadData(const StructMsg & thr): thr_templ(thr) {}
//         void put_msg(PrimitiveMsg && rhs) { queue.emplace_back(rhs); }

// };

class CppLogger {
    private:
        ThrTempls log_templs;
        bounded_buffer< PrimitiveMsg * > buffer;
        std::atomic<bool> is_work;
        std::unique_ptr<BaseOutputter> out;
        StructMsg base_format;
        std::thread output_thr;
        std::mutex m;

        static size_t const default_capacity = 50;

        CppLogger(): 
            buffer(default_capacity), 
            is_work(true), 
            out(std::make_unique<ConsoleOutputter>(buffer, is_work, log_templs)), 
            output_thr(&BaseOutputter::work, out.get()) 
        {
            std::cout << "Create CppLogger instance from thread " << std::this_thread::get_id() << std::endl;
            reg_thread();
        }
        ~CppLogger() { output_thr.join(); }
        ThrTempls::iterator find_and_insert_thr(std::thread::id thr_id) {
            ThrTempls::iterator lb = log_templs.lower_bound(thr_id);
            if(lb == log_templs.end() || log_templs.key_comp()(thr_id, lb->first)) {
                lb = log_templs.insert(lb, ThrTempls::value_type(thr_id, std::make_unique<StructMsg>()));
                lb->second->set_format(base_format);
            }
            return lb;
        }

        void set_base_format_to_all() {
            for(auto & el: log_templs) 
                el.second->set_format(base_format);
        }
    public:
        static CppLogger & get_logger() {
            static CppLogger logger;
            return logger;
        }
        CppLogger(const CppLogger&) = delete;
        CppLogger & operator=(const CppLogger&) = delete;

        std::pair<bool, std::string> set_base_format(const std::string &f) { 
            std::lock_guard<std::mutex> lock(m);
            try {
                base_format.set_format(f); 
            } catch (std::string s) {
                return std::pair<bool, std::string>(false, s);
            }
            set_base_format_to_all();
            return std::pair<bool, std::string>(true, "");
        }

        bool set_glob_value(std::string name, std::string value) { 
            std::lock_guard<std::mutex> lock(m);
            bool res = base_format.set_value(name, value);
            if (!res) return res;
            for(auto & el: log_templs) 
                el.second->set_value(name, value);
            return res; 
        }
        void reg_thread() { 
            std::lock_guard<std::mutex> lock(m);
            find_and_insert_thr(std::this_thread::get_id());
        }
        bool set_thr_value(std::string name, std::string value) {
            std::lock_guard<std::mutex> lock(m); 
            ThrTempls::iterator lb = find_and_insert_thr(std::this_thread::get_id());
            return lb->second->set_value(name, value);
        }

        void trace_msg(const std::string & msg) {
            buffer.push_front(new PrimitiveMsg(msg, LogLevel::TRACE));
        }
        void debug_msg(const std::string & msg) {
            buffer.push_front(new PrimitiveMsg(msg, LogLevel::DEBUG));
        }
        void info_msg(const std::string & msg) {
            buffer.push_front(new PrimitiveMsg(msg, LogLevel::INFO));
        }
        void warning_msg(const std::string & msg) {
            buffer.push_front(new PrimitiveMsg(msg, LogLevel::WARNING));
        }
        void error_msg(const std::string & msg) {
            buffer.push_front(new PrimitiveMsg(msg, LogLevel::ERROR));
        }
        void critical_msg(const std::string & msg) {
            buffer.push_front(new PrimitiveMsg(msg, LogLevel::CRITICAL));
        }
};