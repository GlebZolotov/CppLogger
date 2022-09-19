#include "CppLogger.hpp"

std::string CppLogger::LogLevel::to_str(LogLevel::Level a) {
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

CppLogger::LogLevel::Level CppLogger::LogLevel::from_str(const std::string & a) {
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

std::string CppLogger::get_cur_time() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%d-%m-%Y %H-%M-%S");
    return oss.str();
}

std::string CppLogger::get_iso_time() {
    auto timepoint = std::chrono::system_clock::now();
    auto coarse = std::chrono::system_clock::to_time_t(timepoint);
    auto fine = std::chrono::time_point_cast<std::chrono::milliseconds>(timepoint);

    char buffer[sizeof "9999-12-31T23:59:59.999Z"];
    std::snprintf(buffer + std::strftime(buffer, sizeof buffer - 4,
                                         "%FT%T.", std::localtime(&coarse)),
                  5, "%03luZ", fine.time_since_epoch().count() % 1000);
    return buffer;
}

bool CppLogger::Parser::is_true(const std::string & format) const {
    int res = 0;
    for(const char& c : format) {
        if (c == '<') res++;
        else if (c == '>') res--;

        if (res > 1 || res < 0) return false;
    }
    return res == 0;
}

CppLogger::Parser::Parser(const std::string & f) {
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

bool CppLogger::Parser::set_value(const std::string & name, const std::string & value) {
    ptrdiff_t pos = std::distance(vars.begin(), std::find_if(vars.begin(), vars.end(), [&name](const std::pair<std::string, std::string>& el){ return el.first == name; }));
    if (pos < static_cast<long int>(vars.size())) {
        vars[pos].second = value;
        return true;
    }
    return false;
}

std::string CppLogger::Parser::get_string() const {
    std::string res;
    for(size_t i = 0; i < vars.size(); i++) {
        res += simple_text[i] + vars[i].second;
    }
    res += simple_text[vars.size()];
    return res;
}

void CppLogger::BaseOutputter::work() {
    while(is_worked.load() || buffer.cur_count() > 0) {
        PrimitiveMsg * msg;
        buffer.pop_back(&msg, is_worked);
        if (!is_worked.load() && buffer.cur_count() == 0) return;
        write_msg(log_templs[msg->get_thr()]->serialize(msg));
        delete msg;
    }
}

bool CppLogger::HttpOutputter::read_with_timeout(boost::asio::ip::tcp::socket & s, std::string & res) {
    // read for max 5s
    boost::asio::steady_timer timer(io_context, std::chrono::seconds(5));
    timer.async_wait([&](boost::system::error_code ec) { s.cancel(); });
    
    std::string reply_buf;
    boost::system::error_code reply_ec;
    boost::asio::async_read(s, boost::asio::dynamic_buffer(res, 2048), 
        [&](boost::system::error_code ec, size_t size) { timer.cancel(); reply_ec = ec; });

    io_context.run();

    if (!reply_ec || reply_ec == boost::asio::error::eof) {
        std::string status_code = res.substr(res.find(" ") + 1, 3);
        std::string body = res.substr(res.find("\r\n\r\n") + 4);
        res = get_cur_time() + " - " + status_code + " " + body;
        if (status_code == "500")
            return false;
        else 
            return true;
    } else {
        res = get_cur_time() + " - Timeout";
        return false;
    } 
}
std::pair<bool, std::string> CppLogger::HttpOutputter::send_message() {
    std::pair<bool, std::string> res(false, "");
    boost::asio::ip::tcp::socket sock(io_context);
    sock.connect(ep);
    boost::asio::write(sock, request);
    //sock.shutdown(boost::asio::ip::tcp::socket::shutdown_send);
    res.first = read_with_timeout(sock, res.second);
    io_context.reset();
    return res;
}

void CppLogger::HttpOutputter::write_msg(const std::string & msg) {
    if (is_success_last) {
        std::ostream request_stream(&request);

        request_stream << "POST / HTTP/1.1\r\n";
        request_stream << "Host: " << host << ":" << port << "\r\n";
        request_stream << "User-Agent: curl/7.68.0\r\n";
        request_stream << "Accept: */*\r\n";
        request_stream << "Content-Type: application/json\r\n";
        request_stream << "Content-Length: " << msg.length() << "\r\n";    
        request_stream << "Connection: close\r\n\r\n";  //NOTE THE Double line feed
        request_stream << msg;
    }
    std::pair<bool, std::string> resp = send_message();
    std::cout << resp.second << "\n";
    if (resp.first) {
        if (is_success_last) return;
        is_success_last = true;
        fout.close();
        std::ifstream fin(path_to_file);
        std::string new_msg;
        std::cout << get_cur_time() + " - Write file to server\n";
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
            std::cout << get_cur_time() + " - Put message to file\n";
            fout << msg;
        }
    }
}

std::pair<bool, std::string> CppLogger::HttpOutputter::init(Options settings) {
    try{
        path_to_file = settings.path_to_file;
        host = settings.host;
        port = settings.port;
        boost::asio::ip::tcp::resolver resolver(service);
        boost::asio::ip::tcp::resolver::query query(host, port);
        boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
        ep = iter->endpoint();
        return std::pair<bool, std::string>(true, "Connection init successfully!");
    } catch (std::exception& e) {
        return std::pair<bool, std::string>(false, e.what());
    }
}