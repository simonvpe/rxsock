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

struct headers {
  std::string method;
  
  headers with_method(const std::string& method) const {
    return {
      method
    };
  }

  operator std::string() const {
    std::ostringstream ss;
    ss << "HEADERS:\n"
       << "  method: " << method << '\n';
    return ss.str();
  }
};

std::vector<std::string> split(const std::string& str, std::regex delim) {
  using namespace std;
  sregex_token_iterator cursor(cbegin(str), cend(str), delim, {-1, 0});
  sregex_token_iterator end;
  vector<string> splits(cursor, end);
  return splits;
}

const auto tokenize_on(std::regex delim) {
  return [=](observable<std::string> obs) {
    return obs
      | concat_map([delim](std::string s) {
	  auto splits = split(s, delim);
	  return rxcpp::sources::iterate(move(splits));
	})
      | filter([](const std::string& s) {
	  return !s.empty();
	});
  };
}

const auto parse_header() {
  return [=](observable<std::string> obs) {
    using namespace std::chrono_literals;
    
    auto method = obs
      | first()
      | filter([](std::string x) {
	  const auto tokens = split(x, std::regex{R"/(\s)/"});
	  return tokens.size() == 5 && tokens[0] == "GET" && tokens[4] == "HTTP/1.1";
	})
      | tap([](std::string x) { std::cout << "METHOD " << x << '\n'; });

    auto header_end = obs
      | filter([](std::string x) { return std::regex_match(x, std::regex{R"/(\r?\n\r?\n)/"}); })
      | tap([](std::string x) { std::cout << "HEADER END " << x << '\n'; });

    auto update_headers = [](const headers& hdr, std::string s) {
      if(std::regex_match(s, std::regex{R"/(GET .+ HTTP/1.1)/"}))
	{
	  return hdr.with_method("GET");
	}
      else if(std::regex_match(s, std::regex{R"/([A-Za-z\-]+:\s?.+)/"}))
	{
	  return hdr.with_method(s);
	}
      else if(std::regex_match(s, std::regex{R"/((\r?\n){1,2})/"}))
	{
	  return hdr;
	}
      throw std::runtime_error(std::string("Bad header: ") + s);
    };

    return obs
      | window_toggle(method, [=](auto x) { return header_end; })
      | flat_map([=](observable<std::string> obs) {
	  return obs | accumulate(headers{}, update_headers);
	})
      | take(1)
      | timeout(200ms, rxcpp::synchronize_new_thread())
      | map([](const headers& hdr) { return (std::string)hdr; })
      | tap([](const std::string& str) { std::cout << str; });
  };
}


SCENARIO("hej") {

  {
    const auto tokens = split("a,b,c", std::regex(R"/(,+)/"));
    CHECK(tokens.size() == 5);
    CHECK(tokens[0] == "a");
    CHECK(tokens[1] == ",");
    CHECK(tokens[2] == "b");
    CHECK(tokens[3] == ",");
    CHECK(tokens[4] == "c");
  }

  {
    const auto tokens = split("a,,", std::regex(R"/(,+)/"));
    CHECK(tokens.size() == 2);
    CHECK(tokens[0] == "a");
    CHECK(tokens[1] == ",,");
  }
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
      //std::cout << "Read: " << data << '\n';
    };

    auto on_read_v = [=](std::vector<std::string> data) {
      std::for_each(cbegin(data), cend(data), on_read);
    };
    
    auto echo = [conn](std::string data) {
      conn.write(std::move(data));
    };

    auto on_completed = [] {
      std::cout << "Socket completed\n";
    };

    auto chunks = conn
    | subscribe_on(rxcpp::synchronize_new_thread())
    | tokenize_on(std::regex(R"/(\n+)/"))
    | publish()
    | ref_count();

    auto header = chunks
    | parse_header();
    
    chunks
    | skip_until(header)
    | subscribe<std::string>(on_read, on_error, on_completed);
			 
    //std::string data("hello");
    //conn.write(std::move(data));
  };

  rxsock::make_server(5000)
    .as_blocking()
    .subscribe(on_socket_connected, on_error);
}
