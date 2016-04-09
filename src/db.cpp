//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include "db.hpp"

void create_tables(CppSQLite3DB& db) {
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

bool db_connect(CppSQLite3DB& db) {
    try {
        db.open("asgard.db");

        // Create tables
        create_tables(db);

        // Perform pi insertion
        db.execDML("insert into pi(name) select 'tyr' where not exists(select 1 from pi where name='tyr');");

        return true;
    } catch (CppSQLite3Exception& e) {
        std::cerr << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }

    return false;
}

