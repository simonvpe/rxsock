#pragma once

#include "connection.hpp"

namespace rxsock {

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
