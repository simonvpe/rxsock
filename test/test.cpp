#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <server.hpp>

#include <rxcpp/rx.hpp>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <regex>

#include <rxcpp/operators/rx-concat_map.hpp>

namespace Rx {
  using namespace rxcpp;
  using namespace rxcpp::sources;
  using namespace rxcpp::operators;
  using namespace rxcpp::util;
}
using namespace Rx;

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

    auto on_read = [](std::string data) {
      std::cout << "Read: " << data << '\n';
    };

    auto echo = [conn](std::string data) {
      conn.write(std::move(data));
    };

    auto on_completed = [] {
      std::cout << "Socket completed\n";
    };

    conn
    | subscribe_on(rxcpp::synchronize_new_thread())
    | concat_map([](std::string s) {
	using namespace std;
	regex delim("(k)");
	cregex_token_iterator cursor(&s[0], &s[0] + s.size(), delim, {-1, 0});
	cregex_token_iterator end;
	vector<string> splits(cursor, end);
	return rxcpp::sources::iterate(move(splits));
      })
    | filter([](const std::string& s) {
	return !s.empty();
      })
    | subscribe<std::string>(on_read, on_error, on_completed);

    std::string data("hello");
    conn.write(std::move(data));
  };

  rxsock::make_server(5000)
    .as_blocking()
    .subscribe(on_socket_connected, on_error);
}
