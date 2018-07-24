#pragma once

extern "C" {
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <errno.h>
#  include <string.h>
}

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

}
