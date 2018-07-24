#pragma once

#include "socket.hpp"
#include <rxcpp/rx.hpp>
#include <string>

namespace rxsock {

struct connection_t : public rxcpp::sources::source_base<std::string>
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

    std::string read()
    {
      constexpr size_t buffer_size = 1024;
      std::string buffer(buffer_size, '\0');
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

    void write(std::string&& data) const
    {
      if(::write(connection_fd, data.c_str(), data.size()) == -1) {
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
          }

	  sub.on_next(result);
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

class connection : public rxcpp::observable<std::string, connection_t> {
public:
  connection(connection_t conn) : conn{conn}, rxcpp::observable<std::string, connection_t>(conn) {}
  void write(std::vector<char>&& data) const { conn.state->write(std::forward<std::vector<char>>(data)); }
  void write(std::string&& data) const { conn.state->write(std::forward<std::string>(data)); }
  void write(char byte) const { conn.state->write(byte); }
private:
  connection_t conn;
};

}
