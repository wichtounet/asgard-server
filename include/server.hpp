//=======================================================================
// Copyright (c) 2015-2016 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include<sys/socket.h>
#include<sys/un.h>

int source_addr_from_sql(int id_sql);

bool send_message(int client_address, const std::string& message);
