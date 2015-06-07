//=======================================================================
// Copyright (c) 2015 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include<iostream>
#include<thread>

#include<cstdio>
#include<cstring>

#include<sys/socket.h>
#include<sys/un.h>
#include<sys/types.h>
#include<unistd.h>
#include<signal.h>

#include <wiringPi.h>

namespace {

int socket_fd;
const std::size_t UNIX_PATH_MAX = 108;

char buffer[4096];

void connection_handler(int connection_fd){
    std::cout << "asgard: New connection received" << std::endl;

    int nbytes;
    while((nbytes = read(connection_fd, buffer, 4096)) > 0){
        buffer[nbytes] = 0;
        std::cout << "asgard: Received message: " << buffer << std::endl;
    }

    close(connection_fd);
}

void terminate(int /*signo*/){
    digitalWrite(1, LOW);

    close(socket_fd);
    unlink("/tmp/asgard_socket");

    abort();
}


} //end of anonymous namespace

int main(){
    //Register signals for "proper" shutdown
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);

    wiringPiSetup();
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);

    //Open the socket
    socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if(socket_fd < 0){
        printf("socket() failed\n");
        return 1;
    }

    unlink("/tmp/asgard_socket");

    //Init the address
    struct sockaddr_un address;
    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, UNIX_PATH_MAX, "/tmp/asgard_socket");

    //Bind
    if(bind(socket_fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)) != 0){
        printf("bind() failed\n");
        return 1;
    }

    if(listen(socket_fd, 5) != 0){
        printf("listen() failed\n");
        return 1;
    }

    int connection_fd;
    socklen_t address_length;
    while((connection_fd = accept(socket_fd, (struct sockaddr *) &address, &address_length)) > -1){
        std::thread(connection_handler, connection_fd).detach();
    }

    close(socket_fd);
    unlink("/tmp/asgard_socket");

    return 0;
}
