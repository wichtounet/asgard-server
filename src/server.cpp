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
#include <wiringPi.h>

#include <mongoose/Server.h>
#include <mongoose/WebController.h>

#include "CppSQLite3.h"

namespace {

const std::size_t UNIX_PATH_MAX = 108;
const std::size_t gpio_led_pin = 1;
const std::size_t socket_buffer_size = 4096;
const std::size_t max_sources = 32;

const char* socket_path = "/tmp/asgard_socket";

int socket_fd;

int nb_drivers = 0, nb_actuators = 0, nb_sensors = 0, nb_clicks = 0;

// Allocate space for the buffers
char receive_buffer[socket_buffer_size];
char write_buffer[socket_buffer_size];

struct sensor_t {
    std::size_t id;
    std::string type;
    std::string name;
    std::string data;
};

struct actuator_t {
    std::size_t id;
    std::string name;
    std::string data;
};

struct source_t {
    bool active; // TODO Remove

    std::size_t id;
    std::string name;
    std::vector<sensor_t> sensors;
    std::vector<actuator_t> actuators;

    std::size_t sensors_counter;
    std::size_t actuators_counter;
};

std::size_t current_source = 0;

std::vector<source_t> sources;

source_t& select_source(std::size_t source_id){
	for(auto& source : sources){
		if(source.id == source_id){
			return source;
		}
	}

	std::cerr << "asgard:server: Invalid request for source id " << source_id << std::endl;

	return sources.front();
}

// Create the database object
CppSQLite3DB db;

void cleanup();

void handle_command(const std::string& message, sockaddr_un& client_address, socklen_t& address_length){
    std::stringstream message_ss(message);

    std::string command;
    message_ss >> command;

    if(command == "REG_SOURCE"){
	    sources.emplace_back();

	    auto& source = sources.back();
	    source.id = current_source++;
            source.sensors_counter = 0;
            source.actuators_counter = 0;

            message_ss >> source.name;

	    // Give the source id back to the client
	    auto nbytes = snprintf(write_buffer, 4096, "%d", source.id);
	    if(sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &client_address, address_length) < 0){
                std::perror("asgard:server: failed to answer");
                return;
            }

	    std::cout << "asgard: new source registered " << source.id << " : " << source.name << std::endl;
    } else if(command == "UNREG_SOURCE"){
            int source_id;
	    message_ss >> source_id;

            sources.erase(std::remove_if(sources.begin(), sources.end(), [&](source_t& source){
                  return source.id == static_cast<std::size_t>(source_id);
            }), sources.end());

            std::cout << "asgard: unregistered source " << source_id << std::endl;
    } else if(command == "REG_SENSOR"){
            int source_id;
	    message_ss >> source_id;

            auto& source = select_source(source_id);

            source.sensors.emplace_back();
            auto& sensor  = source.sensors.back();;

            message_ss >> sensor.type;
            message_ss >> sensor.name;

            sensor.id = source.sensors_counter++;

	    // Give the sensor id back to the client
	    auto nbytes = snprintf(write_buffer, 4096, "%d", sensor.id);
	    if(sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &client_address, address_length) < 0){
                std::perror("asgard:server: failed to answer");
                return;
            }

            std::cout << "asgard: new sensor registered " << sensor.id << " (" << sensor.type << ") : " << sensor.name << std::endl;
    } else if(command == "UNREG_SENSOR"){
            int source_id;
	    message_ss >> source_id;

            int sensor_id;
	    message_ss >> sensor_id;

            auto& source = select_source(source_id);

            source.sensors.erase(std::remove_if(source.sensors.begin(), source.sensors.end(), [&](sensor_t& sensor){
                  return sensor.id == static_cast<std::size_t>(sensor_id);
            }), source.sensors.end());

            std::cout << "asgard: sensor unregistered from source " << source_id << " : " << sensor_id << std::endl;
    } else if(command == "REG_ACTUATOR"){
            int source_id;
	    message_ss >> source_id;

            auto& source = select_source(source_id);

            source.actuators.emplace_back();
            auto& actuator  = source.actuators.back();;

            message_ss >> actuator.name;

            actuator.id = source.actuators_counter++;

	    // Give the sensor id back to the client
	    auto nbytes = snprintf(write_buffer, 4096, "%d", actuator.id);
	    if(sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &client_address, address_length) < 0){
                std::perror("asgard:server: failed to answer");
                return;
            }

            std::cout << "asgard: new actuator registered " << actuator.id << " : " << actuator.name << std::endl;
    } else if(command == "UNREG_ACTUATOR"){
            int source_id;
	    message_ss >> source_id;

            int actuator_id;
	    message_ss >> actuator_id;

            auto& source = select_source(source_id);

            source.actuators.erase(std::remove_if(source.actuators.begin(), source.actuators.end(), [&](actuator_t& actuator){
                  return actuator.id == static_cast<std::size_t>(actuator_id);
            }), source.actuators.end());

            std::cout << "asgard: actuator unregistered from source " << source_id << " : " << actuator_id << std::endl;
    } else if(command == "DATA"){
            int source_id;
	    message_ss >> source_id;

            int sensor_id;
	    message_ss >> sensor_id;

	    std::string data;
	    message_ss >> data;

            auto& source = select_source(source_id);
	    auto& sensor = source.sensors[sensor_id];

	    sensor.data = data;

	    try {
		    CppSQLite3Buffer bufSQL;
		    bufSQL.format("insert into sensor_data (data, fk_sensor) values (\"%s\", %d);", data.c_str(), sensor_id+1);
		    db.execDML(bufSQL);
	    } catch (CppSQLite3Exception& e){
		    std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
	    }

	    std::cout << "asgard:server: new data: sensor(" << sensor.type << "): \"" << sensor.name << "\" : " << data << std::endl;
    } else if(command == "EVENT"){
            int source_id;
	    message_ss >> source_id;

            int actuator_id;
	    message_ss >> actuator_id;

	    std::string data;
	    message_ss >> data;

            auto& source = select_source(source_id);
	    auto& actuator = source.actuators[actuator_id];

	    actuator.data = data;

	    try {
		    CppSQLite3Buffer buffSQL;
		    if(actuator.name == "rf_button_1")
			    buffSQL.format("insert into actuator_data (data, fk_actuator) values (\"%s\", 1);", data.c_str());
		    else if(actuator.name == "ir_remote")
			    buffSQL.format("insert into actuator_data (data, fk_actuator) values (\"%s\", 2);", data.c_str());
		    db.execDML(buffSQL);
	    } catch (CppSQLite3Exception& e){
		    std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
	    }

	    std::cout << "asgard:server: new event: actuator: \"" << actuator.name << "\" : " << data << std::endl;
    }
}

int run(){
    // Create the socket
    socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if(socket_fd < 0){
        std::cerr << "socket() failed\n" << std::endl;
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
    if(::bind(socket_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0){
        std::cerr << "bind() failed\n" << std::endl;
	close(socket_fd);
        return 1;
    }

    while(true){
        sockaddr_un client_address;
        socklen_t address_length = sizeof(struct sockaddr_un);

        // Wait for one message
        auto bytes_received = recvfrom(socket_fd, receive_buffer, socket_buffer_size - 1, 0, (struct sockaddr *) &client_address, &address_length);
        receive_buffer[bytes_received] = '\0';

        // Handle the message
        handle_command(receive_buffer, client_address, address_length);
    }

    cleanup();

    return 0;
}

void db_table();
void db_insert();

void db_create(){
    try {
        db.open("test.db");

        // Create tables
	db_table();

        // Perform insertions
        db_insert();

    } catch (CppSQLite3Exception& e){
        std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }
}

void db_table(){
    db.execDML("create table if not exists pi(pk_pi integer primary key autoincrement, name char(20) unique);");
    db.execDML("create table if not exists source(pk_source integer primary key autoincrement, fk_pi integer, foreign key(fk_pi) references pi(pk_pi));");
    db.execDML("create table if not exists sensor(pk_sensor integer primary key autoincrement, type char(20), name char(20), fk_source integer, foreign key(fk_source) references source(pk_source));");
    db.execDML("create table if not exists actuator(pk_actuator integer primary key autoincrement, name char(20), fk_source integer, foreign key(fk_source) references source(pk_source));");
    db.execDML("create table if not exists sensor_data(pk_sensor_data integer primary key autoincrement, data char(20), time datetime not null default current_timestamp, fk_sensor integer, foreign key(fk_sensor) references sensor(pk_sensor));");
    db.execDML("create table if not exists actuator_data(pk_actuator_data integer primary key autoincrement, data char(20), time datetime not null default current_timestamp, fk_actuator integer, foreign key(fk_actuator) references actuator(pk_actuator));");
}

void db_insert(){
    db.execDML("insert into pi(name) select 'tyr' where not exists(select 1 from pi where name='tyr');");
    db.execDML("insert into source(fk_pi) select 1 where not exists(select 1 from source where fk_pi=1);");
    db.execDML("insert into sensor(type, name, fk_source) select 'TEMPERATURE', 'Local', 1 where not exists(select 1 from sensor where type='TEMPERATURE' and name='Local' and fk_source=1);");
    db.execDML("insert into sensor(type, name, fk_source) select 'HUMIDITY', 'Local', 1 where not exists(select 1 from sensor where type='HUMIDITY' and name='Local' and fk_source=1);");
    db.execDML("insert into sensor(type, name, fk_source) select 'TEMPERATURE', 'rf_weather_1', 1 where not exists(select 1 from sensor where type='TEMPERATURE' and name='rf_weather_1' and fk_source=1);");
    db.execDML("insert into sensor(type, name, fk_source) select 'HUMIDITY', 'rf_weather_1', 1 where not exists(select 1 from sensor where type='HUMIDITY' and name='rf_weather_1' and fk_source=1);");
    db.execDML("insert into actuator(name, fk_source) select 'rf_button_1', 1 where not exists(select 1 from actuator where name='rf_button_1' and fk_source=1);");
    db.execDML("insert into actuator(name, fk_source) select 'ir_remote', 1 where not exists(select 1 from actuator where name='ir_remote' and fk_source=1);");
}

void cleanup(){
    digitalWrite(gpio_led_pin, LOW);

    close(socket_fd);
    unlink("/tmp/asgard_socket");
}

void terminate(int /*signo*/){
    std::cout << "asgard:server: stopping the server" << std::endl;

    cleanup();

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

struct display_controller : public Mongoose::WebController {
    void display(Mongoose::Request& /*request*/, Mongoose::StreamResponse& response){
        response << "<html><head><title>Test 63</title></head><body><center><h1>Asgard - Home Automation System</h1></center><br/><h3>Current informations</h3></html>" << std::endl;
	response << "&nbsp;&nbsp;&nbsp;Drivers running : " << nb_drivers << "<br/>" << std::endl;
	response << "&nbsp;&nbsp;&nbsp;Actuators active : " << nb_actuators << "<br/>" << std::endl;
	response << "&nbsp;&nbsp;&nbsp;Sensors active : " << nb_sensors << "<br/>" << std::endl;
	response << "&nbsp;&nbsp;&nbsp;Number of clicks : " << nb_clicks << "<br/>" << std::endl;

	for(std::size_t i = 0; i < max_sources; ++i){
            source_t& source = sources[i];
            if(source.active){
                for(std::size_t sensor_id = 0; sensor_id < source.sensors.size(); ++sensor_id){
                    sensor_t& sensor = source.sensors[sensor_id];
		    if(sensor.name == "Local"){
			if(sensor.type == "TEMPERATURE"){
			    response << "<br/>*********************************************" << "<h3>Driver name : " << sensor.name << "</h3>" << std::endl;
			    response << "&nbsp;&nbsp;&nbsp;Temperature : " << sensor.data << " Celsius<br/>" << std::endl;
			} else if(sensor.type == "HUMIDITY")
			    response << "&nbsp;&nbsp;&nbsp;Air humidity : " << sensor.data << " %<br/>" << std::endl;
		    } else if(sensor.name == "rf_weather_1"){
			if(sensor.type == "TEMPERATURE"){
			    response << "<br/>*********************************************" << "<h3>Driver name : " << sensor.name << "</h3>" << std::endl;
			    response << "&nbsp;&nbsp;&nbsp;Temperature : " << sensor.data << " Celsius<br/>" << std::endl;
			} else if(sensor.type == "HUMIDITY")
			    response << "&nbsp;&nbsp;&nbsp;Air humidity : " << sensor.data << " %<br/>" << std::endl;
		    }
                }

		for(std::size_t actuator_id = 0; actuator_id < source.actuators.size(); ++actuator_id){
                    actuator_t& actuator = source.actuators[actuator_id];
		    if(actuator.name == "ir_remote"){
			response << "<br/>*********************************************" << "<h3>Driver name : " << actuator.name << "</h3>" << std::endl;
			response << "&nbsp;&nbsp;&nbsp;Last input : " << actuator.data << "<br/>" << std::endl;
		    }
                }
            }
        }

	/* TO IMPLEMENT
	try {
	    CppSQLite3Query p = db.execQuery("select * from sensor order by 1;");
            std::string last_value_1;
            while (!p.eof()){
		if(p.fieldValue(2) != last_value_1){
		    last_value_1 = p.fieldValue(2);
		    response << "<br/>*********************************************" << "<h3>Driver name : " << last_value_1 << "</h3>" << std::endl;
		}
		CppSQLite3Query q = db.execQuery("select * from sensor_data order by 1;");
		std::string last_value_2;
		while (!q.eof()){
		    last_value_2 = q.fieldValue(1);
		    q.nextRow();
		}
		response << "&nbsp;&nbsp;&nbsp;Last Data : " << last_value_2 << "<br/>" << std::endl;
		p.nextRow();
            }
	    CppSQLite3Query r = db.execQuery("select * from sensor order by 1;");
	    std::string last_value_3;
            while (!r.eof()){
		if(r.fieldValue(2) != last_value_3){
		    last_value_3 = r.fieldValue(2);
		    response << "<br/>*********************************************" << "<h3>Driver name : " << last_value_3 << "</h3>" << std::endl;
		}
		CppSQLite3Query s = db.execQuery("select * from sensor_data order by 1;");
		std::string last_value_4;
		while (!s.eof()){
		    last_value_4 = s.fieldValue(1);
		    s.nextRow();
		}
		response << "&nbsp;&nbsp;&nbsp;Last input : " << last_value_4 << "<br/>" << std::endl;
		r.nextRow();
            }
	} catch (CppSQLite3Exception& e){
            std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
        }*/
    }

    //This will be called automatically
    void setup(){
        addRoute<display_controller>("GET", "/display", &display_controller::display);
    }
};

} //end of anonymous namespace

int main(){
    //Run the wiringPi setup (as root)
    wiringPiSetup();

    //Drop root privileges and run as pi:pi again
    if(!revoke_root()){
       std::cout << "asgard: unable to revoke root privileges, exiting..." << std::endl;
       return 1;
    }

    // Open (connect) the database
    db_create();

    // Create the controller handling the requests
    display_controller controller;

    // Run the server with our controller
    Mongoose::Server server(8080);
    server.registerController(&controller);

    // Start the server and wait forever
    server.start();

    //Register signals for "proper" shutdown
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);

    pinMode(gpio_led_pin, OUTPUT);
    digitalWrite(gpio_led_pin, HIGH);

    return run();
}
