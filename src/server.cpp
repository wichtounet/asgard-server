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

#ifdef __RPI__
#include <wiringPi.h>
#endif

#include <mongoose/Server.h>
#include <mongoose/WebController.h>

#include "CppSQLite3.h"

namespace {

const std::size_t UNIX_PATH_MAX = 108;
const std::size_t gpio_led_pin = 1;
const std::size_t socket_buffer_size = 4096;
const std::size_t max_sources = 32;

const char* socket_path = "/tmp/asgard_socket";
const std::vector<size_t> interval{1, 24, 48};

int socket_fd;

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

    std::size_t id_sql;
    std::size_t sensors_counter;
    std::size_t actuators_counter;
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

// Create the database object
CppSQLite3DB db;

void cleanup();

template<typename... T>
void db_exec_dml(const std::string& query, T... args){
    try {
        CppSQLite3Buffer buffSQL;
        buffSQL.format(query.c_str(), args...);
        db.execDML(buffSQL);
    } catch (CppSQLite3Exception& e) {
        std::cerr << "asgard: SQL Query failed: " << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }
}

template<typename... T>
int db_exec_scalar(const std::string& query, T... args){
    try {
        CppSQLite3Buffer buffSQL;
        buffSQL.format(query.c_str(), args...);
        return db.execScalar(buffSQL);
    } catch (CppSQLite3Exception& e) {
        std::cerr << "asgard: SQL Query failed: " << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }

    return -1;
}

template<typename... T>
CppSQLite3Query db_exec_query(const std::string& query, T... args){
    try {
        CppSQLite3Buffer buffSQL;
        buffSQL.format(query.c_str(), args...);
        return db.execQuery(buffSQL);
    } catch (CppSQLite3Exception& e) {
        std::cerr << "asgard: SQL Query failed: " << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }

    return {};
}

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

        message_ss >> source.name;

        // Give the source id back to the client
        auto nbytes = snprintf(write_buffer, 4096, "%d", source.id);
        if (sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr*)&client_address, address_length) < 0) {
            std::perror("asgard: server: failed to answer");
            return;
        }

        try {
            CppSQLite3Buffer buffSQL;
            buffSQL.format("insert into source(name,fk_pi) select \"%s\", 1 where not exists(select 1 from source where name=\"%s\");",
                           source.name.c_str(), source.name.c_str());
            db.execDML(buffSQL);
            buffSQL.format("select pk_source from source where name=\"%s\";", source.name.c_str());
            source.id_sql = db.execScalar(buffSQL);
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
        auto nbytes = snprintf(write_buffer, 4096, "%d", sensor.id);
        if (sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr*)&client_address, address_length) < 0) {
            std::perror("asgard: server: failed to answer");
            return;
        }

        db_exec_dml(
            "insert into sensor(type, name, fk_source) select \"%s\", \"%s\","
            "%d where not exists(select 1 from sensor where type=\"%s\" and name=\"%s\");"
            , sensor.type.c_str(), sensor.name.c_str(), source.id_sql, sensor.type.c_str(), sensor.name.c_str());

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
    } else if (command == "REG_ACTUATOR") {
        int source_id;
        message_ss >> source_id;

        auto& source = select_source(source_id);

        source.actuators.emplace_back();
        auto& actuator = source.actuators.back();

        message_ss >> actuator.name;

        actuator.id = source.actuators_counter++;

        // Give the sensor id back to the client
        auto nbytes = snprintf(write_buffer, 4096, "%d", actuator.id);
        if (sendto(socket_fd, write_buffer, nbytes, 0, (struct sockaddr*)&client_address, address_length) < 0) {
            std::perror("asgard: server: failed to answer");
            return;
        }

        db_exec_dml("insert into actuator(name, fk_source) select \"%s\", %d where not exists(select 1 from actuator where name=\"%s\");"
                      , actuator.name.c_str(), source.id_sql, actuator.name.c_str());

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

        int sensor_pk = db_exec_scalar("select pk_sensor from sensor where name=\"%s\" and type=\"%s\";", sensor.name.c_str(), sensor.type.c_str());

        db_exec_dml("insert into sensor_data (data, fk_sensor) values (\"%s\", %d);", data.c_str(), sensor_pk);

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

        int actuator_pk = db_exec_scalar("select pk_actuator from actuator where name=\"%s\";", actuator.name.c_str());

        db_exec_dml("insert into actuator_data (data, fk_actuator) values (\"%s\", %d);", data.c_str(), actuator_pk);

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

void db_table() {
    db.execDML("create table if not exists pi(pk_pi integer primary key autoincrement, name char(20) unique);");
    db.execDML(
        "create table if not exists source(pk_source integer primary key autoincrement,"
        "name char(20) unique, fk_pi integer, foreign key(fk_pi) references pi(pk_pi));");
    db.execDML(
        "create table if not exists sensor(pk_sensor integer primary key autoincrement, type char(20),"
        "name char(20), fk_source integer, foreign key(fk_source) references source(pk_source));");
    db.execDML(
        "create table if not exists actuator(pk_actuator integer primary key autoincrement,"
        "name char(20), fk_source integer, foreign key(fk_source) references source(pk_source));");
    db.execDML(
        "create table if not exists sensor_data(pk_sensor_data integer primary key autoincrement, data char(20),"
        "time datetime not null default current_timestamp, fk_sensor integer, foreign key(fk_sensor) references sensor(pk_sensor));");
    db.execDML(
        "create table if not exists actuator_data(pk_actuator_data integer primary key autoincrement, data char(20),"
        "time datetime not null default current_timestamp, fk_actuator integer, foreign key(fk_actuator) references actuator(pk_actuator));");
}

void db_create() {
    try {
        db.open("asgard.db");

        // Create tables
        db_table();

        // Perform pi insertion
        db.execDML("insert into pi(name) select 'tyr' where not exists(select 1 from pi where name='tyr');");
    } catch (CppSQLite3Exception& e) {
        std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }
}

void set_led_off() {
#ifdef __RPI__
    digitalWrite(gpio_led_pin, LOW);
#endif
}

void set_led_on() {
#ifdef __RPI__
    digitalWrite(gpio_led_pin, HIGH);
#endif
}

void init_led() {
#ifdef __RPI__
    pinMode(gpio_led_pin, OUTPUT);
#endif
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

bool revoke_root() {
    if (getuid() == 0) {
        if (setgid(1000) != 0) {
            std::cout << "asgard: setgid: Unable to drop group privileges: " << strerror(errno) << std::endl;
            return false;
        }
        if (setuid(1000) != 0) {
            std::cout << "asgard: setgid: Unable to drop user privileges: " << strerror(errno) << std::endl;
            return false;
        }
    }

    if (setuid(0) != -1) {
        std::cout << "asgard: managed to regain root privileges, exiting..." << std::endl;
        return false;
    }

    return true;
}

std::string header = R"=====(
<!DOCTYPE html>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<title>Asgard - Home Automation System</title>
<link rel="stylesheet" href="https://ajax.googleapis.com/ajax/libs/jqueryui/1.11.4/themes/smoothness/jquery-ui.css">
<script src="https://ajax.googleapis.com/ajax/libs/jquery/1.12.0/jquery.min.js"></script>
<script src="https://ajax.googleapis.com/ajax/libs/jqueryui/1.11.4/jquery-ui.min.js"></script>
<script src="https://code.highcharts.com/highcharts.js"></script>
<script src="https://code.highcharts.com/modules/exporting.js"></script>
<style type="text/css">
div{margin: 0 auto;}
p{padding: 10px 0px 0px 20px; font-weight: bold;}
ul.led li, ul.menu li{list-style: none; cursor: pointer; border: 1px solid gray; padding: 10px 0px 10px 0px; background-color: lightgray; font-weight: bold;}
ul.menu li{background: url(http://icongal.com/gallery/image/57586/right_monotone_arrow_next_play.png)
center right no-repeat; background-size: 30px 30px; background-color: lightgray; padding: 10px 0px 10px 10px;}
ul.menu li:first-child, ul.led li:first-child{border-radius: 10px 10px 0px 0px;}
ul.menu li:last-child, ul.led li:last-child{border-radius: 0px 0px 10px 10px;}
ul.menu li:first-child:last-child{border-radius: 10px;}
.title{padding: 8px 0px 8px 10px !important;}
.tabs{width: 720px; margin-top: 20px; border: 1px solid black;}
.myTabs{float: right !important; font-size: 14px;}
.menu, .led{padding: 0px 10px 0px 10px;}
.button{text-align: center;}
#header{background-color: lightgray; opacity: 0.8; width: 1020px; height: 65px; margin-top: 20px;
border-radius: 5px 5px 0px 0px; border: solid black; border-width: 1px 1px 0px 1px;}
#container{width: 1000px; padding-right: 10px; padding-bottom: 10px; padding-left: 10px; border: 1px solid black; border-radius: 0px 0px 5px 5px; overflow: hidden;}
#sidebar{float: left; width: 240px;}
#main{float: right;}
#footer{text-align: right; width: 1000px; margin-top: 30px; margin-bottom: 30px; font-size: 14px;}
</style>
<script>
$( function() {
    $(".tabs").tabs();
    $('a[data-toggle="tab"]').on('click', function (e) {
        var selector = $(this.getAttribute("href"));
        var chart = $(selector).highcharts();
        chart.reflow();
    });
});
function load_menu(name) {
    $('.hideable').hide();
    $('.' + name).show();
}
</script>
</head>
<body>
)=====";

struct display_controller : public Mongoose::WebController {
    void display_menu(Mongoose::StreamResponse& response) {
        response << "<ul class=\"menu\"><li onclick=\"load_menu('hideable')\">Show All</li></ul>" << std::endl
                 << "<p>Drivers registered :</p>" << std::endl
                 << "<ul class=\"menu\">" << std::endl;

        CppSQLite3Query source_query = db.execQuery("select name, pk_source from source order by name;");

        while (!source_query.eof()) {
            std::string source_name = source_query.fieldValue(0);
            std::string source_pk = source_query.fieldValue(1);
            response << "<li onclick=\"load_source('" << source_pk << "')\">" << source_name << "</li>" << std::endl;
            source_query.nextRow();
        }

        response << "</ul>" << std::endl
                 << "<p>Sensors active :</p>" << std::endl
                 << "<ul class=\"menu\">" << std::endl;

        CppSQLite3Query sensor_query = db.execQuery("select distinct name from sensor order by name;");

        while (!sensor_query.eof()) {
            std::string sensor_name = sensor_query.fieldValue(0);
            response << "<li onclick=\"load_menu('" << sensor_name << "')\">" << sensor_name << "</li>" << std::endl;
            sensor_query.nextRow();
        }

        response << "</ul>" << std::endl
                 << "<p>Actuators active :</p>" << std::endl
                 << "<ul class=\"menu\">" << std::endl;

        CppSQLite3Query actuator_query = db.execQuery("select name from actuator order by name;");

        while (!actuator_query.eof()) {
            std::string actuator_name = actuator_query.fieldValue(0);
            response << "<li onclick=\"load_menu('" << actuator_name << "')\">" << actuator_name << "</li>" << std::endl;
            actuator_query.nextRow();
        }

        response << "</ul></div>" << std::endl
                 << "<div class=\"tabs\" style=\"width: 240px;\"><ul><li class=\"title\">Onboard LED</li></ul>" << std::endl
                 << "<ul class=\"led\"><li class=\"button\" onclick=\"location.href='/led_on'\">ON</li>" << std::endl
                 << "<li class=\"button\" onclick=\"location.href='/led_off'\">OFF</li></ul>" << std::endl
                 << "</div></div>" << std::endl
                 << "<div id=\"main\">" << std::endl;
    }

    void display_sensors(Mongoose::StreamResponse& response) {
        CppSQLite3Query sensor_query = db.execQuery("select name, type from sensor order by name;");
        std::string sensor_name;
        std::string sensor_type;

        while (!sensor_query.eof()) {
            sensor_name = sensor_query.fieldValue(0);
            sensor_type = sensor_query.fieldValue(1);

            std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);

            std::string url_data   = sensor_name + "/" + sensor_type + "/data";
            std::string url_script = sensor_name + "/" + sensor_type + "/script";

            response << "<div id=\"" << sensor_name << "_" << sensor_type << "\" class=\"hideable " << sensor_name << "\"></div>" << std::endl
                     << "<script> $(function() {" << std::endl

                     << "$(\"#" << sensor_name << "_" << sensor_type << "\").load(\"/" << url_data << "\", function(){"
                     << "$.ajaxSetup({ cache: false });"
                     << "$.getScript(\"" << url_script << "\");"
                     << "});" << std::endl

                     << "setInterval(function() {" << std::endl

                     << "$(\"#" << sensor_name << "_" << sensor_type << "\").load(\"/" << url_data << "\", function(){"
                     << "$.ajaxSetup({ cache: false });"
                     << "$.getScript(\"" << url_script << "\");"
                     << "});" << std::endl

                     << "}, 10000);" << std::endl

                     << "})</script>" << std::endl;

            sensor_query.nextRow();
        }
    }

    void display_actuators(Mongoose::StreamResponse& response) {
        CppSQLite3Query actuator_query = db.execQuery("select distinct name from actuator order by name;");

        while (!actuator_query.eof()) {
            std::string actuator_name = actuator_query.fieldValue(0);

            std::string url_data   = actuator_name + "/data";
            std::string url_script = actuator_name + "/script";

            response << "<div id=\"" << actuator_name << "_script\" class=\"hideable " << actuator_name << "\"></div>" << std::endl
                     << "<script> $(function() {" << std::endl

                     << "$(\"#" << actuator_name << "_script\").load(\"/" << url_data << "\", function(){"
                     << "$.ajaxSetup({ cache: false });"
                     << "$.getScript(\"" << url_script << "\");"
                     << "});" << std::endl

                     << "setInterval(function() {" << std::endl

                     << "$(\"#" << actuator_name << "_script\").load(\"/" << url_data << "\", function(){"
                     << "$.ajaxSetup({ cache: false });"
                     << "$.getScript(\"" << url_script << "\");"
                     << "});" << std::endl

                     << "}, 10000);" << std::endl

                     << "})</script>" << std::endl;

            actuator_query.nextRow();
        }
    }

    void display(Mongoose::Request& /*request*/, Mongoose::StreamResponse& response){
        response << header << std::endl
                 << "<div id=\"header\"><center><h2>Asgard - Home Automation System</h2></center></div>" << std::endl
                 << "<div id=\"container\"><div id=\"sidebar\"><div class=\"tabs\" style=\"width: 240px;\">" << std::endl
                 << "<ul><li class=\"title\">Current information</li></ul>" << std::endl;

        try {
            response << "<script>function load_source(pk) {" << std::endl;
            response << "$('.hideable').hide();" << std::endl;

            CppSQLite3Query sensor_query = db.execQuery("select distinct name, fk_source from sensor order by name;");

            while (!sensor_query.eof()) {
                std::string sensor_name = sensor_query.fieldValue(0);
                int sensor_fk = sensor_query.getIntField(1);
                response << "if (pk == " << sensor_fk << ") { $('." << sensor_name << "').show(); }" << std::endl;
                sensor_query.nextRow();
            }

            CppSQLite3Query actuator_query = db.execQuery("select distinct name, fk_source from actuator order by name;");

            while (!actuator_query.eof()) {
                std::string actuator_name = actuator_query.fieldValue(0);
                int actuator_fk = actuator_query.getIntField(1);
                response << "if (pk == " << actuator_fk << ") { $('." << actuator_name << "').show(); }" << std::endl;
                actuator_query.nextRow();
            }
            response << "}" << "</script>" << std::endl;

            display_menu(response);
            display_sensors(response);
            display_actuators(response);

        } catch (CppSQLite3Exception& e) {
            std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
        }

        response << "</div></div>" << std::endl
                 << "<div id=\"footer\">© 2015-2016 Asgard Team. All Rights Reserved.</div></body></html>" << std::endl;
    }

    void led_on(Mongoose::Request& request, Mongoose::StreamResponse& response) {
        set_led_on();
        display(request, response);
    }

    void led_off(Mongoose::Request& request, Mongoose::StreamResponse& response) {
        set_led_off();
        display(request, response);
    }

    void sensor_data(Mongoose::Request& request, Mongoose::StreamResponse& response) {
        std::string url = request.getUrl();

        auto start_name = 1;
        auto end_name = url.find("/", 1);

        auto start_type = end_name + 1;
        auto end_type = url.find("/", start_type);

        std::string sensor_name(url.begin() + start_name, url.begin() + end_name);
        std::string sensor_type(url.begin() + start_type, url.begin() + end_type);

        std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::toupper);

        int sensor_pk = db_exec_scalar("select pk_sensor from sensor where name=\"%s\" and type=\"%s\";", sensor_name.c_str(), sensor_type.c_str());

        std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);

        sensor_type[0] = toupper(sensor_type[0]);
        CppSQLite3Query sensor_query = db_exec_query("select data from sensor_data where fk_sensor=%d order by time desc limit 1;", sensor_pk);

        if (!sensor_query.eof()) {
            std::string sensor_data = sensor_query.fieldValue(0);

            auto div_id = sensor_name + sensor_type;

            if (sensor_type == "Temperature" || sensor_type == "Humidity") {
                response << "<div id=\"" << div_id << "\" class=\"tabs\"><ul><li class=\"title\">Sensor name : "
                         << sensor_name << " (" << sensor_type << ")</li>" << std::endl;

                for (size_t i = 0; i < interval.size(); ++i) {
                    response << "<li class=\"myTabs\"><a href=\"#" << sensor_name << sensor_type << i
                             << "\" data-toggle=\"tab\">" << interval[i] << "h </a></li>" << std::endl;
                }

                response << "</ul>" << std::endl;

                if (sensor_type == "Temperature") {
                    response << "<ul><li>Current Temperature : " << sensor_data << "°C</li></ul>" << std::endl;
                } else if (sensor_type == "Humidity") {
                    response << "<ul><li>Current Air Humidity : " << sensor_data << "%</li></ul>" << std::endl;
                }

                for (size_t i = 0; i < interval.size(); ++i) {
                    response << "<div id=\"" << div_id << i << "\" style=\"width: 680px; height: 240px\"></div>" << std::endl;
                }
            } else {
                response << "<div id=\"" << div_id << "\" class=\"tabs\"><ul><li class=\"title\">Sensor name : "
                         << sensor_name << " (" << sensor_type << ")</li></ul>"
                         << "<ul><li>Last Value : " << sensor_data << "</li>" << std::endl;

                int nbValue = db_exec_scalar("select count(data) from sensor_data where fk_sensor=%d;", sensor_pk);
                response << "<li>Number of Values : " << nbValue << "</li></ul>" << std::endl;
            }
            response << "</div>" << std::endl;
        }
    }

    void sensor_script(Mongoose::Request& request, Mongoose::StreamResponse& response) {
        std::string url = request.getUrl();

        auto start_name = 1;
        auto end_name = url.find("/", 1);

        auto start_type = end_name + 1;
        auto end_type = url.find("/", start_type);

        std::string sensor_name(url.begin() + start_name, url.begin() + end_name);
        std::string sensor_type(url.begin() + start_type, url.begin() + end_type);

        std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::toupper);

        int sensor_pk = db_exec_scalar("select pk_sensor from sensor where name=\"%s\" and type=\"%s\";", sensor_name.c_str(), sensor_type.c_str());

        std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);

        sensor_type[0] = toupper(sensor_type[0]);
        CppSQLite3Query sensor_query = db_exec_query("select data from sensor_data where fk_sensor=%d order by time desc limit 1;", sensor_pk);

        if (!sensor_query.eof()) {
            std::string sensor_data = sensor_query.fieldValue(0);

            auto div_id = sensor_name + sensor_type;

            if (sensor_type == "Temperature" || sensor_type == "Humidity") {
                response << "$('#" << div_id  << "').tabs();" << std::endl;

                for (size_t i = 0; i < interval.size(); ++i) {
                    response << "$('#" << div_id << i
                             << "').highcharts({chart: {marginBottom: 60}, title: {text: ''}, xAxis: {categories: [";

                    CppSQLite3Query sensor_interval = db_exec_query(
                        "select time from sensor_data where time > datetime('now', '-%d hours') and fk_sensor=%d order by time;", interval[i], sensor_pk);

                    std::string sensor_time;

                    while (!sensor_interval.eof()) {
                        sensor_time = sensor_interval.fieldValue(0);
                        response << "\"" << sensor_time << "\"" << ",";
                        sensor_interval.nextRow();
                    }

                    response << "], labels: {enabled: false}}, subtitle: {text: '" << sensor_name << " - last " << interval[i] << " hours from " << sensor_time
                             << "', verticalAlign: 'bottom', y: -5}, yAxis: {min: 0, title: {text: '" << sensor_type;

                    if (sensor_type == "Temperature") {
                        response << " (°C)'";
                    } else if (sensor_type == "Humidity") {
                        response << " (%)'";
                    }

                    response << "}}, plotOptions: {line: {animation: false}}, exporting: {enabled: false}, credits: {enabled: false}, tooltip: {valueSuffix: '";

                    if (sensor_type == "Temperature") {
                        response << "°C'";
                    } else if (sensor_type == "Humidity") {
                        response << "%'";
                    }

                    response << "}, series: [{showInLegend: false, name: '" << sensor_name << "', data: [";

                    sensor_query = db_exec_query(
                        "select data from sensor_data where time > datetime('now', '-%d hours') and fk_sensor=%d order by time;", interval[i], sensor_pk);

                    while (!sensor_query.eof()) {
                        sensor_data = sensor_query.fieldValue(0);
                        response << sensor_data << ",";
                        sensor_query.nextRow();
                    }

                    response << "]}]});" << std::endl;
                }
            } else {
                response << "$('#" << div_id  << "').tabs();" << std::endl;
            }
        }
    }

    void actuator_data(Mongoose::Request& request, Mongoose::StreamResponse& response) {
        std::string url = request.getUrl();

        auto start = url.find_first_not_of("/");
        auto end = url.find("/", start + 1);

        std::string actuator_name(url.begin() + start, url.begin() + end);

        int actuator_pk = db_exec_scalar("select pk_actuator from actuator where name=\"%s\";", actuator_name.c_str());

        CppSQLite3Query actuator_query = db_exec_query("select data from actuator_data where fk_actuator=%d order by time desc limit 1;", actuator_pk);

        if (!actuator_query.eof()) {
            std::string actuator_data = actuator_query.fieldValue(0);

            auto div_id = actuator_name;

            response << "<div id=\"" << div_id << "\" class=\"tabs\"><ul>"
                     << "<li class=\"title\">Actuator name : " << actuator_name << "</li></ul>" << std::endl
                     << "<ul><li>Last Input : " << actuator_data << "</li>" << std::endl;

            int nbClicks = db_exec_scalar("select count(data) from actuator_data where fk_actuator=%d;", actuator_pk);
            response << "<li>Number of Inputs : " << nbClicks << "</li></ul>" << std::endl
                     << "</div>" << std::endl;
        }
    }

    void actuator_script(Mongoose::Request& request, Mongoose::StreamResponse& response) {
        std::string url = request.getUrl();

        auto start = url.find_first_not_of("/");
        auto end = url.find("/", start + 1);

        std::string actuator_name(url.begin() + start, url.begin() + end);

        int actuator_pk = db_exec_scalar("select pk_actuator from actuator where name=\"%s\";", actuator_name.c_str());

        CppSQLite3Query actuator_query = db_exec_query("select data from actuator_data where fk_actuator=%d order by time desc limit 1;", actuator_pk);

        if (!actuator_query.eof()) {
            auto div_id = actuator_name;

            response << "$('#" << div_id  << "').tabs();" << std::endl;
        }
    }

    //This will be called automatically
    void setup() {
        addRoute<display_controller>("GET", "/", &display_controller::display);
        addRoute<display_controller>("GET", "/display", &display_controller::display);
        addRoute<display_controller>("GET", "/led_on", &display_controller::led_on);
        addRoute<display_controller>("GET", "/led_off", &display_controller::led_off);

        //TODO The routes should be added dynamically when we register a new source or sensor or actuator
        //Otherwise the new sensors will not show unless we restart the server

        try {
            CppSQLite3Query sensor_query = db.execQuery("select name, type from sensor order by name;");

            while (!sensor_query.eof()) {
                std::string sensor_name = sensor_query.fieldValue(0);
                std::string sensor_type = sensor_query.fieldValue(1);

                std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);

                std::string url = "/" + sensor_name + "/" + sensor_type;
                addRoute<display_controller>("GET", url + "/data", &display_controller::sensor_data);
                addRoute<display_controller>("GET", url + "/script", &display_controller::sensor_script);

                sensor_query.nextRow();
            }

            CppSQLite3Query actuator_query = db.execQuery("select name from actuator order by name;");

            while (!actuator_query.eof()) {
                std::string url = std::string("/") + actuator_query.fieldValue(0);

                addRoute<display_controller>("GET", url + "/data", &display_controller::actuator_data);
                addRoute<display_controller>("GET", url + "/script", &display_controller::actuator_script);

                actuator_query.nextRow();
            }

        } catch (CppSQLite3Exception& e){
            std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
        }
    }
};

} //end of anonymous namespace

int main() {
#ifdef __RPI__
    //Run the wiringPi setup (as root)
    wiringPiSetup();
#endif

    //Drop root privileges and run as pi:pi again
    if (!revoke_root()) {
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

    init_led();
    set_led_on();

    return run();
}
