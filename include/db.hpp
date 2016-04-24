//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <string>
#include <iostream>

#include "CppSQLite3.h"

CppSQLite3DB& get_db();

template<typename... T>
int db_exec_dml(CppSQLite3DB& db, const std::string& query, T... args){
    try {
        CppSQLite3Buffer buffSQL;
        buffSQL.format(query.c_str(), args...);
        return db.execDML(buffSQL);
    } catch (CppSQLite3Exception& e) {
        std::cerr << "asgard: SQL Query failed: " << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }

    return 0;
}

template<typename... T>
int db_exec_scalar(CppSQLite3DB& db, const std::string& query, T... args){
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
CppSQLite3Query db_exec_query(CppSQLite3DB& db, const std::string& query, T... args){
    try {
        CppSQLite3Buffer buffSQL;
        buffSQL.format(query.c_str(), args...);
        return db.execQuery(buffSQL);
    } catch (CppSQLite3Exception& e) {
        std::cerr << "asgard: SQL Query failed: " << e.errorCode() << ":" << e.errorMessage() << std::endl;
    }

    return {};
}

struct query_iterator {
    CppSQLite3Query& query;
    bool end;

    query_iterator(CppSQLite3Query& query, bool end = false);

    bool operator==(const query_iterator& rhs) const ;
    bool operator!=(const query_iterator& rhs) const ;

    query_iterator& operator++();
    CppSQLite3Query& operator*();
};

query_iterator begin(CppSQLite3Query& query);
query_iterator end(CppSQLite3Query& query);

void create_tables(CppSQLite3DB& db);
bool db_connect(CppSQLite3DB& db);
