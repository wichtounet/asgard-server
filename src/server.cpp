//=======================================================================
// Copyright (c) 2015 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include<iostream>
#include<thread>
#include<vector>

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
const std::size_t gpio_led_pin = 1;
const std::size_t socket_buffer_size = 4096;
const std::size_t max_sources = 32;

struct sensor_t {
    std::size_t id;
    std::string name;
};

struct source_t {
    bool active;
    std::vector<sensor_t> sensors;
};

source_t sources[max_sources];

void connection_handler(int connection_fd, std::size_t source_id){
    char buffer[socket_buffer_size];

    std::cout << "asgard: New connection received" << std::endl;

    int nbytes;
    while((nbytes = read(connection_fd, buffer, socket_buffer_size)) > 0){
        buffer[nbytes] = 0;
        std::cout << "asgard: Received message: " << buffer << std::endl;

        std::string message(buffer);

        std::string command(buffer.begin(), buffer.begin() + buffer.find(' '));

        std::cout << "asgard: Command: " << command << std::endl;
    }

    close(connection_fd);
}

void close(){
    digitalWrite(gpio_led_pin, LOW);

    close(socket_fd);
    unlink("/tmp/asgard_socket");
}

void terminate(int /*signo*/){
    close();

    abort();
}

bool revoke_root(){
    if (getuid() == 0) {
        if (setgid(1000) != 0){
            std::cout << "asgard: setgid: Unable to drop group privileges: " << strerror(errno) << std::endl;
            return false;
        }

        if (setuid(1000) != 0){
            std::cout << "asgard: setgid: Unable to drop user privileges: " << strerror(errno) << std::endl;
            return false;
        }
    }

    if (setuid(0) != -1){
        std::cout << "asgard: managed to regain root privileges, exiting..." << std::endl;
        return false;
    }

    return true;
}

} //end of anonymous namespace

int main(){
    for(std::size_t i = 0; i < max_sources; ++i){
        sources[i].active = false;
    }

    //Run the wiringPi setup (as root)
    wiringPiSetup();

    //Drop root privileges and run as pi:pi again
    if(!revoke_root()){
       std::cout << "asgard: unable to revoke root privileges, exiting..." << std::endl;
       return 1;
    }

    //Register signals for "proper" shutdown
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);

    pinMode(gpio_led_pin, OUTPUT);
    digitalWrite(gpio_led_pin, HIGH);

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

    //Listen
    if(listen(socket_fd, 5) != 0){
        printf("listen() failed\n");
        return 1;
    }

    int connection_fd;
    socklen_t address_length;

    std::size_t current_source = 0;

    //Wait for connection
    while((connection_fd = accept(socket_fd, (struct sockaddr *) &address, &address_length)) > -1){
        sources[current_source].active = true;
        std::thread(connection_handler, connection_fd, current_source).detach();
        ++current_source;
    }

    close();

    return 0;
}
