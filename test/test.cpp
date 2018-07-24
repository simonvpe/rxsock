#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <rxcpp/rx.hpp>
#include <iostream>
#include <stdexcept>
#include <algorithm>

extern "C" {
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <errno.h>
#  include <string.h>
}

#include <rxcpp/rx-includes.hpp>

namespace rxsock {

class socket_t
{
public:
  socket_t(unsigned long port)
  {
    sockaddr_in serv_addr;
    {
      bzero(&serv_addr, sizeof(serv_addr));
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      serv_addr.sin_port = htons(port);
    }

    constexpr auto protocol = 0;
    fd = socket(AF_INET, SOCK_STREAM, protocol);
    if(fd == -1) {
      throw std::runtime_error(strerror(errno));
    }

    auto iomode = 0; // blocking
    ioctl(fd, FIONBIO, &iomode);

    if(bind(fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr))) {
      throw std::runtime_error(strerror(errno));
    }

    constexpr auto pool_size = 5;
    if(listen(fd, pool_size)) {
      throw std::runtime_error(strerror(errno));
    }
  }

  int file_descriptor() const { return fd; }

private:
  int fd;
};

struct connection_t : public rxcpp::sources::source_base<char>
{
  struct connection_t_state_type {
    connection_t_state_type() = delete;
    connection_t_state_type(const connection_t_state_type&) = delete;
    connection_t_state_type(connection_t_state_type&&) = delete;

    connection_t_state_type(const socket_t& socket)
      : client_addr_len{sizeof(client_addr)}
      , connection_fd{-1}
      , socket{socket}
    {}

    ~connection_t_state_type() {
      if(connection_fd >= 0) {
        ::close(connection_fd);
        std::cerr << "Connection " << connection_fd << " closed!\n";
      }
    }

    void accept()
    {
      connection_fd = ::accept(socket.file_descriptor(), &client_addr, &client_addr_len);
      if(connection_fd == -1){
        throw std::runtime_error(strerror(errno));
      }
      std::cerr << "Connection established " << connection_fd << '\n';
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

    void write(std::vector<char>&& data) const
    {
      if(::write(connection_fd, data.data(), data.size()) == -1) {
        throw std::runtime_error(strerror(errno));
      }
    }

    void write(char byte) const
    {
      if(::write(connection_fd, &byte, 1) == -1) {
        throw std::runtime_error(strerror(errno));
      }
    }

    sockaddr client_addr;
    socklen_t client_addr_len;
    int connection_fd;
    const socket_t& socket;
  };

  std::shared_ptr<connection_t_state_type> state;

  connection_t(const socket_t& socket)
    : state{std::make_shared<connection_t_state_type>(socket)}
  {}
  connection_t(const connection_t& other)
    : state{other.state}
  {}
  connection_t(connection_t&& other)
    : state{std::move(other.state)}
  {}

  template<class Subscriber>
  void on_subscribe(Subscriber sub) const {
    static_assert(rxcpp::is_subscriber<Subscriber>::value, "subscribe must be passed a subscriber");

    auto loop = [this, sub] {
      while(true) {
        try {
          auto result = std::move(state->read());

          if(result.size() == 0) {
            sub.on_completed();
            return false;
          }

          auto next = [&](char byte) { sub.on_next(byte); };
          std::for_each(cbegin(result), cend(result), next);
        } catch(...) {
          sub.on_error(std::current_exception());
          return false;
        }
      }
      return true;
    };
    rxcpp::on_exception(loop, sub);
  }
};

class connection : public rxcpp::observable<char, connection_t> {
public:
  connection(connection_t conn) : conn{conn}, rxcpp::observable<char, connection_t>(conn) {}
  void write(std::vector<char>&& data) const { conn.state->write(std::forward<std::vector<char>>(data)); }
  void write(char byte) const { conn.state->write(byte); }
private:
  connection_t conn;
};

auto make_server(unsigned long port) {
  return rxcpp::observable<>::create<connection>
    ([port](rxcpp::subscriber<connection> sub) {
      try {
        socket_t socket{port};

        while(1) {
          connection_t conn{socket};
          conn.state->accept();
          sub.on_next(connection(conn));
        }
      } catch(...) {
        sub.on_error(std::current_exception());
        return;
      }
      // Never completes
      sub.on_completed();
    });
}

}

struct headers {};

SCENARIO("hej") {
  using namespace std::chrono_literals;

  auto on_error = [](std::exception_ptr eptr) {
    try {
      std::rethrow_exception(eptr);
    } catch(const std::exception& e) {
      std::cout << "Something went wrong! " << e.what() << '\n';
    }
  };

  auto on_socket_connected = [on_error](rxsock::connection conn) {

    auto on_read = [](char byte) {
      std::cout << "Byte: " << byte << '\n';
    };

    auto echo = [conn](char byte) {
      conn.write(byte);
    };

    auto on_completed = [] {
      std::cout << "Socket completed\n";
    };

    conn
    .subscribe_on(rxcpp::synchronize_new_thread())
    .tap(echo)
    .subscribe(on_read, on_error, on_completed);

    std::vector<char> data = {'H', 'e', 'l', 'l', 'o'};
    conn.write(std::move(data));
  };

  rxsock::make_server(5000)
    .retry(10)
    .as_blocking()
    .subscribe(on_socket_connected, on_error);
}
