#include <iostream>
#include <cstdlib>
#include <string>
#include <sstream>
#include <string_view>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unordered_map>
#include <functional>

int main(int argc, char **argv)
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  // Uncomment this block to pass the first stage
  //
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
  {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0)
  {
    std::cerr << "listen failed\n";
    return 1;
  }

  // Register server listening socket
  int ep_fd = epoll_create1(0);
  struct epoll_event server_ev{};
  server_ev.events = EPOLLIN;
  server_ev.data.fd = server_fd;

  epoll_ctl(ep_fd, EPOLL_CTL_ADD, server_fd, &server_ev);
  epoll_event events[10]{};

  std::string resp_ok{"HTTP/1.1 200 OK\r\n\r\n"};
  std::string resp_not_found{"HTTP/1.1 404 Not Found\r\n\r\n"};

  auto handleEcho = [&](const std::string_view path, int client_fd)
  {
    std::string prefix{"/echo/"};
    std::string body{path.substr(prefix.size())};
    std::string headers{
        resp_ok + "\r\n" +
        "Content-Type: text/plain\r\n" +
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n"};

    std::string response = headers + body;
    std::cout << "response: " << response << '\n';
    std::cout << "body: " << body << '\n';
    send(client_fd, response.data(), response.size(), 0);
  };

  auto handleRoot = [&](const std::string_view path, int client_fd)
  {
    if (path.size() > 1)
    {
      send(client_fd, resp_not_found.data(), resp_not_found.size(), 0);

      return;
    }

    send(client_fd, resp_ok.data(), resp_ok.size(), 0);
  };

  std::vector<std::pair<std::string, std::function<void(const std::string_view, int)>>> routeHandlers = {
      {"/echo/", handleEcho},
      {"/", handleRoot},
  };

  auto dispatch = [&](const std::string_view path, int client_fd)
  {
    for (const auto &[prefix, handler] : routeHandlers)
    {

      if (path.starts_with(prefix))
      {
        std::cout << "Path starts with: " << prefix << '\n';
        std::cout << "Path is: " << path << '\n';
        handler(path, client_fd);

        return;
      }
    }

    send(client_fd, resp_not_found.data(), resp_ok.size(), 0);
  };

  // Receive buffer
  std::vector<char> recvBuffer(4096);

  // Enter event loop
  while (true)
  {
    int nfds = epoll_wait(ep_fd, events, 10, -1);

    // Check for events
    for (int i = 0; i < nfds; ++i)
    {
      int fd = events[i].data.fd;

      // If event source is from the server
      if (fd == server_fd)
      {
        // Accept new connection
        struct sockaddr_in client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_len);

        // An error occurred
        if (client_fd < 0)
        {
          perror("Client connection error");
          continue;
        }

        std::cout << "Client connected!" << '\n';

        // Register client with epoll
        epoll_event client_ev{};
        client_ev.events = EPOLLIN;
        client_ev.data.fd = client_fd;
        epoll_ctl(ep_fd, EPOLL_CTL_ADD, client_fd, &client_ev);
      }
      else
      {
        // Client read
        ssize_t bytes = recv(fd, recvBuffer.data(), recvBuffer.size(), 0);

        // Client sent something
        if (bytes > 0)
        {
          std::string req(recvBuffer.data(), bytes);
          std::cout << "Received: " << req << '\n';
          std::istringstream iss(req);
          std::string method, path, version;
          iss >> method >> path >> version;

          // Determine request method
          if (method == "GET")
          {
            dispatch(path, fd);
            // if (path == "/")
            // {
            //   send(fd, resp_ok.data(), resp_ok.size(), 0);
            // }
            // else
            // {
            //   send(fd, resp_not_found.data(), resp_not_found.size(), 0);
            // }
          }
        }
        else
        {
          // Client disconnected or error
          std::cout << "Client disconnected" << '\n';
          close(fd);
          epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd, nullptr);
        }
      }
    }
  }

  close(server_fd);

  return 0;
}
