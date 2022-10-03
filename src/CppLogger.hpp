#pragma once

#include <string>
#include <map>
#include <thread>
#include "RingBuffer.hpp"
#include <algorithm>
#include <regex>
#include <mutex>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

namespace CppLogger{

namespace LogLevel {
    enum Level { UNKNOWN, TRACE, DEBUG, INFO, WARNING, ERROR, CRITICAL };
    std::string to_str(Level a);
    Level from_str(const std::string & a);
}

struct Options {
    std::string output_type;
    std::string host;
    std::string port;
    std::string path_to_file;
    std::string path_to_template;
    std::string level;
    size_t buf_size;
};

std::string get_cur_time();
std::string get_iso_time();
std::string get_uuid();

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

class Parser {
    private:
        std::vector<std::string> simple_text;
        std::vector< std::pair<std::string, std::string> > vars;
        bool is_true(const std::string & format) const;
    public:
        Parser(const std::string & f);
        Parser& operator=(const Parser&) = default;
        bool set_value(const std::string & name, const std::string & value);
        std::string get_string() const;
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

            p.set_value("_LOGGER_OPTIONS_UUID", get_uuid());

            return p.get_string(); 
        }
        ~StructMsg(){}
};

using ThrTempls = std::map<std::thread::id, std::unique_ptr<StructMsg> >;

class BaseOutputter {
    protected:
        ring_buffer<PrimitiveMsg *> & buffer;
        virtual void write_msg(const std::string&) = 0;
        std::atomic<bool> & is_worked;
        ThrTempls & log_templs;
    public:
        BaseOutputter(
            ring_buffer<PrimitiveMsg *> & in_buf, 
            std::atomic<bool> & in_a, 
            ThrTempls &l
        ): 
            buffer(in_buf), 
            is_worked(in_a),
            log_templs(l) {}
        virtual std::pair<bool, std::string> init(Options settings) { return std::pair<bool, std::string>(true, ""); }
        void work();
        virtual ~BaseOutputter() {}
};

class ConsoleOutputter: public BaseOutputter {
    protected:
        void write_msg(const std::string & msg) override { std::cout << msg + "\n"; }
    public:
        ConsoleOutputter(
            ring_buffer<PrimitiveMsg *> & in_buf, 
            std::atomic<bool> & in_a, 
            ThrTempls &l
        ): BaseOutputter(in_buf, in_a, l) {}
};

class HttpOutputter: public BaseOutputter {
    private:
        boost::asio::io_service service;
        boost::asio::io_context io_context;
        boost::asio::ip::tcp::endpoint ep;

        std::string host;
        std::string port;
        std::string path_to_file;
        std::ofstream fout;

        boost::asio::streambuf request;

        bool is_success_last;
    protected:
        bool read_with_timeout(boost::asio::ip::tcp::socket & s, std::string & res);
        std::pair<bool, std::string> send_message();
        void write_msg(const std::string & msg) override;
    public:
        HttpOutputter(
            ring_buffer<PrimitiveMsg *> & in_buf, 
            std::atomic<bool> & in_a, 
            ThrTempls &l
        ): BaseOutputter(in_buf, in_a, l), is_success_last(true) {}
        std::pair<bool, std::string> init(Options settings) override;
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
        ring_buffer< PrimitiveMsg * > buffer;
        std::atomic<bool> is_work;
        std::unique_ptr<BaseOutputter> out;
        StructMsg base_format;
        std::thread output_thr;
        std::mutex m;
        LogLevel::Level level;

        static size_t const default_capacity = 50;

        CppLogger(Options settings): 
            buffer(settings.buf_size), 
            is_work(true) 
        {
            std::cerr << "Create CppLogger instance" << std::endl;
            std::transform(settings.level.begin(), settings.level.end(), settings.level.begin(), ::toupper);
            level = LogLevel::from_str(settings.level);
            if (settings.output_type == "http")
                out = std::make_unique<HttpOutputter>(buffer, is_work, log_templs);
            else if (settings.output_type == "console") 
                out = std::make_unique<ConsoleOutputter>(buffer, is_work, log_templs); 

            std::pair<bool, std::string> res = out->init(settings);
            if (!res.first) {
                std::cerr << "Failed to init logger: " << res.second << std::endl;
                is_work.store(false);
                return;
            }
            output_thr = std::thread(&BaseOutputter::work, out.get());
            reg_thread();
        }
        ~CppLogger() { 
            std::cerr << "Wait 5s for destroying CppLogger" << std::endl;
            boost::this_thread::sleep_for(boost::chrono::seconds(5)); 
            is_work.store(false); 
            output_thr.join(); 
        }
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
        static CppLogger & get_logger(Options * settings = nullptr) {
            static CppLogger logger(*settings);
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
            if (level <= LogLevel::TRACE)
                buffer.push_front(new PrimitiveMsg(msg, LogLevel::TRACE));
        }
        void debug_msg(const std::string & msg) {
            if (level <= LogLevel::DEBUG)
                buffer.push_front(new PrimitiveMsg(msg, LogLevel::DEBUG));
        }
        void info_msg(const std::string & msg) {
            if (level <= LogLevel::INFO)
                buffer.push_front(new PrimitiveMsg(msg, LogLevel::INFO));
        }
        void warning_msg(const std::string & msg) {
            if (level <= LogLevel::WARNING)
                buffer.push_front(new PrimitiveMsg(msg, LogLevel::WARNING));
        }
        void error_msg(const std::string & msg) {
            if (level <= LogLevel::ERROR)
                buffer.push_front(new PrimitiveMsg(msg, LogLevel::ERROR));
        }
        void critical_msg(const std::string & msg) {
            if (level <= LogLevel::CRITICAL)
                buffer.push_front(new PrimitiveMsg(msg, LogLevel::CRITICAL));
        }
};
}