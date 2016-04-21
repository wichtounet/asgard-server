//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include<iostream>
#include<sstream>
#include<thread>
#include<vector>
#include<algorithm>

#include<cstdlib>
#include<cstdio>
#include<cstring>

#include<sys/socket.h>
#include<sys/un.h>
#include<sys/types.h>
#include<unistd.h>
#include<signal.h>

#include <ctime>

#include <mongoose/Server.h>
#include <mongoose/WebController.h>

#include "asgard/utils.hpp"

#include "db.hpp"
#include "led.hpp"
#include "display_controller.hpp"

namespace {

const std::size_t UNIX_PATH_MAX = 108;
const std::size_t socket_buffer_size = 4096;
const std::size_t max_sources = 32;

const char* socket_path = "/tmp/asgard_socket";

int socket_fd;

// Allocate space for the buffers
char receive_buffer[socket_buffer_size];
char write_buffer[socket_buffer_size];

struct sensor_t {
    std::size_t id;
    std::string type;
    std::string name;
};

struct action_t {
    std::size_t id;
    std::string type;
    std::string name;
};

struct actuator_t {
    std::size_t id;
    std::string name;
};

struct source_t {
    std::size_t id;
    std::string name;
    std::vector<sensor_t> sensors;
    std::vector<actuator_t> actuators;
    std::vector<action_t> actions;

    std::size_t id_sql;
    std::size_t sensors_counter;
    std::size_t actuators_counter;
    std::size_t actions_counter;
};

std::size_t current_source = 0;

std::vector<source_t> sources;

source_t& select_source(std::size_t source_id) {
    for (auto& source : sources) {
        if (source.id == source_id) {
            return source;
        }
    }

    std::cerr << "asgard: server: Invalid request for source id " << source_id << std::endl;

    return sources.front();
}

// Create the controller handling the requests
display_controller controller;

void cleanup();

void handle_command(const std::string& message, sockaddr_un& client_address, socklen_t& address_length) {
    std::stringstream message_ss(message);

    std::string command;
    message_ss >> command;

    if (command == "REG_SOURCE") {
        sources.emplace_back();

        auto& source             = sources.back();
        source.id                = current_source++;
        source.sensors_counter   = 0;
        source.actuators_counter = 0;
        source.actions_counter   = 0;

        message_ss >> source.name;

        // Give the source id back to the client
        auto nbytes = snprintf(write_buffer, 4096, "%d", (int) source.id);
        if (sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr*)&client_address, address_length) < 0) {
            std::perror("asgard: server: failed to answer");
            return;
        }

        try {
            CppSQLite3Buffer buffSQL;
            buffSQL.format("insert into source(name,fk_pi) select \"%s\", 1 where not exists(select 1 from source where name=\"%s\");",
                           source.name.c_str(), source.name.c_str());
            get_db().execDML(buffSQL);
            buffSQL.format("select pk_source from source where name=\"%s\";", source.name.c_str());
            source.id_sql = get_db().execScalar(buffSQL);
        } catch (CppSQLite3Exception& e) {
            std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
        }

        std::cout << "asgard: new source registered " << source.id << " : " << source.name << std::endl;
    } else if (command == "UNREG_SOURCE") {
        int source_id;
        message_ss >> source_id;

        sources.erase(std::remove_if(sources.begin(), sources.end(), [&](source_t& source) {
                          return source.id == static_cast<std::size_t>(source_id);
                      }), sources.end());

        std::cout << "asgard: unregistered source " << source_id << std::endl;
    } else if (command == "REG_SENSOR") {
        int source_id;
        message_ss >> source_id;

        auto& source = select_source(source_id);

        source.sensors.emplace_back();
        auto& sensor = source.sensors.back();

        message_ss >> sensor.type;
        message_ss >> sensor.name;

        sensor.id = source.sensors_counter++;

        // Give the sensor id back to the client
        auto nbytes = snprintf(write_buffer, 4096, "%d", (int) sensor.id);
        if (sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr*)&client_address, address_length) < 0) {
            std::perror("asgard: server: failed to answer");
            return;
        }

        if(db_exec_dml(
            get_db(), "insert into sensor(type, name, fk_source) select \"%s\", \"%s\","
            "%d where not exists(select 1 from sensor where type=\"%s\" and name=\"%s\");"
            , sensor.type.c_str(), sensor.name.c_str(), source.id_sql, sensor.type.c_str(), sensor.name.c_str())) {

            auto sensor_type = sensor.type;
            std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);
            std::string url = "/" + sensor.name + "/" + sensor_type;
            controller.addRoute<display_controller>("GET", url + "/data", &display_controller::sensor_data);
            controller.addRoute<display_controller>("GET", url + "/script", &display_controller::sensor_script);
        }

        std::cout << "asgard: new sensor registered " << sensor.id << " (" << sensor.type << ") : " << sensor.name << std::endl;
    } else if (command == "UNREG_SENSOR") {
        int source_id;
        message_ss >> source_id;

        int sensor_id;
        message_ss >> sensor_id;

        auto& source = select_source(source_id);

        source.sensors.erase(std::remove_if(source.sensors.begin(), source.sensors.end(), [&](sensor_t& sensor) {
                                 return sensor.id == static_cast<std::size_t>(sensor_id);
                             }), source.sensors.end());

        std::cout << "asgard: sensor unregistered from source " << source_id << " : " << sensor_id << std::endl;
    } else if (command == "REG_ACTION") {
        int source_id;
        message_ss >> source_id;

        auto& source = select_source(source_id);

        source.actions.emplace_back();
        auto& action = source.actions.back();

        message_ss >> action.type;
        message_ss >> action.name;

        action.id = source.actions_counter++;

        // Give the action id back to the client
        auto nbytes = snprintf(write_buffer, 4096, "%d", action.id);
        if (sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr*)&client_address, address_length) < 0) {
            std::perror("asgard: server: failed to answer");
            return;
        }

        db_exec_dml(
            get_db(),
            "insert into action(type, name, fk_source) select \"%s\", \"%s\","
            "%d where not exists(select 1 from action where type=\"%s\" and name=\"%s\");",
            action.type.c_str(), action.name.c_str(), source.id_sql, action.type.c_str(), action.name.c_str());

        std::cout << "asgard: new action registered " << action.id << " (" << action.type << ") : " << action.name << std::endl;
    } else if (command == "UNREG_ACTION") {
        int source_id;
        message_ss >> source_id;

        int action_id;
        message_ss >> action_id;

        auto& source = select_source(source_id);

        source.actions.erase(std::remove_if(source.actions.begin(), source.actions.end(), [&](action_t& action) {
                                 return action.id == static_cast<std::size_t>(action_id);
                             }), source.actions.end());

        std::cout << "asgard: action unregistered from source " << source_id << " : " << action_id << std::endl;
    } else if (command == "REG_ACTUATOR") {
        int source_id;
        message_ss >> source_id;

        auto& source = select_source(source_id);

        source.actuators.emplace_back();
        auto& actuator = source.actuators.back();

        message_ss >> actuator.name;

        actuator.id = source.actuators_counter++;

        // Give the sensor id back to the client
        auto nbytes = snprintf(write_buffer, 4096, "%d", (int) actuator.id);
        if (sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr*)&client_address, address_length) < 0) {
            std::perror("asgard: server: failed to answer");
            return;
        }

        if(db_exec_dml(get_db(), "insert into actuator(name, fk_source) select \"%s\", %d where not exists(select 1 from actuator where name=\"%s\");"
                      , actuator.name.c_str(), source.id_sql, actuator.name.c_str())){

            std::string url = "/" + actuator.name;
            controller.addRoute<display_controller>("GET", url + "/data", &display_controller::actuator_data);
            controller.addRoute<display_controller>("GET", url + "/script", &display_controller::actuator_script);
        }

        std::cout << "asgard: new actuator registered " << actuator.id << " : " << actuator.name << std::endl;
    } else if (command == "UNREG_ACTUATOR") {
        int source_id;
        message_ss >> source_id;

        int actuator_id;
        message_ss >> actuator_id;

        auto& source = select_source(source_id);

        source.actuators.erase(std::remove_if(source.actuators.begin(), source.actuators.end(), [&](actuator_t& actuator) {
                                   return actuator.id == static_cast<std::size_t>(actuator_id);
                               }), source.actuators.end());

        std::cout << "asgard: actuator unregistered from source " << source_id << " : " << actuator_id << std::endl;
    } else if (command == "DATA") {
        int source_id;
        message_ss >> source_id;

        int sensor_id;
        message_ss >> sensor_id;

        std::string data;
        message_ss >> data;

        auto& source = select_source(source_id);
        auto& sensor = source.sensors[sensor_id];

        int sensor_pk = db_exec_scalar(get_db(), "select pk_sensor from sensor where name=\"%s\" and type=\"%s\";", sensor.name.c_str(), sensor.type.c_str());

        db_exec_dml(get_db(), "insert into sensor_data (data, fk_sensor) values (\"%s\", %d);", data.c_str(), sensor_pk);

        std::cout << "asgard: server: new data: sensor(" << sensor.type << "): \"" << sensor.name << "\" : " << data << std::endl;
    } else if (command == "EVENT") {
        int source_id;
        message_ss >> source_id;

        int actuator_id;
        message_ss >> actuator_id;

        std::string data;
        message_ss >> data;

        auto& source   = select_source(source_id);
        auto& actuator = source.actuators[actuator_id];

        int actuator_pk = db_exec_scalar(get_db(), "select pk_actuator from actuator where name=\"%s\";", actuator.name.c_str());

        db_exec_dml(get_db(), "insert into actuator_data (data, fk_actuator) values (\"%s\", %d);", data.c_str(), actuator_pk);

        std::cout << "asgard: server: new event: actuator: \"" << actuator.name << "\" : " << data << std::endl;
    }
}

int run(){
    // Create the socket
    socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if(socket_fd < 0){
        std::cerr << "socket() failed" << std::endl;
        return 1;
    }

    // Init the server address
    struct sockaddr_un server_address;
    memset(&server_address, 0, sizeof(struct sockaddr_un));
    server_address.sun_family = AF_UNIX;
    snprintf(server_address.sun_path, UNIX_PATH_MAX, socket_path);

    // Unlink the socket file
    unlink(socket_path);

    // Bind
    if (::bind(socket_fd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        std::cerr << "bind() failed" << std::endl;
        close(socket_fd);
        return 1;
    }

    std::cout << "asgard: Server started" << std::endl;

    while (true) {
        sockaddr_un client_address;
        socklen_t address_length = sizeof(struct sockaddr_un);

        // Wait for one message
        auto bytes_received = recvfrom(socket_fd, receive_buffer, socket_buffer_size-1, 0, (struct sockaddr *) &client_address, &address_length);
        receive_buffer[bytes_received] = '\0';

        // Handle the message
        handle_command(receive_buffer, client_address, address_length);
    }

    cleanup();

    return 0;
}

void cleanup() {
    set_led_off();
    close(socket_fd);
    unlink("/tmp/asgard_socket");
}

void terminate(int /*signo*/) {
    std::cout << "asgard: server: stopping the server" << std::endl;
    cleanup();
    abort();
}

} //end of anonymous namespace

int main() {
    setup_led_controller();

    //Drop root privileges and run as pi:pi again
    if (!asgard::revoke_root()) {
       std::cout << "asgard: unable to revoke root privileges, exiting..." << std::endl;
       return 1;
    }

    // Open (connect) the database
    if(!db_connect(get_db())){
       std::cout << "asgard: unable to connect to the database, exiting..." << std::endl;
       return 1;
    }

    // Run the server with our controller
    Mongoose::Server server(8080);
    server.registerController(&controller);

    // Start the server and wait forever
    server.start();

    //Register signals for "proper" shutdown
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);

    init_led();
    set_led_on();

    return run();
}
