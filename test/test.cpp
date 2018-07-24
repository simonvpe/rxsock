#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <rxcpp/rx.hpp>
#include <iostream>
#include <stdexcept>

extern "C" {
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <errno.h>
#  include <string.h>
}

class connection_type {
public:
  connection_type() = delete;
  connection_type(const connection_type&) = delete;

  connection_type(int listen_fd)
    : client_addr_len{sizeof(client_addr)}
    , connection_fd{-1}
    , listen_fd{listen_fd}
  {}

  connection_type(connection_type&& other)
    : client_addr{std::move(other.client_addr)}
    , client_addr_len{std::move(other.client_addr_len)}
    , connection_fd{std::move(other.connection_fd)}
    , listen_fd{std::move(other.listen_fd)}
  {}

  ~connection_type()
  {
    if(connection_fd >= 0) {
      ::close(connection_fd);
      std::cerr << "Connection " << connection_fd << " closed!\n";
    }
  }

  void accept()
  {
    connection_fd = ::accept(listen_fd, &client_addr, &client_addr_len);
    if(connection_fd == -1){
      throw std::runtime_error(strerror(errno));
    }
  }

  void write(std::vector<char>&& data)
  {
    if(::write(connection_fd, data.data(), data.size()) == -1) {
      throw std::runtime_error(strerror(errno));
    }
  }

  std::vector<char> read()
  {
    constexpr size_t buffer_size = 1024;
    std::vector<char> buffer(buffer_size);
    auto res = ::read(connection_fd, buffer.data(), buffer_size);
    if(res == -1) {
      throw std::runtime_error(strerror(errno));
    }
    buffer.resize(res);
    return std::move(buffer);
  }

private:
  sockaddr client_addr;
  socklen_t client_addr_len;
  int connection_fd;
  int listen_fd;
};

using connection = std::shared_ptr<connection_type>;

auto make_server(unsigned long port) {
  return rxcpp::observable<>::create<connection>
    ([port](rxcpp::subscriber<connection> sub) {

      sockaddr_in serv_addr;
      {
        bzero(&serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        serv_addr.sin_port = htons(port);
      }
      try {
        constexpr auto protocol = 0;
        int listenfd = socket(AF_INET, SOCK_STREAM, protocol);
        if(listenfd == -1) {
          throw std::runtime_error(strerror(errno));
        }

        auto iomode = 0; // blocking
        ioctl(listenfd, FIONBIO, &iomode);

        if(bind(listenfd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr))) {
          throw std::runtime_error(strerror(errno));
        }

        constexpr auto pool_size = 5;
        if(listen(listenfd, pool_size)) {
          throw std::runtime_error(strerror(errno));
        }

        while(1) {
          auto conn = std::make_shared<connection_type>(listenfd);
          conn->accept();
          sub.on_next(conn);
        }
      } catch(...) {
        sub.on_error(std::current_exception());
        return;
      }
      // Never completes
      sub.on_completed();
    });
}

SCENARIO("hej") {
  using namespace std::chrono_literals;

  auto threads = rxcpp::observe_on_event_loop();

  auto socket_connected = [&threads](connection conn) {

    auto read_fn = [conn](auto sub) {
      while(true) {
        sub.on_next(std::move(conn->read()));
      }
    };

    auto on_read = [](auto data) {
      std::cout << "READ: ";
      for(auto& b : data) std::cout << b;
      std::cout << '\n';
    };

    auto reader = rxcpp::observable<>::create<std::vector<char>>(read_fn)
    .subscribe_on(threads)
    .subscribe(on_read);

    std::cout << "SENDING\n";

    std::vector<char> buffer(1024);
    time_t ticks = time(nullptr);
    snprintf(buffer.data(), buffer.size(), "%.24s\r\n", ctime(&ticks));
    conn->write(std::move(buffer));
  };

  auto socket_error = [](std::exception_ptr eptr) {
    try {
      std::rethrow_exception(eptr);
    } catch(const std::exception& e) {
      std::cout << "Something went wrong! " << e.what() << '\n';
    }
  };

  make_server(5000)
    .retry(10)
    .as_blocking()
    .subscribe(socket_connected, socket_error);
}
