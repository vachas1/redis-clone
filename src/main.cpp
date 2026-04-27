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

enum class DataType{STRING,LIST};

struct ValueWithExpiry{
  DataType type = DataType::STRING;
  std::string value;
  std::vector<std::string> list;
  std::chrono::steady_clock::time_point expiry_time;
  bool has_expiry = false;
};

std::unordered_map<std::string, ValueWithExpiry> kv_store;
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

    
    std::string command = args[0];
    std::transform(command.begin(), command.end(), command.begin(), ::toupper);
    
    
    if(command == "PING"){
      const char *response = "+PONG\r\n";
      send(client_fd, response, strlen(response),0);
    }
    else if (command == "ECHO"){
      if(args.size()<2) continue;
      std::string echo_arg = args[1];
      std::string response = "$" +std::to_string(echo_arg.length()) + "\r\n" + echo_arg + "\r\n";
      send(client_fd, response.c_str(), response.length(), 0);
    }

    else if(command == "SET"){
      if(args.size() < 3) continue;

      ValueWithExpiry entry;
      std::string key = args[1];
      entry.value = args[2];

      if(args.size()>=5){
        std::string option = args[3];
        std::transform(option.begin(),option.end(),option.begin(),::toupper);

        if(option == "PX"){
          long long ms = std::stoll(args[4]);
          entry.expiry_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
          entry.has_expiry = true;

        }
      }

      {
        std::lock_guard<std::mutex> lock(kv_mutex);
        kv_store[key] = entry;

      }

      const char *response = "+OK\r\n";
      send(client_fd, response, strlen(response),0);
    }
    else if(command == "GET"){
      if(args.size()<2) continue;

      std::string key  = args[1];
      std::string response =  "$-1\r\n";
      {
        std::lock_guard<std::mutex> lock(kv_mutex);
        if(kv_store.count(key)){

          auto& entry = kv_store[key];
          
          if(entry.has_expiry && std::chrono::steady_clock::now() >= entry.expiry_time){
            kv_store.erase(key);
          }
          else{
            response = "$" + std::to_string(entry.value.length()) + "\r\n" + entry.value + "\r\n";
          }
        }
      }
      send(client_fd, response.c_str(), response.length(), 0);

    }
    
    else if(command == "RPUSH"){
      if(args.size()<3) continue;

      std::string key = args[1];
      int new_length = 0;

      {
        std::lock_guard<std::mutex> lock(kv_mutex);

        auto& entry = kv_store[key];
        entry.type = DataType::LIST;

        for (size_t i = 2;i<args.size(); i++){
          entry.list.push_back(args[i]);
        }

        new_length = entry.list.size();

      }

      std::string response = ":" + std::to_string(new_length) + "\r\n";
      send(client_fd, response.c_str(), response.length(), 0 );

    }

    else if(command == "LPUSH"){
      if(args.size()<3) continue;

      std::string key = args[1];
      int new_length = 0;

      {
        std::lock_guard<std::mutex> lock(kv_mutex);

        auto& entry = kv_store[key];
        entry.type = DataType::LIST;

        for (size_t i = 2;i<args.size(); i++){
          entry.list.insert(entry.list.begin(),args[i]);
        }

        new_length = entry.list.size();

      }

      std::string response = ":" + std::to_string(new_length) + "\r\n";
      send(client_fd, response.c_str(), response.length(), 0 );

    }

    else if(command == "LRANGE"){
      if(args.size()<4) continue;
      
      
      std::string key = args[1];
      int start = std::stoi(args[2]);
      int stop = std::stoi(args[3]);
    
      std::vector<std::string> result;
      
      {
        std::lock_guard<std::mutex> lock(kv_mutex);
        
        if(kv_store.count(key) && kv_store[key].type == DataType::LIST){
          const auto& list = kv_store[key].list;
          int n = static_cast<int>(list.size());


          if(start<0) start = n+start;
          if(stop<0) stop = n+stop;

          if(start<0) start = 0;
          if(stop>=n) stop = n-1;

          if(start<n && start<=stop){
            for(int i=start; i<=stop; ++i){
              result.push_back(list[i]);
            }
          }
        }
      }
      std::string response = "*" + std::to_string(result.size())+"\r\n";
      for(const auto& item : result){
        response += "$" + std::to_string(item.length()) + "\r\n" + item + "\r\n";
      }
      send(client_fd, response.c_str(), response.length(), 0);
    }
    

    else if (command == "LLEN") {
      if(args.size()<2) continue;
      
      std::string key = args[1];
      int length = 0;
      {
        std::lock_guard<std::mutex> lock(kv_mutex);
        if(kv_store.count(key)){
          auto&entry = kv_store[key];

          if(entry.type == DataType::LIST){
            length = entry.list.size();

          }
          else{
            const char* err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            send(client_fd, err, strlen(err), 0);
            continue;
          }
        }
        else{
          length = 0;
        }
        
      }

      std::string response = ":" + std::to_string(length) + "\r\n";
      send(client_fd, response.c_str(), response.length(),0);
      
    }

    else if(command == "LPOP"){
      if(args.size()<2) continue;

      std::string key = args[1];
      std::string response = "$-1\r\n";
      {
        std::lock_guard<std::mutex> lock(kv_mutex);
        if(kv_store.count(key)){
          auto& entry = kv_store[key];
          
          if(entry.type == DataType::LIST){
            
            if(!entry.list.empty()){
              std::string first_val = entry.list.front();
              entry.list.erase(entry.list.begin());

              response = "$"+ std::to_string(first_val.length()) + "\r\n" + first_val + "\r\n";

            }
          }

          else{
            const char* err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            send(client_fd, err, strlen(err), 0);
            continue;
          }
        }
      }
      send(client_fd, response.c_str(),response.length(), 0);
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
