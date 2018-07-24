#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <server.hpp>

#include <rxcpp/rx.hpp>
#include <iostream>
#include <stdexcept>
#include <algorithm>

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

    std::string data("hello");
    conn.write(std::move(data));
  };

  rxsock::make_server(5000)
    .retry(10)
    .as_blocking()
    .subscribe(on_socket_connected, on_error);
}
