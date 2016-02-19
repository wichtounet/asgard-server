
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

int socket_fd, nb_drivers = 0, nb_actuators = 0, nb_sensors = 0;

// Allocate space for the buffers
char receive_buffer[socket_buffer_size];
char write_buffer[socket_buffer_size];

struct sensor_t {
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
    
    std::cerr << "asgard: server: Invalid request for source id " << source_id << std::endl;

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
	nb_drivers++;
	sources.emplace_back();

	auto& source = sources.back();
	source.id = current_source++;
        source.sensors_counter = 0;
        source.actuators_counter = 0;

        message_ss >> source.name;

	// Give the source id back to the client
	auto nbytes = snprintf(write_buffer, 4096, "%d", source.id);
	if(sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr *) &client_address, address_length) < 0){
            std::perror("asgard: server: failed to answer");
            return;
        }

	try {
	    CppSQLite3Buffer buffSQL;
	    buffSQL.format("insert into source(name,fk_pi) select \"%s\",1 where not exists(select 1 from source where name=\"%s\");", source.name.c_str(), source.name.c_str());
	    db.execDML(buffSQL);
	} catch (CppSQLite3Exception& e){
	    std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
	}

	 std::cout << "asgard: new source registered " << source.id << " : " << source.name << std::endl;
    } else if(command == "UNREG_SOURCE"){
	nb_drivers--;
        int source_id;
	message_ss >> source_id;

        sources.erase(std::remove_if(sources.begin(), sources.end(), [&](source_t& source){
             return source.id == static_cast<std::size_t>(source_id);
        }), sources.end());

        std::cout << "asgard: unregistered source " << source_id << std::endl;
    } else if(command == "REG_SENSOR"){
	nb_sensors++;
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
            std::perror("asgard: server: failed to answer");
            return;
        }

	try {
	    CppSQLite3Buffer buffSQL;
	    if(sensor.name=="local"){
	    	int sensor_pk = db.execScalar("select pk_source from source where name=\"dht11\";");
	    	buffSQL.format("insert into sensor(type,name,fk_source) select \"%s\",'local',%d where not exists(select 1 from sensor where type=\"%s\" and name='local');", sensor.type.c_str(), sensor_pk, sensor.type.c_str());
	    } else if(sensor.name=="rf_weather"){
	    	int sensor_pk = db.execScalar("select pk_source from source where name=\"rf\";");
	    	buffSQL.format("insert into sensor(type,name,fk_source) select \"%s\",'rf_weather',%d where not exists(select 1 from sensor where type=\"%s\" and name='rf_weather');", sensor.type.c_str(), sensor_pk, sensor.type.c_str());
	    }
	    db.execDML(buffSQL);
	} catch (CppSQLite3Exception& e){
	    std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
	}

        std::cout << "asgard: new sensor registered " << sensor.id << " (" << sensor.type << ") : " << sensor.name << std::endl;
    } else if(command == "UNREG_SENSOR"){
	nb_sensors--;
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
	nb_actuators++;
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
            std::perror("asgard: server: failed to answer");
            return;
        }

	try {
	    CppSQLite3Buffer buffSQL;
	    if(actuator.name=="rf_button_1"){
	    	int actuator_pk = db.execScalar("select pk_source from source where name=\"rf\";");
	    	buffSQL.format("insert into actuator(name,fk_source) select 'rf_button_1',%d where not exists(select 1 from actuator where name='rf_button_1');", actuator_pk);
	    } else if(actuator.name=="ir_button_1"){
	    	int actuator_pk = db.execScalar("select pk_source from source where name=\"ir\";");
	    	buffSQL.format("insert into actuator(name,fk_source) select 'ir_button_1',%d where not exists(select 1 from actuator where name='ir_button_1');", actuator_pk);
	    }
	    db.execDML(buffSQL);
	} catch (CppSQLite3Exception& e){
	    std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
	}

        std::cout << "asgard: new actuator registered " << actuator.id << " : " << actuator.name << std::endl;
    } else if(command == "UNREG_ACTUATOR"){
	nb_actuators--;
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

	try {
	    CppSQLite3Buffer buffSQL;
	    buffSQL.format("select pk_sensor from sensor where name=\"%s\" and type=\"%s\";", sensor.name.c_str(), sensor.type.c_str());
	    int sensor_pk = db.execScalar(buffSQL);
	    CppSQLite3Buffer bufSQL;
	    bufSQL.format("insert into sensor_data (data, fk_sensor) values (\"%s\", %d);", data.c_str(), sensor_pk);
	    db.execDML(bufSQL);
	} catch (CppSQLite3Exception& e){
	    std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
	}

	std::cout << "asgard: server: new data: sensor(" << sensor.type << "): \"" << sensor.name << "\" : " << data << std::endl;
    } else if(command == "EVENT"){
        int source_id;
	message_ss >> source_id;

        int actuator_id;
	message_ss >> actuator_id;

	std::string data;
	message_ss >> data;

        auto& source = select_source(source_id);
	auto& actuator = source.actuators[actuator_id];

	try {
	    CppSQLite3Buffer buffSQL;
	    buffSQL.format("select pk_actuator from actuator where name=\"%s\";", actuator.name.c_str());
	    int actuator_pk = db.execScalar(buffSQL);
	    CppSQLite3Buffer bufSQL;
	    bufSQL.format("insert into actuator_data (data, fk_actuator) values (\"%s\", %d);", data.c_str(), actuator_pk);
	    db.execDML(bufSQL);
	} catch (CppSQLite3Exception& e){
	    std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
	}

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
    if(::bind(socket_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0){
        std::cerr << "bind() failed" << std::endl;
	close(socket_fd);
        return 1;
    }

    while(true){
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

void db_table();
void db_insert();

void db_create(){
    try {
        db.open("test.db");

        // Create tables
	db_table();

        // Perform pi insertion
    	db.execDML("insert into pi(name) select 'tyr' where not exists(select 1 from pi where name='tyr');");

    } catch (CppSQLite3Exception& e){
        std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }
}

void db_table(){
    db.execDML("create table if not exists pi(pk_pi integer primary key autoincrement, name char(20) unique);");
    db.execDML("create table if not exists source(pk_source integer primary key autoincrement, name char(20) unique, fk_pi integer, foreign key(fk_pi) references pi(pk_pi));");
    db.execDML("create table if not exists sensor(pk_sensor integer primary key autoincrement, type char(20), name char(20), fk_source integer, foreign key(fk_source) references source(pk_source));");
    db.execDML("create table if not exists actuator(pk_actuator integer primary key autoincrement, name char(20), fk_source integer, foreign key(fk_source) references source(pk_source));");
    db.execDML("create table if not exists sensor_data(pk_sensor_data integer primary key autoincrement, data char(20), time datetime not null default current_timestamp, fk_sensor integer, foreign key(fk_sensor) references sensor(pk_sensor));");
    db.execDML("create table if not exists actuator_data(pk_actuator_data integer primary key autoincrement, data char(20), time datetime not null default current_timestamp, fk_actuator integer, foreign key(fk_actuator) references actuator(pk_actuator));");
}

void cleanup(){
    digitalWrite(gpio_led_pin, LOW);

    close(socket_fd);
    unlink("/tmp/asgard_socket");
}

void terminate(int /*signo*/){
    std::cout << "asgard: server: stopping the server" << std::endl;

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

std::string header = R"=====(
<!DOCTYPE html>
<html>
<head>
<title>Test</title>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta http-equiv="refresh" content="30">
<script src="https://ajax.googleapis.com/ajax/libs/jquery/1.12.0/jquery.min.js"></script>
<script src="https://code.highcharts.com/highcharts.js"></script>
<script src="https://code.highcharts.com/modules/exporting.js"></script>
</head>
<body>
<center>
<h1>Asgard - Home Automation System</h1>
</center><br/>
<h3>Current informations</h3>
<div id="container" style="min-width: 310px; height: 400px; margin: 0 auto"></div>
<script>
function addZero(i) {
  if (i < 10) {
    i = "0" + i;
  }
  return i;
}
var currentdate = new Date();
var datetime = [];
for (var i = 0; i < 12; ++i) {
  datetime[i] = addZero(currentdate.getDate()) + "-" + addZero(currentdate.getMonth() + 1) + "-" + addZero(currentdate.getFullYear()) + " " + addZero(currentdate.getHours()-i) + ":" + addZero(currentdate.getMinutes()) + ":" + addZero(currentdate.getSeconds());
}

$(function() {
  $('#container').highcharts({
    title: {
      text: 'Daily Average Temperature',
      x: -20 //center
    },
    subtitle: {
      text: 'Last 12 hours from ' + datetime[0],
      x: -20
    },
    xAxis: {
      categories: [datetime[11], datetime[10], datetime[9], datetime[8], datetime[7], datetime[6], datetime[5], datetime[4], datetime[3], datetime[2], datetime[1], datetime[0]],
      labels: {
       	enabled: false
      },
      tickLength: 0
    },
    yAxis: {
      title: {
        text: 'Temperature (°C)'
      },
      plotLines: [{
        value: 0,
        width: 1,
        color: '#808080'
      }]
    },
    tooltip: {
      valueSuffix: '°C'
    },
    legend: {
      layout: 'vertical',
      align: 'right',
      verticalAlign: 'middle',
      borderWidth: 0
    },
    series: [{
      name: 'local',
      data: [7.0, 6.9, 9.5, 14.5, 18.2, 21.5, 25.2, 26.5, 23.3, 18.3, 13.9, 9.6]
    }, {
      name: 'rf_weather',
      data: [1.8, 3.4, 5.7, 11.3, 17.0, 22.0, 24.8, 24.1, 20.1, 14.1, 8.6, 2.5]
    }]
  });
});

</script>
)=====";

struct display_controller : public Mongoose::WebController {
    void display(Mongoose::Request& /*request*/, Mongoose::StreamResponse& response){
        response << header << std::endl;
	response << "&nbsp;&nbsp;&nbsp;Drivers running : " << nb_drivers << "<br/>" << std::endl;
	response << "&nbsp;&nbsp;&nbsp;Actuators active : " << nb_actuators << "<br/>" << std::endl;
	response << "&nbsp;&nbsp;&nbsp;Sensors active : " << nb_sensors << "<br/>" << std::endl;

	try {
	    CppSQLite3Query sensor_name = db.execQuery("select pk_sensor,name,type from sensor order by name;");
            int last_sensor_pk;
            std::string last_sensor_name;
            std::string last_sensor_type;
            while (!sensor_name.eof()){
	    	last_sensor_pk = sensor_name.getIntField(0);
	    	last_sensor_name = sensor_name.fieldValue(1);
	    	last_sensor_type = sensor_name.fieldValue(2);
		CppSQLite3Buffer buffSQL;
            	buffSQL.format("select data from sensor_data where fk_sensor=%d", last_sensor_pk);
            	std::string query_result(buffSQL);
	    	CppSQLite3Query sensor_data = db.execQuery(query_result.c_str());
            	std::string last_sensor_data;
            	while (!sensor_data.eof()){
	   	    last_sensor_data = sensor_data.fieldValue(0);
            	    sensor_data.nextRow();
            	}
		if(last_sensor_type=="TEMPERATURE"){
		    response << "<br/>*********************************************" << "<h3>Driver name : " << last_sensor_name << "</h3>" << std::endl;
		    response << "&nbsp;&nbsp;&nbsp;Temperature : " << last_sensor_data << " °C<br/>" << std::endl;
		} else if(last_sensor_type=="HUMIDITY"){
		    response << "&nbsp;&nbsp;&nbsp;Air humidity : " << last_sensor_data << " %<br/>" << std::endl;
		} else {
		    response << "&nbsp;&nbsp;&nbsp;Other : " << last_sensor_data << " %<br/>" << std::endl;
		}
            	sensor_name.nextRow();
            }

	    CppSQLite3Query actuator_name = db.execQuery("select pk_actuator,name from actuator order by name;");
	    int last_actuator_pk;
	    std::string last_actuator_name;
            while (!actuator_name.eof()){
	    	last_actuator_pk = actuator_name.getIntField(0);
	    	last_actuator_name = actuator_name.fieldValue(1);
		response << "<br/>*********************************************" << "<h3>Driver name : " << last_actuator_name << "</h3>" << std::endl;
	    	if(last_actuator_name=="ir_button_1"){
	    	    CppSQLite3Buffer buffSQL;
            	    buffSQL.format("select data from actuator_data where fk_actuator=%d", last_actuator_pk);
            	    std::string query_result(buffSQL);
	    	    CppSQLite3Query actuator_data = db.execQuery(query_result.c_str());
            	    std::string last_actuator_data;
            	    while (!actuator_data.eof()){
	    	   	last_actuator_data = actuator_data.fieldValue(0);
            	  	actuator_data.nextRow();
            	    }
		    response << "&nbsp;&nbsp;&nbsp;Last input : " << last_actuator_data << "<br/>" << std::endl;
	    	} else if (last_actuator_name=="rf_button_1") {
		    CppSQLite3Buffer buffSQL;
            	    buffSQL.format("select count(data) from actuator_data where fk_actuator=%d", last_actuator_pk);
	    	    int nbClicks = db.execScalar(buffSQL);
		    response << "&nbsp;&nbsp;&nbsp;Number of clicks : " << nbClicks << "<br/>" << std::endl;
	    	}
            	actuator_name.nextRow();
            }
	} catch (CppSQLite3Exception& e){
            std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
        }
		    response << "</html>" << std::endl;
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


