#include <iostream>
#include <string>
#include <cstdlib>
#include <memory>
#include <utility>

#include <boost/program_options.hpp>
#include <boost/asio.hpp>

#include "config.h"
#include "logic.h"

using namespace std;

namespace po = boost::program_options;
using boost::asio::ip::tcp;

const static Logger &l = Logger::get();

static unique_ptr<Logic> logic = nullptr;

class session
  : public std::enable_shared_from_this<session>
{

public:

    session(tcp::socket socket)
        : _socket(std::move(socket))
    {
    }

    void start()
    {
        auto self(shared_from_this());
        _socket.async_read_some(boost::asio::buffer(_data, _maxLen),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    const string payload(_data, length);
                    const auto rc = logic->parseRequest(payload);
                    boost::asio::write(_socket, boost::asio::buffer(to_string(rc) + "\n"));
                }
            });
    }

private:

    tcp::socket _socket;
    static constexpr int _maxLen = { 2048 };
    char _data[_maxLen];
};

class server
{

public:

  server(boost::asio::io_service& io_service, short port)
    : _acceptor(io_service, tcp::endpoint(tcp::v4(), port)),
      _socket(io_service)
  {
    do_accept();
  }

private:

    void do_accept()
    {

      _acceptor.async_accept(_socket,
          [this](boost::system::error_code ec)
          {
              if (!ec)
              {
                  std::make_shared<session>(std::move(_socket))->start();
              }

              do_accept();
          });

    }

    tcp::acceptor _acceptor;
    tcp::socket _socket;
};


int main(int argc, char** argv)
{
    int retval = -1;
    short port;
    std::chrono::seconds tokenTimeout;

    try {
        unsigned int timeout;
        po::options_description desc("usage: doorlockd");
        desc.add_options()
            ("help,h", "print help")
            ("tokentimeout,t", po::value<unsigned int>(&timeout)->required(), "tokentimeout in seconds")
            ("port,p", po::value<short>(&port)->default_value(DEFAULT_PORT), "Port");

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);

        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            retval = 0;
            goto out;
        }

        po::notify(vm);

        tokenTimeout = std::chrono::seconds(timeout>3 ? timeout-3 : timeout); // Epaper refresh takes ~3 seconds
    }
    catch(const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        goto out;
    }

    logic = unique_ptr<Logic>(new Logic(tokenTimeout));

    l(LogLevel::notice, "Starting doorlockd");

    try {
        boost::asio::io_service io_service;
        server s(io_service, port);
        io_service.run();

        retval = 0;
    }
    catch (const char* const &ex) {
        ostringstream str;
        str << "FATAL ERROR: " << ex;
        l(str, LogLevel::error);
        retval = -1;
        goto out;
    }

    retval = 0;

out:
    Door::get().lock();
    l(LogLevel::notice, "Doorlockd stopped");
    return retval;
}
