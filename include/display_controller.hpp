//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <mongoose/Server.h>
#include <mongoose/WebController.h>

struct display_controller : public Mongoose::WebController {
    void display_menu(Mongoose::StreamResponse& response);
    void display_sensors(Mongoose::StreamResponse& response);
    void display_actuators(Mongoose::StreamResponse& response);
    void display(Mongoose::Request& /*request*/, Mongoose::StreamResponse& response);
    void led_on(Mongoose::Request& request, Mongoose::StreamResponse& response);
    void led_off(Mongoose::Request& request, Mongoose::StreamResponse& response);
    void sensor_data(Mongoose::Request& request, Mongoose::StreamResponse& response);
    void sensor_script(Mongoose::Request& request, Mongoose::StreamResponse& response);
    void actuator_data(Mongoose::Request& request, Mongoose::StreamResponse& response);
    void actuator_script(Mongoose::Request& request, Mongoose::StreamResponse& response);
    void display_actions(Mongoose::Request& request, Mongoose::StreamResponse& response);
    void action(Mongoose::Request& request, Mongoose::StreamResponse& response);
    void setup();
};
