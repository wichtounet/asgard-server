//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <algorithm>

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <ctime>

#include <mongoose/Server.h>
#include <mongoose/WebController.h>

#include "asgard/config.hpp"
#include "asgard/utils.hpp"
#include "asgard/network.hpp"

#include "db.hpp"
#include "led.hpp"
#include "display_controller.hpp"
#include "server.hpp"

namespace {

// Configuration
std::vector<asgard::KeyValue> config;
const std::size_t UNIX_PATH_MAX = 108;
const std::size_t socket_buffer_size = 4096;
const std::size_t max_sources = 32;

int socket_desc;
struct sockaddr_in server, client;
std::vector<std::thread> threads;

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

    int socket;
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

source_t& select_source_from_sql(std::size_t source_id) {
    for (auto& source : sources) {
        if (source.id_sql == source_id) {
            return source;
        }
    }

    std::cerr << "asgard: server: Invalid request for source id sql " << source_id << std::endl;

    return sources.front();
}

// Create the controller handling the requests
display_controller controller;

void cleanup() {
    set_led_off();
    close(socket_desc);
    unlink("/tmp/asgard_socket");
}

bool handle_command(const std::string& message, int socket_fd) {
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
        source.socket            = socket_fd;

        message_ss >> source.name;

        // Give the source id back to the client
        auto nbytes = snprintf(write_buffer, 4096, "%d", (int)source.id);
        if (!asgard::send_message(socket_fd, write_buffer, nbytes)) {
            std::perror("asgard: server: failed to answer");
            return true;
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

        return false;
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
        if (!asgard::send_message(socket_fd, write_buffer, nbytes)) {
            std::perror("asgard: server: failed to answer");
            return true;
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
        if (!asgard::send_message(socket_fd, write_buffer, nbytes)) {
            std::perror("asgard: server: failed to answer");
            return true;
        }

        db_exec_dml(
            get_db(), "insert into action(type, name, fk_source) select \"%s\", \"%s\","
            "%d where not exists(select 1 from action where type=\"%s\" and name=\"%s\");",
            action.type.c_str(), action.name.c_str(), source.id_sql, action.type.c_str(), action.name.c_str());

        //TODO Dynamically register the actions

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
        if (!asgard::send_message(socket_fd, write_buffer, nbytes)) {
            std::perror("asgard: server: failed to answer");
            return true;
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

    return true;
}

void connection_handler(int client_socket_fd) {
    while(asgard::receive_message(client_socket_fd, receive_buffer, socket_buffer_size)){
        if(!handle_command(receive_buffer, client_socket_fd)){
            break;
        }
    }
}

int run(){
   //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        std::cout << "Could not create socket" << std::endl;
    }

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(asgard::get_int_value(config, "server_socket_port"));

    //Bind
    if (::bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        //print the error message
        std::perror("bind failed. Error");
        return 1;
    }

    //Listen
    listen(socket_desc, 3);

    //Accept for incoming connection
    int socket_size = sizeof(struct sockaddr_in);
    int client_socket_fd;
    while((client_socket_fd = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&socket_size))) {
        threads.push_back(std::thread(connection_handler, client_socket_fd));
    }

    if (client_socket_fd < 0) {
        std::perror("accept failed");
        return 1;
    }

    cleanup();

    return 0;
}

void terminate(int /*signo*/) {
    std::cout << "asgard: server: stopping the server" << std::endl;
    cleanup();
    abort();
}

} //end of anonymous namespace

int source_addr_from_sql(int id_sql){
    auto& source = select_source_from_sql(id_sql);
    return source.socket;
}

bool send_to_driver(int client_address, const std::string& message){
    auto nbytes = snprintf(write_buffer, 4096, "%s", message.c_str());

    if (!asgard::send_message(client_address, write_buffer, nbytes)) {
        std::perror("asgard: server: failed to send message");
        return false;
    }

    return true;
}

int main() {
    // Load the configuration file
    asgard::load_config(config);

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
