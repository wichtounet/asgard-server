//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include<algorithm>

#include "display_controller.hpp"
#include "db.hpp"
#include "led.hpp"
#include "server.hpp"

const std::vector<size_t> interval{1, 24, 48};

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
.rule{display: inline-block;}
#header{background-color: lightgray; opacity: 0.8; width: 1020px; height: 65px; margin-top: 20px;
border-radius: 5px 5px 0px 0px; border: solid black; border-width: 1px 1px 0px 1px;}
#container{width: 1000px; padding-right: 10px; padding-bottom: 10px; padding-left: 10px; border: 1px solid black; border-radius: 0px 0px 5px 5px; overflow: hidden;}
#sidebar{float: left; width: 240px;}
#main{float: right;}
#footer{text-align: right; width: 1000px; margin-top: 30px; margin-bottom: 30px; font-size: 14px;}
</style>
<script>
$(function() {
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

void display_controller::display_controller::display_menu(Mongoose::StreamResponse& response) {
    std::cout << "DEBUG: asgard: Begin rendering menu" << std::endl;

    response << "<ul class=\"menu\"><li onclick=\"location.href='/actions'\">Actions Page</li>" << std::endl
             << "<li onclick=\"location.href='/rules'\">Rules Page</li>" << std::endl
             << "<li onclick=\"load_menu('hideable')\">Show All</li></ul>" << std::endl
             << "<p>Drivers registered :</p>" << std::endl
             << "<ul class=\"menu\">" << std::endl;

    CppSQLite3Query source_query = get_db().execQuery("select name, pk_source from source order by name;");

    while (!source_query.eof()) {
        std::string source_name = source_query.fieldValue(0);
        int source_pk = source_query.getIntField(1);
        response << "<li onclick=\"load_source('" << source_pk << "')\">" << source_name << "</li>" << std::endl;
        source_query.nextRow();
    }

    response << "</ul>" << std::endl
             << "<p>Sensors active :</p>" << std::endl
             << "<ul class=\"menu\">" << std::endl;

    CppSQLite3Query sensor_query = get_db().execQuery("select distinct name from sensor order by name;");

    while (!sensor_query.eof()) {
        std::string sensor_name = sensor_query.fieldValue(0);
        response << "<li onclick=\"load_menu('" << sensor_name << "')\">" << sensor_name << "</li>" << std::endl;
        sensor_query.nextRow();
    }

    response << "</ul>" << std::endl
             << "<p>Actuators active :</p>" << std::endl
             << "<ul class=\"menu\">" << std::endl;

    CppSQLite3Query actuator_query = get_db().execQuery("select name from actuator order by name;");

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

    std::cout << "DEBUG: asgard: End rendering menu" << std::endl;
}

void display_controller::display_controller::display_sensors(Mongoose::StreamResponse& response) {
    std::cout << "DEBUG: asgard: Begin rendering sensors" << std::endl;

    for (auto& data : get_db().execQuery("select name, type, pk_sensor from sensor order by name;")) {
        std::string sensor_name = data.fieldValue(0);
        std::string sensor_type = data.fieldValue(1);
        int sensor_pk = data.getIntField(2);
CppSQLite3Query sensor_data = db_exec_query(get_db(), "select data from sensor_data where fk_sensor=%d order by time desc limit 1;", sensor_pk);

        if (!sensor_data.eof()) {
            std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);

            std::string url_data   = sensor_name + "/" + sensor_type + "/data";
            std::string url_script = sensor_name + "/" + sensor_type + "/script";

            response << "<div id=\"" << sensor_name << "_" << sensor_type << "\" class=\"hideable " << sensor_name << "\"></div>" << std::endl
                     << "<script> $(function() {" << std::endl

                     << "$(\"#" << sensor_name << "_" << sensor_type << "\").load(\"/" << url_data << "\", function() {" << std::endl
                     << "$.ajaxSetup({ cache: false });" << std::endl
                     << "$.getScript(\"" << url_script << "\");" << std::endl
                     << "});" << std::endl

                     << "setInterval(function() {" << std::endl

                     << "if ($(\"#" << sensor_name << "_" << sensor_type << "\").is(\":visible\")) {" << std::endl

                     << "$(\"#" << sensor_name << "_" << sensor_type << "\").load(\"/" << url_data << "\", function() {" << std::endl
                     << "$.ajaxSetup({ cache: false });" << std::endl
                     << "$.getScript(\"" << url_script << "\");" << std::endl
                     << "});}" << std::endl

                     << "}, 10000);" << std::endl

                     << "})</script>" << std::endl;
        }
    }

    std::cout << "DEBUG: asgard: End rendering sensors" << std::endl;
}

void display_controller::display_controller::display_actuators(Mongoose::StreamResponse& response) {
    std::cout << "DEBUG: asgard: Begin rendering actuators" << std::endl;

    for (auto& data : get_db().execQuery("select name, pk_actuator from actuator order by name;")) {
        std::string actuator_name = data.fieldValue(0);
        int actuator_pk = data.getIntField(1);

        CppSQLite3Query actuator_data = db_exec_query(get_db(), "select data from actuator_data where fk_actuator=%d order by time desc limit 1;", actuator_pk);

        if (!actuator_data.eof()) {
            std::string url_data   = actuator_name + "/data";
            std::string url_script = actuator_name + "/script";

            response << "<div id=\"" << actuator_name << "_script\" class=\"hideable " << actuator_name << "\"></div>" << std::endl
                     << "<script> $(function() {" << std::endl

                     << "$(\"#" << actuator_name << "_script\").load(\"/" << url_data << "\", function() {" << std::endl
                     << "$.ajaxSetup({ cache: false });" << std::endl
                     << "$.getScript(\"" << url_script << "\");" << std::endl
                     << "});" << std::endl

                     << "setInterval(function() {" << std::endl

                     << "if ($(\"#" << actuator_name << "_script\").is(\":visible\")) {" << std::endl

                     << "$(\"#" << actuator_name << "_script\").load(\"/" << url_data << "\", function() {" << std::endl
                     << "$.ajaxSetup({ cache: false });" << std::endl
                     << "$.getScript(\"" << url_script << "\");" << std::endl
                     << "});}" << std::endl

                     << "}, 10000);" << std::endl

                     << "})</script>" << std::endl;
        }
    }

    std::cout << "DEBUG: asgard: End rendering actuators" << std::endl;
}

void display_controller::display_controller::display(Mongoose::Request& /*request*/, Mongoose::StreamResponse& response){
    std::cout << "DEBUG: asgard: Begin rendering home" << std::endl;

    response << header << std::endl
             << "<div id=\"header\"><center><h2>Asgard - Home Automation System</h2></center></div>" << std::endl
             << "<div id=\"container\"><div id=\"sidebar\"><div class=\"tabs\" style=\"width: 240px;\">" << std::endl
             << "<ul><li class=\"title\">Current information</li></ul>" << std::endl;

    try {
        response << "<script>function load_source(pk) {" << std::endl;
        response << "$('.hideable').hide();" << std::endl;

        for (auto& data : get_db().execQuery("select distinct name, fk_source from sensor order by name;")) {
            std::string sensor_name = data.fieldValue(0);
            int sensor_fk = data.getIntField(1);
            response << "if (pk == " << sensor_fk << ") { $('." << sensor_name << "').show(); }" << std::endl;
        }

        for (auto& data : get_db().execQuery("select distinct name, fk_source from actuator order by name;")) {
            std::string actuator_name = data.fieldValue(0);
            int actuator_fk = data.getIntField(1);
            response << "if (pk == " << actuator_fk << ") { $('." << actuator_name << "').show(); }" << std::endl;
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

    std::cout << "DEBUG: asgard: End rendering home" << std::endl;
}

void display_controller::led_on(Mongoose::Request& request, Mongoose::StreamResponse& response) {
    set_led_on();
    display(request, response);
}

void display_controller::led_off(Mongoose::Request& request, Mongoose::StreamResponse& response) {
    set_led_off();
    display(request, response);
}

void display_controller::sensor_data(Mongoose::Request& request, Mongoose::StreamResponse& response) {
    std::cout << "TRACE: asgard: Begin rendering sensor data" << std::endl;

    std::string url = request.getUrl();

    auto start_name = 1;
    auto end_name = url.find("/", 1);

    auto start_type = end_name + 1;
    auto end_type = url.find("/", start_type);

    std::string sensor_name(url.begin() + start_name, url.begin() + end_name);
    std::string sensor_type(url.begin() + start_type, url.begin() + end_type);

    std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::toupper);

    int sensor_pk = db_exec_scalar(get_db(), "select pk_sensor from sensor where name=\"%s\" and type=\"%s\";", sensor_name.c_str(), sensor_type.c_str());

    std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);

    sensor_type[0] = toupper(sensor_type[0]);
    CppSQLite3Query sensor_query = db_exec_query(get_db(), "select data from sensor_data where fk_sensor=%d order by time desc limit 1;", sensor_pk);

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

            int nbValue = db_exec_scalar(get_db(), "select count(data) from sensor_data where fk_sensor=%d;", sensor_pk);
            response << "<li>Number of Values : " << nbValue << "</li></ul>" << std::endl;
        }
        response << "</div>" << std::endl;
    }

    std::cout << "TRACE: asgard: End rendering sensor data" << std::endl;
}

void display_controller::sensor_script(Mongoose::Request& request, Mongoose::StreamResponse& response) {
    std::cout << "TRACE: asgard: Begin rendering sensor script" << std::endl;

    std::string url = request.getUrl();

    auto start_name = 1;
    auto end_name = url.find("/", 1);

    auto start_type = end_name + 1;
    auto end_type = url.find("/", start_type);

    std::string sensor_name(url.begin() + start_name, url.begin() + end_name);
    std::string sensor_type(url.begin() + start_type, url.begin() + end_type);

    std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::toupper);

    int sensor_pk = db_exec_scalar(get_db(), "select pk_sensor from sensor where name=\"%s\" and type=\"%s\";", sensor_name.c_str(), sensor_type.c_str());

    std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);

    sensor_type[0] = toupper(sensor_type[0]);
    CppSQLite3Query sensor_query = db_exec_query(get_db(), "select data from sensor_data where fk_sensor=%d order by time desc limit 1;", sensor_pk);

    if (!sensor_query.eof()) {
        std::string sensor_data = sensor_query.fieldValue(0);

        auto div_id = sensor_name + sensor_type;

        if (sensor_type == "Temperature" || sensor_type == "Humidity") {
            response << "$('#" << div_id  << "').tabs();" << std::endl;

            for (size_t i = 0; i < interval.size(); ++i) {
                response << "$('#" << div_id << i
                         << "').highcharts({chart: {marginBottom: 60}, title: {text: ''}, xAxis: {categories: [";

                CppSQLite3Query sensor_interval = db_exec_query(
                    get_db(), "select time from sensor_data where time > datetime('now', '-%d hours') and fk_sensor=%d order by time;", interval[i], sensor_pk);

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
                    get_db(), "select data from sensor_data where time > datetime('now', '-%d hours') and fk_sensor=%d order by time;", interval[i], sensor_pk);

                while (!sensor_query.eof()) {
                    sensor_data = sensor_query.fieldValue(0);
                    response << sensor_data << ",";
                    sensor_query.nextRow();
                }

                response << "]}]});" << std::endl;
            }

            response << "$('a[data-toggle=\"tab\"]').on('click', function (e) {" << std::endl
                     << "var selector = $(this.getAttribute(\"href\"));" << std::endl
                     << "var chart = $(selector).highcharts();" << std::endl
                     << "chart.reflow();" << std::endl
                     << "});" << std::endl;

        } else {
            response << "$('#" << div_id  << "').tabs();" << std::endl;
        }
    }

    std::cout << "TRACE: asgard: End rendering sensor script" << std::endl;
}

void display_controller::actuator_data(Mongoose::Request& request, Mongoose::StreamResponse& response) {
    std::cout << "TRACE: asgard: Begin rendering actuator data" << std::endl;

    std::string url = request.getUrl();

    auto start = url.find_first_not_of("/");
    auto end = url.find("/", start + 1);

    std::string actuator_name(url.begin() + start, url.begin() + end);

    int actuator_pk = db_exec_scalar(get_db(), "select pk_actuator from actuator where name=\"%s\";", actuator_name.c_str());

    CppSQLite3Query actuator_query = db_exec_query(get_db(), "select data from actuator_data where fk_actuator=%d order by time desc limit 1;", actuator_pk);

    if (!actuator_query.eof()) {
        std::string actuator_data = actuator_query.fieldValue(0);

        auto div_id = actuator_name;

        response << "<div id=\"" << div_id << "\" class=\"tabs\"><ul>"
                 << "<li class=\"title\">Actuator name : " << actuator_name << "</li></ul>" << std::endl
                 << "<ul><li>Last Input : " << actuator_data << "</li>" << std::endl;

        int nbClicks = db_exec_scalar(get_db(), "select count(data) from actuator_data where fk_actuator=%d;", actuator_pk);
        response << "<li>Number of Inputs : " << nbClicks << "</li></ul>" << std::endl
                 << "</div>" << std::endl;
    }

    std::cout << "TRACE: asgard: End rendering actuator data" << std::endl;
}

void display_controller::actuator_script(Mongoose::Request& request, Mongoose::StreamResponse& response) {
    std::cout << "TRACE: asgard: Begin rendering actuator script" << std::endl;

    std::string url = request.getUrl();

    auto start = url.find_first_not_of("/");
    auto end = url.find("/", start + 1);

    std::string actuator_name(url.begin() + start, url.begin() + end);

    int actuator_pk = db_exec_scalar(get_db(), "select pk_actuator from actuator where name=\"%s\";", actuator_name.c_str());

    CppSQLite3Query actuator_query = db_exec_query(get_db(), "select data from actuator_data where fk_actuator=%d order by time desc limit 1;", actuator_pk);

    if (!actuator_query.eof()) {
        auto div_id = actuator_name;

        response << "$('#" << div_id  << "').tabs();" << std::endl;
    }

    std::cout << "TRACE: asgard: End rendering actuator script" << std::endl;
}

void display_controller::display_actions(Mongoose::Request& /*request*/, Mongoose::StreamResponse& response) {
    std::cout << "DEBUG: asgard: Begin rendering actions" << std::endl;

    response << header << std::endl
             << "<div id=\"header\"><center><h2>Asgard - Home Automation System</h2></center></div>" << std::endl
             << "<div id=\"container\"><div class=\"sidebar\"><div class=\"tabs\" style=\"float: left; width: 240px;\"><ul><li class=\"title\">Actions Menu</li></ul>" << std::endl
             << "<ul class=\"menu\"><li onclick=\"top.location.href='/'\">Main Page</li><li onclick=\"location.href='/rules'\">Rules Page</li></ul>" << std::endl
             << "</div></div>" << std::endl
             << "<div id=\"main\"><div class=\"tabs\">" << std::endl
             << "<ul><li class=\"title\">Actions Available</li></ul><ul>" << std::endl;

    for (auto& data : get_db().execQuery("select fk_source, name, type from action;")) {
        int source_pk = data.getIntField(0);
        std::string action_name = data.fieldValue(1);
        std::string action_type = data.fieldValue(2);

        CppSQLite3Query source_query = db_exec_query(get_db(), "select name from source where pk_source=%d;", source_pk);
        std::string source_name = source_query.fieldValue(0);

        if (action_type == "SIMPLE") {
            response << "<li><a href=/action/" << source_name << "/" << action_name << ">" << action_name << "</a></li>" << std::endl;
        } else {
            response << "<li><FORM action=\"/action/" << source_name << "/" << action_name << "\" method=\"GET\">" << action_name << " : <input name=\"value\" type=\"text\"><input type=\"submit\"></FORM></li>" << std::endl;
        }
    }
    response << "</ul></div></div></div>" << std::endl
             << "<div id=\"footer\">© 2015-2016 Asgard Team. All Rights Reserved.</div></body></html>" << std::endl;

    std::cout << "DEBUG: asgard: End rendering actions" << std::endl;
}

void display_controller::display_rules(Mongoose::Request& /*request*/, Mongoose::StreamResponse& response) {
    std::cout << "DEBUG: asgard: Begin rendering rules" << std::endl;

    response << header << std::endl
             << "<div id=\"header\"><center><h2>Asgard - Home Automation System</h2></center></div>" << std::endl
             << "<div id=\"container\"><div class=\"sidebar\"><div class=\"tabs\" style=\"float: left; width: 240px;\"><ul><li class=\"title\">Rules Menu</li></ul>" << std::endl
             << "<ul class=\"menu\"><li onclick=\"top.location.href='/'\">Main Page</li><li onclick=\"location.href='/actions'\">Actions Page</li></ul>" << std::endl
             << "</div></div>" << std::endl
             << "<div id=\"main\"><div class=\"tabs\">" << std::endl
             << "<ul><li class=\"title\">Add Rules</li></ul><FORM action=\"/addrule\" method=\"GET\">" << std::endl
             << "<ul style=\"list-style-type: none;\"><li>Condition :</li>" << std::endl
             << "<li><div class=\"rule\"><SELECT name=\"source\" size=\"1\">" << std::endl;

    // Add sensors to the list of sources


    for(auto& data : db_exec_query(get_db(), "select pk_sensor, name, type from sensor order by name;")){
        int sensor_pk = data.getIntField(0);
        std::string sensor_name = data.fieldValue(1);
        std::string sensor_type = data.fieldValue(2);
        response << "<OPTION value=\"s" << sensor_pk << "\">" << sensor_name << " (" << sensor_type << ")" << std::endl;
    }

    // Add actuators to the list of sources

    for(auto& data : db_exec_query(get_db(), "select pk_actuator, name from actuator order by name;")){
        int actuator_pk = data.getIntField(0);
        std::string actuator_name = data.fieldValue(1);
        response << "<OPTION value=\"a" << actuator_pk << "\">" << actuator_name << std::endl;
    }

    response << "</SELECT></div>" << std::endl
             << "<div class=\"rule\"><SELECT name=\"operator\" size=\"1\">" << std::endl
             << "<OPTION>=" << std::endl
             << "<OPTION>>" << std::endl
             << "<OPTION><" << std::endl
             << "</SELECT></div>" << std::endl
             << "<div class=\"rule\"><input name=\"condition_value\" type=\"text\">" << std::endl
             << "</div></li></ul>" << std::endl
             << "<ul style=\"list-style-type: none;\"><li>Action :</li>" << std::endl
             << "<li><div class=\"rule\"><SELECT name=\"action\" size=\"1\">" << std::endl;

    // Add actions to the list of actions

    for(auto& data : db_exec_query(get_db(), "select pk_action, name, type from action order by name;")){
        int action_pk = data.getIntField(0);
        std::string action_name = data.fieldValue(1);
        std::string action_type = data.fieldValue(2);
        response << "<OPTION value=\"n" << action_pk << "\">" << action_name << " (" << action_type << ")" << std::endl;
    }

    // Add system actions to the list of actions

    response << "<OPTION value=\"s1\">sleep (system)" << std::endl;

    response << "</SELECT></div>" << std::endl
             << "<div class=\"rule\"><input name=\"action_value\" type=\"text\">" << std::endl
             << "</div></li></ul>" << std::endl
             << "<ul style=\"list-style-type: none; margin-top: 25px;\"><li><input value=\"Create\" type=\"submit\">" << std::endl
             << "</li></ul></FORM></div>" << std::endl
             << "<div class=\"tabs\"><ul>" << std::endl
             << "<li class=\"title\">Actual Rules</li></ul>" << std::endl
             << "<ul style=\"list-style-type: none;\"><li><table cellpadding=8>" << std::endl
             << "<tr><th colspan=3>Conditions (When)</th><th colspan=2>Actions (Do)</th></tr>" << std::endl;

    // Fill the table of rules

    for(auto& rule_data : db_exec_query(get_db(), "select fk_condition, fk_action, system_action, value from rule;")){
        int fk_condition = rule_data.getIntField(0);
        int fk_action = rule_data.getIntField(1);
        int system_action = rule_data.getIntField(2);
        std::string rule_value = rule_data.fieldValue(3);

        CppSQLite3Query condition_query = db_exec_query(get_db(), "select operator, value, fk_sensor, fk_actuator from condition where pk_condition = %d;", fk_condition);

        if(condition_query.eof()){
            std::cerr << "Invalid link in database pk_condition <> fk_condition" << std::endl;
            break;
        }

        std::string condition_operator = condition_query.fieldValue(0);
        std::string condition_value = condition_query.fieldValue(1);
        int sensor_fk = condition_query.getIntField(2);
        int actuator_fk = condition_query.getIntField(3);

        // Get the source event (actuator or sensor)

        if(sensor_fk == 0){
            CppSQLite3Query actuator_query = db_exec_query(get_db(), "select name from actuator where pk_actuator=%d;", actuator_fk);

            if(actuator_query.eof()){
                std::cerr << "Invalid link in database pk_actuator <> fk_actuator" << std::endl;
                break;
            }

            std::string actuator_name = actuator_query.fieldValue(0);
            response << "<tr><td>" << actuator_name << "</td><td>&nbsp;</td><td width=\"200px\">&nbsp;</td>" << std::endl;
        } else if (actuator_fk == 0){
            CppSQLite3Query sensor_query = db_exec_query(get_db(), "select name, type from sensor where pk_sensor=%d;", sensor_fk);

            if(sensor_query.eof()){
                std::cerr << "Invalid link in database pk_actuator <> fk_actuator" << std::endl;
                break;
            }

            std::string sensor_name = sensor_query.fieldValue(0);
            std::string sensor_type = sensor_query.fieldValue(1);
            response << "<tr><td>" << sensor_name << " (" << sensor_type << ")</td><td>" << condition_operator << "</td><td td width=\"80px\">" << condition_value << "</td>" << std::endl;
        }

        // Get the action

        if(fk_action){
            CppSQLite3Query do_query = db_exec_query(get_db(), "select name, type from action where pk_action=%d;", fk_action);

            if(do_query.eof()){
                std::cerr << "Invalid link in database pk_action <> fk_action" << std::endl;
                break;
            }

            std::string do_name = do_query.fieldValue(0);
            std::string do_type = do_query.fieldValue(1);
            response << "<td>" << do_name << " (" << do_type << ")</td><td>" << rule_value << "</td></tr>" << std::endl;
        } else if(system_action){
            if(system_action == 1){
                response << "<td>sleep (system)</td><td>" << rule_value << "</td></tr>" << std::endl;
            } else {
                std::cerr << "Invalid system action: " << system_action << std::endl;
                break;
            }
        }
    }

    response << "</table></li></ul></div></div></div>" << std::endl
             << "<div id=\"footer\">© 2015-2016 Asgard Team. All Rights Reserved.</div></body></html>" << std::endl;

    std::cout << "DEBUG: asgard: End rendering rules" << std::endl;
}

void display_controller::action(Mongoose::Request& request, Mongoose::StreamResponse& response) {
    std::string url = request.getUrl();

    auto start_source = url.find("/", 1) + 1;
    auto end_source = url.find("/", start_source);

    auto start_action = end_source + 1;
    auto end_action = url.size();

    std::string source_name(url.begin() + start_source, url.begin() + end_source);
    std::string action_name(url.begin() + start_action, url.begin() + end_action);

    CppSQLite3Query source_query = db_exec_query(get_db(), "select pk_source from source where name=\"%s\";", source_name.c_str());

    if(!source_query.eof()){
        int pk_source = source_query.getIntField(0);

        CppSQLite3Query action_query = db_exec_query(get_db(), "select type from action where name=\"%s\" and fk_source=%d;", action_name.c_str(), pk_source);
        std::string action_type = action_query.fieldValue(0);

        if(!action_query.eof()){
            auto client_addr = source_addr_from_sql(pk_source);
            if(action_type == "SIMPLE"){
                send_to_driver(client_addr, "ACTION " + action_name);
            } else {
                send_to_driver(client_addr, "ACTION " + action_name + " " + request.get("value"));
            }
        }
    }
    response << "<!DOCTYPE HTML><html>" << std::endl
             << "<head><meta charset=\"UTF-8\"><meta http-equiv=\"refresh\">" << std::endl
             << "<script type=\"text/javascript\">window.location.href=\"http://192.168.20.161:8080/actions\"</script>" << std::endl
             << "<title>Page Redirection</title></head>" << std::endl
             << "<body>If you are not redirected automatically, follow the <a href='http://192.168.20.161:8080/actions'>following link</a>" << std::endl
             << "</body></html>" << std::endl;
}

void display_controller::add_rule(Mongoose::Request& request, Mongoose::StreamResponse& response) {
    std::string source = request.get("source");
    std::string symbole = request.get("operator");
    std::string condition_value = request.get("condition_value");

    // Create the condition in the database

    bool valid = true;
    if(source[0] == 's'){
        if (!db_exec_dml(get_db(), "insert into condition(operator, value, fk_sensor) select \"%s\",\"%s\", %d;",
                        symbole.c_str(), condition_value.c_str(), std::atoi(std::string(source.begin() + 1, source.end()).c_str()))) {
            std::cerr << "ERROR: asgard:: Failed to insert into condition (sensor)" << std::endl;
            valid = false;
        }
    } else if(source[0] == 'a'){
        if (!db_exec_dml(get_db(), "insert into condition(operator, value, fk_actuator) select \"%s\",\"%s\", %d;",
                         symbole.c_str(), condition_value.c_str(), std::atoi(std::string(source.begin() + 1, source.end()).c_str()))) {
            std::cerr << "ERROR: asgard:: Failed to insert into condition (actuator)" << std::endl;
            valid = false;
        }
    } else {
        std::cerr << "ERROR: asgard: Invalid source (add_rule)" << std::endl;
        valid = false;
    }

    auto condition_pk = get_db().lastRowId();

    std::string action = request.get("action");
    std::string action_value = request.get("action_value");

    if (valid) {
        // Handle normal action
        if(action[0] == 'n'){
            if (!db_exec_dml(get_db(),
                    "insert into rule(value, fk_action, fk_condition) select \"%s\", %d, %d ;",
                    action_value.c_str(), std::atoi(std::string(action.begin() + 1, action.end()).c_str()), condition_pk)) {
                std::cerr << "ERROR: asgard:: Failed to insert into rule" << std::endl;
            }
        }
        // Handle system action
        else if(action[0] == 's'){
            if (!db_exec_dml(get_db(),
                    "insert into rule(value, system_action, fk_condition) select \"%s\", %d, %d ;",
                    action_value.c_str(), std::atoi(std::string(action.begin() + 1, action.end()).c_str()), condition_pk)) {
                std::cerr << "ERROR: asgard:: Failed to insert into rule" << std::endl;
            }
        }
    } else {
        std::cerr << "ERROR: asgard: Invalid action (add_rule)" << std::endl;
    }

    response << "<!DOCTYPE HTML><html>" << std::endl
             << "<head><meta charset=\"UTF-8\"><meta http-equiv=\"refresh\">" << std::endl
             << "<script type=\"text/javascript\">window.location.href=\"http://192.168.20.161:8080/rules\"</script>" << std::endl
             << "<title>Page Redirection</title></head>" << std::endl
             << "<body>If you are not redirected automatically, follow the <a href='/rules'>following link</a>" << std::endl
             << "</body></html>" << std::endl;
}

//This will be called automatically
void display_controller::display_controller::setup() {
    addRoute<display_controller>("GET", "/", &display_controller::display);
    addRoute<display_controller>("GET", "/display", &display_controller::display);
    addRoute<display_controller>("GET", "/led_on", &display_controller::led_on);
    addRoute<display_controller>("GET", "/led_off", &display_controller::led_off);

    addRoute<display_controller>("GET", "/actions", &display_controller::display_actions);
    addRoute<display_controller>("GET", "/rules", &display_controller::display_rules);
    addRoute<display_controller>("GET", "/addrule", &display_controller::add_rule);

    //TODO The routes should be added dynamically when we register a new source or sensor or actuator
    //Otherwise the new sensors will not show unless we restart the server

    try {
        // Register the sensor data and script pages
        for(auto& data : get_db().execQuery("select name, type, pk_sensor from sensor order by name;")){
            std::string sensor_name = data.fieldValue(0);
            std::string sensor_type = data.fieldValue(1);
            int sensor_pk = data.getIntField(2);

            CppSQLite3Query sensor_data = db_exec_query(get_db(), "select data from sensor_data where fk_sensor=%d order by time desc limit 1;", sensor_pk);

            if (!sensor_data.eof()) {
                std::transform(sensor_type.begin(), sensor_type.end(), sensor_type.begin(), ::tolower);

                std::string url = "/" + sensor_name + "/" + sensor_type;
                addRoute<display_controller>("GET", url + "/data", &display_controller::sensor_data);
                addRoute<display_controller>("GET", url + "/script", &display_controller::sensor_script);
            }
        }

        // Register the actuator data and script pages
        for(auto& data : get_db().execQuery("select name, pk_actuator from actuator order by name;")){
            std::string url = std::string("/") + data.fieldValue(0);
            int actuator_pk = data.getIntField(1);

            CppSQLite3Query actuator_data = db_exec_query(get_db(), "select data from actuator_data where fk_actuator=%d order by time desc limit 1;", actuator_pk);

            if (!actuator_data.eof()) {
                addRoute<display_controller>("GET", url + "/data", &display_controller::actuator_data);
                addRoute<display_controller>("GET", url + "/script", &display_controller::actuator_script);
            }
        }
    } catch (CppSQLite3Exception& e){
        std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }
}
