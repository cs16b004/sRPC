#pragma once
#include "server.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "dpdk_transport/config.hpp"
#include "dpdk_transport/transport.hpp"

namespace rrr {
void UDPServer::handle_read(){
    while(1){
     unsigned int len;
     struct sockaddr_in clientAddr;
     std::vector<uint8_t> buffer;
     std::vector<uint8_t>* buf = &buffer;
    auto cli = &clientAddr;
    
    len = sizeof(*cli);  //len is value/resuslt
    buf->resize(1500);
    int n = ::recvfrom(sockfd_, (char *)buf->data(), 10,
                        MSG_DONTWAIT, (struct sockaddr *) cli, &len);
    Log_info("Number of bytes received %d",n);
   // buf->resize(n);
    i64 xid;
    memcpy(&xid, buf->data() + xid_pos, sizeof(i64));
    Log_info("server receiving udp w/ id: %d, size: %d", xid, buf->size());
    }
    // if (server_->udp_handler_) {
    //     auto handler = server_->udp_handler_;
    //     Coroutine::CreateRun([handler, buf](){
    //         handler(*buf);
    //     });
    // }
    // while (1) {
    //     if(status_==RUNNING){
    //         struct sockaddr_in clientAddr;
    //         memset(&clientAddr, 0, sizeof(clientAddr));
    //         socklen_t addrLen = sizeof(clientAddr);
    //                 // Receive data from client
    //                 ssize_t dataSize = recvfrom(sockfd_, buffer, MAX_BUFFER_SIZE - 1, 0,
    //                                             (struct sockaddr *)&clientAddr, &addrLen);
    //                 if (dataSize < 0) {
    //                     if (errno == EWOULDBLOCK || errno == EAGAIN)
    //                         break;  // No more data to receive
    //                     else {
    //                         perror("Error in recvfrom");
    //                         exit(1);
    //                     }
    //                 }

    //                 buffer[dataSize] = '\0';
    //                 char ca[25];
    //                 inet_ntop(AF_INET, &(((struct sockaddr_in *)&clientAddr)->sin_addr),
    //                 ca, 25);
    //                 Log_info("Received message from client: %s\n", ca);

    //                 // Process received data (you can modify this part according to your needs)
    //                 // In this example, we'll send back the same message to the client

    //                 // Send data back to client
    //                 // ssize_t sentSize = sendto(sockfd, buffer, dataSize, 0,
    //                 //                           (struct sockaddr *)&clientAddr, addrLen);
    //                 // if (sentSize < 0) {
    //                 //     perror("Error in sendto");
    //                 //     exit(1);
    //                 // }
    //                 // Log_info("Sent message back to client: %s\n", buffer);
    //             }
    //}
}
void UDPServer::handle_write(){
    out_l_.lock();
    for (auto& pair : out_) {
        sendto(sockfd_, (const char *)pair.second->data(), pair.second->size(),
               0, (const struct sockaddr *) pair.first,
               (sizeof(*pair.first)));
        i64 xid;
        memcpy(&xid, pair.second->data() + xid_pos, sizeof(i64));
        Log_debug("server sending udp w/ id: %d and size: %d", xid, pair.second->size());
    }
    out_.clear();
    pm->update_mode(this, Pollable::READ);
    out_l_.unlock();
}
void UDPServer::handle_error(){
    status_ = STOPPING;
    //::close(sockfd_);
    status_ = STOPPED;

}
void UDPServer::start(){
    Log_debug("udp server addr: %s", addr_.c_str());
    //server_ = server;
    /* addr = "127.0.0.1:55555"; */
    
    size_t idx = addr_.find(":");
    if (idx == std::string::npos) {
        Log_error("rrr::Server: bad bind address: %s", addr_.c_str());
    }
    std::string host = addr_.substr(0, idx);
    std::string port = addr_.substr(idx + 1);

  // Creating socket file descriptor
    if ( (sockfd_ = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        Log_fatal("socket creation failed");
    }
    struct sockaddr_in servaddr, cliaddr;

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

  // Filling server information

    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(std::atoi(port.c_str()));
    struct in_addr* s_addr_w = (struct in_addr*)malloc(sizeof(struct in_addr));
    s_addr_w->s_addr = servaddr.sin_addr.s_addr;
    const int one = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    // Bind the socket with the server address
    int err = ::bind(sockfd_, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    if (err < 0 ) {
        Log_fatal("bind failed: %d", errno);
    }
    set_nonblocking(sockfd_,true);
    pm->add(this);
    Log_info("UDPServer Launched at %s",inet_ntoa(*s_addr_w));
    status_=RUNNING;

    Log_info("Launching DPDK Server!");
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
       Log_info("Current Path: %s",cwd);
    }
    char * argv[5] = {"./server","-fconfig_files/cpu.yml", "-fconfig_files/network_catskill.yml", 
                    "-fconfig_files/host.yml", "-fconfig_files/dpdk.yml"};
    int argc = 5;
    Config::create_config(argc,argv);
    Config* config = Config::get_config();
    //DpdkTransport dpdk_;
   // dpdk_.init(config);

    // struct sockaddr_in serverAddr, clientAddr;
    

    // // Create socket
    // sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    // if (sockfd_ < 0) {
    //     perror("Error in socket");
    //     exit(1);
    // }

    // memset(&serverAddr, 0, sizeof(serverAddr));
    // memset(&clientAddr, 0, sizeof(clientAddr));

    // // Configure server address
    // serverAddr.sin_family = AF_INET;
    // inet_aton(addr_.c_str(), &(serverAddr.sin_addr));
    // serverAddr.sin_port = htons(port_);

    // // Bind socket to server address
    // if (bind(sockfd_, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    //     perror("Error in bind");
    //     exit(1);
    // }
    // status_ = RUNNING;
    // //Add to poll manager to poll sockets
    // pm->add(this);
    // verify(set_nonblocking(sockfd_, true) == 0);
    // Log_info("Created a UDP Server listening on port %d",port_);
}
int UDPServer::poll_mode(){
   int mode = Pollable::READ;
      out_l_.lock();
      if (!out_.empty()) {
        mode |= Pollable::WRITE;
      }
      out_l_.unlock();
      return mode;
}
}
