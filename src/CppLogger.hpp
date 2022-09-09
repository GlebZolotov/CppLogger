#pragma once

#include <string>
#include <map>
#include <thread>
#include "boundedbuffer.hpp"
#include <algorithm>
#include <regex>
#include <mutex>
#include <cstdio>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

namespace LogLevel {
    enum Level { UNKNOWN, TRACE, DEBUG, INFO, WARNING, ERROR, CRITICAL };
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
            return std::string("UNKNOWN");
        }
    }
    Level from_str(const std::string & a) {
        if (a == "TRACE") 
            return TRACE;
        if (a == "DEBUG")
            return DEBUG;
        if (a == "INFO")
            return INFO;
        if (a == "WARNING")
            return WARNING;
        if (a == "ERROR")
            return ERROR;
        if (a == "CRITICAL")
            return CRITICAL;
        return UNKNOWN;
    }
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
                if (found == std::string::npos) break;
                found_prev = found + 1;
                found = format.find('>', found);
                vars.emplace_back(format.substr(found_prev, found - found_prev), "");
                found_prev = found + 1;
            }
            for(int i = vars.size() - 1; i >= 0; i--) {
                char * find_env = std::getenv(vars[i].first.c_str());
                if (find_env == nullptr) continue;
                // Found env var
                simple_text[i] += std::string(find_env) + simple_text[i+1];
                vars.erase(vars.begin() + i);
                simple_text.erase(simple_text.begin() + i + 1);
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
        virtual std::pair<bool, std::string> init(Options settings) {}
        void work() {
            while(is_worked.load() || buffer.cur_count() > 0) {
                PrimitiveMsg * msg;
                buffer.pop_back(&msg, is_worked);
                if (!is_worked.load() && buffer.cur_count() == 0) return;
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

class HttpOutputter: public BaseOutputter {
    private:
        boost::asio::io_context io_context;
        boost::asio::io_service service;
        boost::asio::ip::tcp::endpoint ep;
        boost::asio::ip::tcp::socket sock;

        std::string host;
        std::string path_to_file;
        std::ofstream fout;

        boost::asio::streambuf request;

        bool is_success_last;
    protected:
        std::pair<bool, std::string> send_message() {
            std::pair<bool, std::string> res(false, "");
            boost::asio::write(sock, request);
            sock.shutdown(boost::asio::ip::tcp::socket::shutdown_send);

            // read for max 5s
            boost::asio::steady_timer timer(io_context, std::chrono::seconds(5));
            timer.async_wait([&](boost::system::error_code ec) { sock.cancel(); });

            std::string reply_buf;
            boost::system::error_code reply_ec;
            boost::asio::async_read(sock, boost::asio::dynamic_buffer(res.second, 2048),
                [&](boost::system::error_code ec, size_t) { timer.cancel(); reply_ec = ec; });

            io_context.run();

            if (!reply_ec || reply_ec == boost::asio::error::eof) {
                std::cout << "Response:\n" << res.second << std::endl;
                res.first = true;
            }
            return res;
        }
        void write_msg(const std::string & msg) override {
            if (is_success_last) {
                std::ostream request_stream(&request);

                request_stream << "POST / HTTP/1.1\r\n";
                request_stream << "Host: " << host << "\r\n";
                request_stream << "User-Agent: curl/7.68.0\r\n";
                request_stream << "Accept: */*\r\n";
                request_stream << "Content-Type: application/json\r\n";
                request_stream << "Content-Length: " << msg.length() << "\r\n";    
                request_stream << "Connection: close\r\n\r\n";  //NOTE THE Double line feed
                request_stream << msg;
            }
            std::pair<bool, std::string> resp = send_message();
            if (resp.first) {
                if (is_success_last) return;
                is_success_last = true;
                fout.close();
                std::ifstream fin(path_to_file);
                std::string new_msg;
                while(std::getline(fin, new_msg)) {
                    write_msg(new_msg);
                }
                fin.close();
                std::remove(path_to_file.c_str());
            } else {
                if (is_success_last) {
                    is_success_last = false;
                    fout = std::ofstream(path_to_file);
                } else {
                    fout << msg;
                }
            }
        }
    public:
        HttpOutputter(
            bounded_buffer<PrimitiveMsg *> & in_buf, 
            std::atomic<bool> & in_a, 
            ThrTempls &l
        ): BaseOutputter(in_buf, in_a, l), sock(service), is_success_last(true) {}
        std::pair<bool, std::string> init(Options settings) override {
            try{
                path_to_file = settings.path_to_file;
                boost::asio::ip::tcp::resolver resolver(service);
                boost::asio::ip::tcp::resolver::query query(settings.host, settings.port);
                boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
                ep = iter->endpoint();
                sock.connect(ep);
                return std::pair<bool, std::string>(true, "Connection init successfully!");
            } catch (std::exception& e) {
                return std::pair<bool, std::string>(false, e.what());
            }
        }
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