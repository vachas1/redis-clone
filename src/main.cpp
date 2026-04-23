#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <bits/stdc++.h>
#include <thread>
#include <vector>
#include <unordered_map>
#include <mutex>

std::unordered_map<std::string, std::string> kv_store;
std::mutex kv_mutex;

std::vector<std::string> parse_resp_array(const std::string& request){
  std::vector<std::string> args;
  if(request.empty() || request[0] != '*') return args;
  size_t pos = request.find("\r\n")+2;
  while(pos<request.length()){
    if(request[pos]!='$') break;

    size_t len_end = request.find("\r\n", pos);
    int len = std::stoi(request.substr(pos+1, len_end - pos -1));

    pos = len_end + 2;
    args.push_back(request.substr(pos,len));
    
    pos += len+2;
  }

  return args;
}

void handle_client(int client_fd){
  char buffer[1024];
  while(true){

    memset(buffer,0,sizeof(buffer));

    int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
    
    if(bytes_received<=0){
      std::cout<<"CLient disconnected or error occured";
      break;
    }

    std::string request(buffer,bytes_received);
    std::vector<std::string> args = parse_resp_array(request);

    
    std::string upper_request = args[0];
    std::transform(upper_request.begin(), upper_request.end(), upper_request.begin(), ::toupper);
    
    
    if(upper_request.find("PING")){
      const char *response = "+PONG\r\n";
      send(client_fd, response, strlen(response),0);
    }
    else if (upper_request.find("ECHO")){
      if(args.size()<2) continue;
      std::string echo_arg = arg[1];
      std::string response = "$" +std::to_string(echo_arg.length()) + "\r\n" + echo_arg + "\r\n";
      send(client_fd, response.c_str(), response.length(), 0);
    }

    else if(upper_request.find("GET")){
      if(args_request.size()<2) continue;

      std::string key  = args[1];
      std::string response;
      {
        std::lock_guard<std::mutex> lock(kv_mutex);
        if(kv_store.find(key) != kv_store.end()){
          std::string value = kv_store[key];
          response = "$" + std::to_string(value.length()) + "\r\n" + value + "\r\n";

        }
        else{
          response = "$-1\r\n";
        }

      }
      send(client_fd, response.c_str(), response.length(), 0);

    }
    else if(upper_request.find("SET")){
      if(args.size() < 3) continue;

      std::string key = args[1];
      std::string value = args[2];

      {
        std::lock_guard<std::mutex> lock(kv_mutex);
        kv_store[key] = value;

      }

      const char *response = "+OK\r\n";
      send(client_fd, response, strlen(response),0);
    }

  }
  close(client_fd);

}


int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;


  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  while(true){
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    std::cout << "Waiting for a client to connect...\n";

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cout << "Logs from your program will appear here!\n";

  
    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if(client_fd <0){
      std::cerr<<"Accept failed\n";
      continue;
    }
    std::thread(handle_client, client_fd).detach();
  }
  
  
  close(server_fd);

  return 0;
}
