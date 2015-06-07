#include<iostream>
#include<thread>

#include<cstdio>
#include<cstring>

#include<sys/socket.h>
#include<sys/un.h>
#include<sys/types.h>
#include<unistd.h>

void connection_handler(int connection_fd){
    std::cout << "asgard: New connection received" << std::endl;

    int nbytes;
    char buffer[4096];
    nbytes = read(connection_fd, buffer, 4096);
    buffer[nbytes] = 0;

    printf("MESSAGE FROM CLIENT: %s\n", buffer);

    nbytes = snprintf(buffer, 256, "hello from the server");
    write(connection_fd, buffer, nbytes);

    close(connection_fd);
}

int main(){
    //Open the socket
    auto socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
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
