#pragma once

#include <string>
#include <map>
#include <thread>
#include "boundedbuffer.hpp"
#include <algorithm>

namespace LogLevel {
    enum Level { DEBUG, INFO, WARNING, ERROR, CRITICAL };
    std::string to_str(Level a) {
        switch (a) {
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
};

class PrimitiveMsg {
    private:
        std::string msg;
        LogLevel::Level log_level;
        std::thread::id thr_id;
        std::time_t t;
    public:
        PrimitiveMsg(std::string && rhs, LogLevel::Level l): 
            msg(rhs), 
            log_level(l), 
            thr_id(std::this_thread::get_id()), 
            t(std::time(0)) {}
        PrimitiveMsg(PrimitiveMsg && rhs): 
            msg(std::move(rhs.msg)), 
            log_level(rhs.log_level), 
            thr_id(rhs.thr_id), 
            t(rhs.t) {}
        std::string & get_msg() { return msg; }
        LogLevel::Level get_level() { return log_level; }
        std::thread::id get_thr() { return thr_id; }
        std::time_t get_time() { return t; }
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
};

class Parser {
    private:
        std::vector<std::string> simple_text;
        std::vector< std::pair<std::string, std::string> > vars;
    public:
        Parser(const std::string & format) {
            std::size_t found = 0;
            std::size_t found_prev = 0;
            while (found != std::string::npos) {
                found = format.find('<', found);
                if (found == std::string::npos) continue;
                simple_text.emplace_back(format.substr(found_prev, found - found_prev));
                found_prev = found + 1;
                found = format.find('>', found);
                if (found == std::string::npos) throw "Bad format: no '>' for '<' in " + std::to_string(found_prev - 1) + " pos";
                vars.emplace_back(format.substr(found_prev, found - found_prev), "");
                found_prev = found + 1;
            }
        }
        void set_value(const std::string & name, const std::string & value) {
            ptrdiff_t pos = std::distance(std::find_if(vars.begin(), vars.end(), [&name](const std::pair<std::string, std::string>& el){ return el.first == name; }), vars.begin());
            if (pos < vars.size()) vars[pos].second = value;
        }
        std::string get_string() {
            std::string res;
            for(size_t i = 0; i < vars.size(); i++) {
                res += simple_text[i] + vars[i].second;
            }
            if (simple_text.size() > vars.size()) res += simple_text[vars.size()];
            return res;
        }
        ~Parser() {};
};

class StructMsg {
    private:
        Parser p;
    public:
        StructMsg(std::string format): p(format) {}
        StructMsg(const StructMsg & in) = default;

        void set_value(const std::string & name, const std::string & value){ p.set_value(name, value); }
        std::string serialize(PrimitiveMsg * msg) {
            set_value("msg", msg->get_msg());
            set_value("level", LogLevel::to_str(msg->get_level()));
            set_value("time", std::to_string(msg->get_time()));

            return p.get_string(); 
        }
        ~StructMsg(){}
};

class BaseOutputter {
    protected:
        bounded_buffer<PrimitiveMsg *> & buffer;
        virtual void write_msg(std::string) = 0;
        std::atomic<bool> & is_worked;
        std::map<std::thread::id, StructMsg> & log_templs;
    public:
        BaseOutputter(
            bounded_buffer<PrimitiveMsg *> & in_buf, 
            std::atomic<bool> & in_a, 
            std::map<std::thread::id, StructMsg> &l
        ): 
            buffer(in_buf), 
            is_worked(in_a),
            log_templs(l) {}
        void work() {
            while(is_worked.load()) {
                PrimitiveMsg * msg;
                buffer.pop_back(&msg);
                write_msg(log_templs[msg->get_thr()].serialize(msg));
                delete msg;
            }
        }
};

class ConsoleOutputter: public BaseOutputter {
    protected:
        void write_msg(std::string msg) override { std::cout << msg + "\n"; }
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
        std::map<std::thread::id, StructMsg> log_templs;
        bounded_buffer< PrimitiveMsg * > buffer;
        std::unique_ptr<BaseOutputter> out;
        StructMsg base_format;
    public:
        CppLogger(std::string format, std::string outputter);
        void info_msg(std::string && rhs);
        void put_msg(StructMsg);
};