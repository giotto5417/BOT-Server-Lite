/*
  Copyright (c) 2016 Academia Sinica, Institute of Information Science

  License:

     GPL 3.0 : The content of this file is subject to the terms and conditions
     defined in file 'COPYING.txt', which is part of this source code package.

  Project Name:

     BeDIS

  File Name:

     sqlWrapper.c

  File Description:

     This file provides APIs for interacting with postgreSQL. This file contains
     programs to connect and disconnect databases, insert, query, update and
     delete data in the database and some APIs for the BeDIS system use
     including updating device status and object tracking data maintainance and
     updates.

  Version:

     1.0, 20191002

  Abstract:

     BeDIS uses LBeacons to deliver 3D coordinates and textual descriptions of
     their locations to users' devices. Basically, a LBeacon is an inexpensive,
     Bluetooth Smart Ready device. The 3D coordinates and location description
     of every LBeacon are retrieved from BeDIS (Building/environment Data and
     Information System) and stored locally during deployment and maintenance
     times. Once initialized, each LBeacon broadcasts its coordinates and
     location description to Bluetooth enabled user devices within its coverage
     area.

  Authors:

     Chun-Yu Lai   , chunyu1202@gmail.com
 */

#include "SqlWrapper.h"

static ErrorCode SQL_execute(PGconn *db_conn, char *sql_statement){

    PGresult *res;

    zlog_info(category_debug, "SQL command = [%s]", sql_statement);

    res = PQexec(db_conn, sql_statement);

    if(PQresultStatus(res) != PGRES_COMMAND_OK){

        zlog_error(category_debug, 
                   "SQL_execute failed [%d]: %s", 
                   res, PQerrorMessage(db_conn));

        PQclear(res);
        return E_SQL_EXECUTE;
    }

    PQclear(res);

    return WORK_SUCCESSFULLY;
}

static ErrorCode SQL_begin_transaction(PGconn* db_conn){

    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char *sql;

    /* Create SQL statement */
    sql = "BEGIN TRANSACTION;";

    /* Execute SQL statement */
    ret_val = SQL_execute(db_conn, sql);

    return WORK_SUCCESSFULLY;
}

static ErrorCode SQL_commit_transaction(PGconn *db_conn){

    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char *sql;

    /* Create SQL statement */
    sql = "END TRANSACTION;";

    /* Execute SQL statement */
    ret_val = SQL_execute(db_conn, sql);

    return WORK_SUCCESSFULLY;
}

static ErrorCode SQL_rollback_transaction(PGconn *db_conn){

    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char *sql;

    /* Create SQL statement */
    sql = "ROLLBACK;";

    /* Execute SQL statement */
    ret_val = SQL_execute(db_conn, sql);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_create_database_connection_pool(
    char *conninfo, 
    DBConnectionListHead * db_connection_list_head,
    int max_connection){

    int i;
    int retry_times = MEMORY_ALLOCATE_RETRIES;
    DBConnectionNode *db_connection;

    pthread_mutex_lock(&db_connection_list_head->list_lock);

    for(i = 0; i< max_connection; i++){
    
        while(retry_times --){
            db_connection = malloc(sizeof(DBConnectionNode));
            if(NULL != db_connection)
                break;
        }
        if(NULL == db_connection){
        
            zlog_error(category_debug, 
                       "SQL_create_database_connection_pool malloc failed");

            pthread_mutex_unlock(&db_connection_list_head->list_lock);
            return E_MALLOC;
        }
        memset(db_connection, 0, sizeof(DBConnectionNode));

        init_entry(&db_connection->list_entry);

        db_connection->serial_id = i;
        db_connection->is_used = 0;
        db_connection->db = (PGconn*) PQconnectdb(conninfo);

        if(PQstatus(db_connection->db) != CONNECTION_OK){

            zlog_error(category_debug,
                       "Connect to database failed: %s",
                       PQerrorMessage(db_connection->db));

            pthread_mutex_unlock(&db_connection_list_head->list_lock);
            return E_SQL_OPEN_DATABASE;
        }

        insert_list_tail(&db_connection->list_entry, 
                         &db_connection_list_head->list_head);
    }

    pthread_mutex_unlock(&db_connection_list_head->list_lock);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_destroy_database_connection_pool(
    DBConnectionListHead * db_connection_list_head){

    List_Entry *current_list_entry = NULL;
    List_Entry *next_list_entry = NULL;
    DBConnectionNode *current_list_ptr = NULL;
    PGconn * conn = NULL;

    pthread_mutex_lock(&db_connection_list_head->list_lock);

    list_for_each_safe(current_list_entry,
                       next_list_entry, 
                       &db_connection_list_head->list_head){

        current_list_ptr = ListEntry(current_list_entry,
                                     DBConnectionNode,
                                     list_entry);

        conn = (PGconn*) current_list_ptr->db;
        PQfinish(conn);

        remove_list_node(current_list_entry);

        free(current_list_ptr);
    }

    pthread_mutex_unlock(&db_connection_list_head->list_lock);

    return WORK_SUCCESSFULLY;
}

static ErrorCode SQL_get_database_connection(
    DBConnectionListHead *db_connection_list_head,
    void **db,
    int *serial_id){

    List_Entry *current_list_entry = NULL;
    DBConnectionNode * current_list_ptr = NULL;
    int retry_times = SQL_GET_AVAILABLE_CONNECTION_RETRIES;

    while(retry_times --){

        pthread_mutex_lock(&db_connection_list_head->list_lock);

        list_for_each(current_list_entry,
                      &db_connection_list_head->list_head){
            
            current_list_ptr = ListEntry(current_list_entry,
                                         DBConnectionNode,
                                         list_entry);
       
            if(current_list_ptr->is_used == 0){

               *db = (PGconn*) current_list_ptr->db;
               *serial_id = current_list_ptr->serial_id;
               current_list_ptr->is_used = 1;

               pthread_mutex_unlock(&db_connection_list_head->list_lock);
               return WORK_SUCCESSFULLY;
           } 
        }
        pthread_mutex_unlock(&db_connection_list_head->list_lock);

    }

    return E_SQL_OPEN_DATABASE;
}

static ErrorCode SQL_release_database_connection(
    DBConnectionListHead *db_connection_list_head,
    int serial_id){

    List_Entry *current_list_entry = NULL;
    DBConnectionNode *current_list_ptr = NULL;

    pthread_mutex_lock(&db_connection_list_head->list_lock);

    list_for_each(current_list_entry,
                  &db_connection_list_head->list_head){
        current_list_ptr = ListEntry(current_list_entry,
                                     DBConnectionNode,
                                     list_entry);
        if(current_list_ptr->serial_id == serial_id){
            current_list_ptr->is_used = 0;

            pthread_mutex_unlock(&db_connection_list_head->list_lock);
            return WORK_SUCCESSFULLY;
        }    
    }

    pthread_mutex_unlock(&db_connection_list_head->list_lock);

    return E_SQL_OPEN_DATABASE;
}

ErrorCode SQL_vacuum_database(
    DBConnectionListHead *db_connection_list_head){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char *table_name[] = {"tracking_table",
                          "lbeacon_table",
                          "gateway_table",
                          "object_table",
                          "notification_table"};

    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *sql_template = "VACUUM %s;";
    int idx = 0;

    for(idx = 0; idx< sizeof(table_name)/sizeof(table_name[0]) ; idx++){

        memset(sql, 0, sizeof(sql));
        sprintf(sql, sql_template, table_name[idx]);

        /* Execute SQL statement */
        if(WORK_SUCCESSFULLY != 
           SQL_get_database_connection(db_connection_list_head, 
                                       &db_conn, 
                                       &db_serial_id)){
            zlog_error(category_debug,
                       "cannot operate database");

            continue;
        }

        ret_val = SQL_execute(db_conn, sql);

        if(WORK_SUCCESSFULLY != ret_val){

            SQL_release_database_connection(
                db_connection_list_head,
                db_serial_id);

            zlog_error(category_debug,
                       "cannot operate database");

            return E_SQL_EXECUTE;
        }

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

    }

    return WORK_SUCCESSFULLY;
}


ErrorCode SQL_delete_old_data(
    DBConnectionListHead *db_connection_list_head,                              
    int retention_hours){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *table_name[] = {"notification_table"};
    char *sql_template = "DELETE FROM %s WHERE " \
                         "violation_timestamp < " \
                         "NOW() - INTERVAL \'%d HOURS\';";
    int idx = 0;
    char *tsdb_table_name[] = {"tracking_table"};
    char *sql_tsdb_template = "SELECT drop_chunks(interval \'%d HOURS\', " \
                              "\'%s\');";
    PGresult *res;


    for(idx = 0; idx< sizeof(table_name)/sizeof(table_name[0]); idx++){

        memset(sql, 0, sizeof(sql));

        sprintf(sql, sql_template, table_name[idx], retention_hours);

        if(WORK_SUCCESSFULLY != 
           SQL_get_database_connection(db_connection_list_head, 
                                       &db_conn, 
                                       &db_serial_id)){
            zlog_error(category_debug,
                       "cannot operate database\n");

            continue;
        }

        ret_val = SQL_execute(db_conn, sql);

        if(WORK_SUCCESSFULLY != ret_val){
            
            SQL_release_database_connection(
                db_connection_list_head,
                db_serial_id);

            zlog_error(category_debug,
                       "cannot operate database\n");

            return E_SQL_EXECUTE;
        }
        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);
    }

    for(idx = 0; 
        idx< sizeof(tsdb_table_name)/sizeof(tsdb_table_name[0]); 
        idx++){

        memset(sql, 0, sizeof(sql));

        sprintf(sql, sql_tsdb_template, retention_hours, 
                tsdb_table_name[idx]);

        /* Execute SQL statement */
        zlog_info(category_debug, "SQL command = [%s]", sql);

        if(WORK_SUCCESSFULLY != 
           SQL_get_database_connection(db_connection_list_head, 
                                       &db_conn, 
                                       &db_serial_id)){
            zlog_error(category_debug,
                       "cannot operate database\n");

            continue;
        }
        res = PQexec(db_conn, sql);
        if(PQresultStatus(res) != PGRES_TUPLES_OK){

            PQclear(res);
            zlog_info(category_debug, "SQL_execute failed: %s", 
                      PQerrorMessage(db_conn));

            SQL_release_database_connection(
                db_connection_list_head,
                db_serial_id);

            return E_SQL_EXECUTE;

        }
        PQclear(res);
        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);
    }

    return WORK_SUCCESSFULLY;
}


ErrorCode SQL_update_gateway_registration_status(
    DBConnectionListHead *db_connection_list_head,
    char *buf,
    size_t buf_len){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char temp_buf[WIFI_MESSAGE_LENGTH];
    char *saveptr = NULL;
    char *numbers_str = NULL;
    int numbers = 0;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *sql_template = "INSERT INTO gateway_table " \
                         "(ip_address, " \
                         "health_status, " \
                         "registered_timestamp, " \
                         "last_report_timestamp) " \
                         "VALUES " \
                         "(%s, \'%d\', NOW(), NOW())" \
                         "ON CONFLICT (ip_address) " \
                         "DO UPDATE SET health_status = \'%d\', " \
                         "last_report_timestamp = NOW();";
    HealthStatus health_status = S_NORMAL_STATUS;
    char *ip_address = NULL;
    char *pqescape_ip_address = NULL;


    memset(temp_buf, 0, sizeof(temp_buf));
    memcpy(temp_buf, buf, buf_len);

    numbers_str = strtok_save(temp_buf, DELIMITER_SEMICOLON, &saveptr);
    if(numbers_str == NULL){
        return E_API_PROTOCOL_FORMAT;
    }
    numbers = atoi(numbers_str);

    if(numbers <= 0){
        return E_SQL_PARSE;
    }

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    while( numbers-- ){
        
        ip_address = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);
       
        /* Create SQL statement */
        pqescape_ip_address =
            PQescapeLiteral(db_conn, ip_address, strlen(ip_address));

        memset(sql, 0, sizeof(sql));
        sprintf(sql, sql_template,
                pqescape_ip_address,
                health_status, health_status);

        PQfreemem(pqescape_ip_address);

        /* Execute SQL statement */
        ret_val = SQL_execute(db_conn, sql);

        if(WORK_SUCCESSFULLY != ret_val){
            
            SQL_release_database_connection(
                db_connection_list_head,
                db_serial_id);

            return E_SQL_EXECUTE;
        }
    }

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_update_lbeacon_registration_status(
    DBConnectionListHead *db_connection_list_head,
    char *buf,
    size_t buf_len,
    char *gateway_ip_address){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char temp_buf[WIFI_MESSAGE_LENGTH];
    char *saveptr = NULL;
    char *numbers_str = NULL;
    int numbers = 0;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *sql_template = "INSERT INTO lbeacon_table " \
                         "(uuid, " \
                         "ip_address, " \
                         "health_status, " \
                         "gateway_ip_address, " \
                         "registered_timestamp, " \
                         "last_report_timestamp, " \
                         "coordinate_x, " \
                         "coordinate_y) " \
                         "VALUES " \
                         "(%s, %s, \'%d\', %s, " \
                         "TIMESTAMP \'epoch\' + %s * \'1 second\'::interval, " \
                         "NOW(), " \
                         "%d, %d) " \
                         "ON CONFLICT (uuid) " \
                         "DO UPDATE SET ip_address = %s, " \
                         "health_status = \'%d\', " \
                         "gateway_ip_address = %s, " \
                         "last_report_timestamp = NOW(), " \
                         "coordinate_x = %d, " \
                         "coordinate_y = %d;";
    HealthStatus health_status = S_NORMAL_STATUS;
    char *uuid = NULL;
    char *lbeacon_ip = NULL;
    char *not_used_gateway_ip = NULL;
    char *registered_timestamp_GMT = NULL;
    char *pqescape_uuid = NULL;
    char *pqescape_lbeacon_ip = NULL;
    char *pqescape_gateway_ip = NULL;
    char *pqescape_registered_timestamp_GMT = NULL;
    char str_uuid[LENGTH_OF_UUID];
    char coordinate_x[LENGTH_OF_UUID];
    char coordinate_y[LENGTH_OF_UUID];
    int int_coordinate_x = 0;
    int int_coordinate_y = 0;
	const int INDEX_OF_COORDINATE_X_IN_UUID = 12;
    const int INDEX_OF_COORDINATE_Y_IN_UUID = 24;
    const int LENGTH_OF_COORDINATE_IN_UUID = 8;
    
	
    memset(temp_buf, 0, sizeof(temp_buf));
    memcpy(temp_buf, buf, buf_len);

    numbers_str = strtok_save(temp_buf, DELIMITER_SEMICOLON, &saveptr);
    if(numbers_str == NULL){
        return E_API_PROTOCOL_FORMAT;
    }
    numbers = atoi(numbers_str);

    if(numbers <= 0){
        return E_SQL_PARSE;
    }

    not_used_gateway_ip = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    while( numbers-- ){
        uuid = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

        memset(str_uuid, 0, sizeof(str_uuid));
        strcpy(str_uuid, uuid);

        memset(coordinate_x, 0, sizeof(coordinate_x));
        memset(coordinate_y, 0, sizeof(coordinate_y));
        
        strncpy(coordinate_x, 
                &str_uuid[INDEX_OF_COORDINATE_X_IN_UUID], 
                LENGTH_OF_COORDINATE_IN_UUID);
        strncpy(coordinate_y, 
                &str_uuid[INDEX_OF_COORDINATE_Y_IN_UUID], 
                LENGTH_OF_COORDINATE_IN_UUID);

        int_coordinate_x = atoi(coordinate_x);
        int_coordinate_y = atoi(coordinate_y);

        registered_timestamp_GMT = 
            strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);
        lbeacon_ip = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

        /* Create SQL statement */
        memset(sql, 0, sizeof(sql));

        pqescape_uuid = 
            PQescapeLiteral(db_conn, uuid, strlen(uuid));
        pqescape_lbeacon_ip =
            PQescapeLiteral(db_conn, lbeacon_ip, strlen(lbeacon_ip));
        pqescape_gateway_ip =
            PQescapeLiteral(db_conn, gateway_ip_address, 
                            strlen(gateway_ip_address));
        pqescape_registered_timestamp_GMT =
            PQescapeLiteral(db_conn, registered_timestamp_GMT,
                            strlen(registered_timestamp_GMT));

        sprintf(sql, sql_template,
                pqescape_uuid,
                pqescape_lbeacon_ip,
                health_status,
                pqescape_gateway_ip,
                pqescape_registered_timestamp_GMT,
                int_coordinate_x,
                int_coordinate_y,
                pqescape_lbeacon_ip,
                health_status,
                pqescape_gateway_ip,
                int_coordinate_x,
                int_coordinate_y);

        PQfreemem(pqescape_uuid);
        PQfreemem(pqescape_lbeacon_ip);
        PQfreemem(pqescape_gateway_ip);
        PQfreemem(pqescape_registered_timestamp_GMT);

        /* Execute SQL statement */
        ret_val = SQL_execute(db_conn, sql);

        if(WORK_SUCCESSFULLY != ret_val){
            SQL_release_database_connection(
                db_connection_list_head,
                db_serial_id);
            return E_SQL_EXECUTE;
        }

    }

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_update_gateway_health_status(
    DBConnectionListHead *db_connection_list_head,
    char *buf,
    size_t buf_len,
    char *gateway_ip_address){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char temp_buf[WIFI_MESSAGE_LENGTH];
    char *saveptr = NULL;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *sql_template = "UPDATE gateway_table " \
                         "SET health_status = %s, " \
                         "last_report_timestamp = NOW() " \
                         "WHERE ip_address = %s ;" ;
    char *not_used_ip_address = NULL;
    char *health_status = NULL;
    char *pqescape_ip_address = NULL;
    char *pqescape_health_status = NULL;


    memset(temp_buf, 0, sizeof(temp_buf));
    memcpy(temp_buf, buf, buf_len);

    not_used_ip_address = strtok_save(temp_buf, DELIMITER_SEMICOLON, &saveptr);
    health_status = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

    /* Create SQL statement */
    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    pqescape_ip_address =
        PQescapeLiteral(db_conn, gateway_ip_address, strlen(gateway_ip_address));
    pqescape_health_status =
        PQescapeLiteral(db_conn, health_status, strlen(health_status));

    memset(sql, 0, sizeof(sql));
    sprintf(sql, sql_template,
            pqescape_health_status,
            pqescape_ip_address);

    PQfreemem(pqescape_ip_address);
    PQfreemem(pqescape_health_status);

    /* Execute SQL statement */
    ret_val = SQL_execute(db_conn, sql);

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    if(WORK_SUCCESSFULLY != ret_val){
    
        return E_SQL_EXECUTE;
    }

    return WORK_SUCCESSFULLY;
}


ErrorCode SQL_update_lbeacon_health_status(
    DBConnectionListHead *db_connection_list_head,
    char *buf,
    size_t buf_len,
    char *gateway_ip_address){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char temp_buf[WIFI_MESSAGE_LENGTH];
    char *saveptr = NULL;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *sql_template = "UPDATE lbeacon_table " \
                         "SET health_status = %s, " \
                         "last_report_timestamp = NOW(), " \
						 "gateway_ip_address = %s " \
                         "WHERE uuid = %s ;";
    char *lbeacon_uuid = NULL;
    char *lbeacon_timestamp = NULL;
    char *lbeacon_ip = NULL;
    char *health_status = NULL;
    char *pqescape_lbeacon_uuid = NULL;
    char *pqescape_health_status = NULL;
    char *pqescape_gateway_ip = NULL;
 
 
    memset(temp_buf, 0, sizeof(temp_buf));
    memcpy(temp_buf, buf, buf_len);

    lbeacon_uuid = strtok_save(temp_buf, DELIMITER_SEMICOLON, &saveptr);	
    lbeacon_timestamp = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);
    lbeacon_ip = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);
    health_status = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);


    /* Create SQL statement */
    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }
    pqescape_lbeacon_uuid = 
        PQescapeLiteral(db_conn, lbeacon_uuid, strlen(lbeacon_uuid));
    pqescape_health_status =
        PQescapeLiteral(db_conn, health_status, strlen(health_status));
    pqescape_gateway_ip = 
        PQescapeLiteral(db_conn, gateway_ip_address, strlen(gateway_ip_address));

    memset(sql, 0, sizeof(sql));
    sprintf(sql, sql_template,
            pqescape_health_status,
		    pqescape_gateway_ip,
            pqescape_lbeacon_uuid);

    PQfreemem(pqescape_lbeacon_uuid);
    PQfreemem(pqescape_health_status);
    PQfreemem(pqescape_gateway_ip);

    /* Execute SQL statement */
    ret_val = SQL_execute(db_conn, sql);

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    if(WORK_SUCCESSFULLY != ret_val){
        return E_SQL_EXECUTE;
    }

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_update_object_tracking_data_with_battery_voltage(
    DBConnectionListHead *db_connection_list_head,
    char *buf,
    size_t buf_len,
    char *server_installation_path,
    int is_enabled_panic_monitoring){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char temp_buf[WIFI_MESSAGE_LENGTH];
    char *saveptr = NULL;
    int num_types = 2; // BR_EDR and BLE types
    char *sql_bulk_insert_template = 
                         "COPY " \
                         "tracking_table " \
                         "(object_mac_address, " \
                         "lbeacon_uuid, " \
                         "rssi, " \
                         "panic_button, " \
                         "battery_voltage, " \
                         "initial_timestamp, " \
                         "final_timestamp, " \
                         "server_time_offset) " \
                         "FROM " \
                         "\'%s\' " \
                         "DELIMITER \',\' CSV;";
    
    char *lbeacon_uuid = NULL;
    char *lbeacon_ip = NULL;
    char *lbeacon_timestamp = NULL;
    char *object_type = NULL;
    char *object_number = NULL;
    int numbers = 0;
    char *object_mac_address = NULL;
    char *initial_timestamp_GMT = NULL;
    char *final_timestamp_GMT = NULL;
    char *rssi = NULL;
    char *panic_button = NULL;
    char *battery_voltage = NULL;
    int current_time = get_system_time();
    int lbeacon_timestamp_value;
    char filename[MAX_PATH];
    FILE *file = NULL;
    time_t rawtime;
    struct tm ts;
    char buf_initial_time[80];
    char buf_final_time[80];

    char *sql_identify_panic = 
        "UPDATE object_summary_table " \
        "SET panic_violation_timestamp = NOW() " \
        "FROM object_summary_table as R " \
        "INNER JOIN object_table " \
        "ON R.mac_address = object_table.mac_address " \
        "WHERE object_summary_table.mac_address = %s " \
        "AND object_table.monitor_type & %d = %d;";

    char *pqescape_mac_address = NULL;

   
    /* Open temporary file with thread id as filename to prepare the tracking 
       data for postgresql bulk-insertion */
    memset(filename, 0, sizeof(filename));
    sprintf(filename, "%s/temp/track_%d", 
            server_installation_path, 
            pthread_self());
    
    file = fopen(filename, "wt");
    if(file == NULL){
        zlog_error(category_debug, "cannot open filepath %s", filename);
        return E_OPEN_FILE;
    }

    /* Parse the message buffer */
    memset(temp_buf, 0, sizeof(temp_buf));
    memcpy(temp_buf, buf, buf_len);

    lbeacon_uuid = strtok_save(temp_buf, DELIMITER_SEMICOLON, &saveptr);
    lbeacon_timestamp = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);
    if(lbeacon_timestamp == NULL){
        fclose(file);
        return E_API_PROTOCOL_FORMAT;
    }
    lbeacon_timestamp_value = atoi(lbeacon_timestamp);
    lbeacon_ip = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

    zlog_debug(category_debug, "lbeacon_uuid=[%s], lbeacon_timestamp=[%s], " \
               "lbeacon_ip=[%s]", lbeacon_uuid, lbeacon_timestamp, lbeacon_ip);

    while(num_types --){

        object_type = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

        object_number = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

        zlog_debug(category_debug, "object_type=[%s], object_number=[%s]", 
                   object_type, object_number);

        if(object_number == NULL){
            fclose(file);
            return E_API_PROTOCOL_FORMAT;
        }
        numbers = atoi(object_number);

        while(numbers--){

            object_mac_address = 
                strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

            initial_timestamp_GMT = 
                strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

            final_timestamp_GMT = 
                strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

            rssi = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

            panic_button = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);

            if(panic_button != NULL && 1 == atoi(panic_button)){
                
                memset(sql, 0, sizeof(sql));
                if(WORK_SUCCESSFULLY != 
                   SQL_get_database_connection(db_connection_list_head, 
                                               &db_conn, 
                                               &db_serial_id)){

                    zlog_error(category_debug,
                               "cannot open database\n");

                    continue;
                }

                pqescape_mac_address = 
                    PQescapeLiteral(db_conn, object_mac_address, 
                                    strlen(object_mac_address)); 
   
                sprintf(sql, sql_identify_panic, 
                        pqescape_mac_address, 
                        MONITOR_PANIC,
                        MONITOR_PANIC);

                PQfreemem(pqescape_mac_address);

                ret_val = SQL_execute(db_conn, sql);

                SQL_release_database_connection(
                    db_connection_list_head, 
                    db_serial_id);
            }

            battery_voltage = strtok_save(NULL, DELIMITER_SEMICOLON, &saveptr);
           
            // Convert Unix epoch timestamp (since 1970-1-1) to 
            // postgre timestamp (since 2000-1-1)
            rawtime = atoi(initial_timestamp_GMT);
            ts = *gmtime(&rawtime);
            strftime(buf_initial_time, sizeof(buf_initial_time), 
                     "%Y-%m-%d %H:%M:%S", &ts);
            
            rawtime = atoi(final_timestamp_GMT);
            ts = *gmtime(&rawtime);
            strftime(buf_final_time, sizeof(buf_final_time), 
                     "%Y-%m-%d %H:%M:%S", &ts);
                      
            fprintf(file, "%s,%s,%s,%s,%s,%s,%s,%d\n",
                    object_mac_address,
                    lbeacon_uuid,
                    rssi,
                    panic_button,
                    battery_voltage,
                    buf_initial_time,
                    buf_final_time,
                    current_time - lbeacon_timestamp_value);
        }
    }
    fclose(file);
    
    memset(sql, 0, sizeof(sql));
    sprintf(sql, sql_bulk_insert_template, filename); 

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot open database\n");

        return E_SQL_OPEN_DATABASE;
    }

    /* Execute SQL statement */
    ret_val = SQL_execute(db_conn, sql);

    SQL_release_database_connection(
        db_connection_list_head, 
        db_serial_id);

    remove(filename);

    if(WORK_SUCCESSFULLY != ret_val){
        return E_SQL_EXECUTE;
    }

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_summarize_object_location(
    DBConnectionListHead *db_connection_list_head,
    int database_pre_filter_time_window_in_sec,
    int time_interval_in_sec,
    int rssi_difference_of_location_accuracy_tolerance,
    int base_location_tolerance_in_millimeter){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];

    char *sql_reset_state_template = 
        "UPDATE object_summary_table "\
        "SET is_location_updated = 0 " \
        "WHERE id > 0";

    char *sql_update_stable_tag_template = 
        "UPDATE object_summary_table " \
        "SET " \
        "rssi = avg_rssi, last_seen_timestamp = final_timestamp, " \
        "battery_voltage = stable_table.battery_voltage, " \
        "is_location_updated = 1 " \
        "FROM ( " \
        "SELECT mac_address, uuid, avg_rssi, final_timestamp, " \
        "recent_table.battery_voltage " \
        "FROM " \
        "object_summary_table " \
        "INNER JOIN " \
        "(SELECT object_mac_address, " \
        "lbeacon_uuid, " \
        "ROUND(AVG(rssi), 0) as avg_rssi, " \
        "MAX(final_timestamp) as final_timestamp, " \
        "MIN(battery_voltage) as battery_voltage " \
        "FROM " \
        "tracking_table " \
        "WHERE " \
        "final_timestamp > NOW() - interval '%d seconds' AND " \
        "final_timestamp >= NOW() - (server_time_offset|| 'seconds')::INTERVAL - " \
        "INTERVAL '%d seconds' " \
        "GROUP BY object_mac_address, lbeacon_uuid " \
        ") recent_table " \
        "ON object_summary_table.mac_address = recent_table.object_mac_address AND " \
        "object_summary_table.uuid = recent_table.lbeacon_uuid " \
        "INNER JOIN " \
        "(SELECT * " \
        "FROM " \
        "(SELECT " \
        "ROW_NUMBER() OVER ( " \
        "PARTITION BY object_mac_address " \
        "ORDER BY object_mac_address ASC, average_rssi DESC " \
        ") as rank, " \
        "object_beacon_rssi_table.* " \
        "FROM " \
        "( SELECT " \
        "t.object_mac_address, t.lbeacon_uuid, ROUND(AVG(rssi), 0) as average_rssi " \
        "FROM " \
        "tracking_table t "\
        "WHERE " \
        "final_timestamp >= NOW() - INTERVAL '%d seconds' AND " \
        "final_timestamp >= NOW() - (server_time_offset || 'seconds')::INTERVAL - " \
        "INTERVAL '%d seconds' " \
        "GROUP BY " \
        "object_mac_address, " \
        "lbeacon_uuid " \
        "HAVING AVG(rssi) > -100 " \
        "ORDER BY " \
        "object_mac_address ASC, " \
        "average_rssi DESC, " \
        "lbeacon_uuid ASC " \
        ") object_beacon_rssi_table " \
        ") object_location_table " \
        "WHERE object_location_table.rank <= 1 " \
	    ") location_information " \
        "ON recent_table.object_mac_address = location_information.object_mac_address AND " \
        "ABS(recent_table.avg_rssi - location_information.average_rssi) < %d " \
        ") stable_table where object_summary_table.mac_address = stable_table.mac_address AND " \
        "object_summary_table.uuid = stable_table.uuid; ";
        
    char *sql_update_moving_tag_template = 
        "UPDATE object_summary_table " \
        "SET " \
        "first_seen_timestamp = CASE " \
        "WHEN first_seen_timestamp IS NULL OR " \
        "object_summary_table.uuid != location_information.lbeacon_uuid " \
        "THEN " \
        "location_information.initial_timestamp " \
        "ELSE first_seen_timestamp " \
        "END, " \
        "rssi = location_information.avg_rssi, " \
        "battery_voltage = location_information.battery_voltage, " \
        "last_seen_timestamp = location_information.final_timestamp, " \
        "uuid = location_information.lbeacon_uuid, " \
        "is_location_updated = 1 " \
        "FROM " \
        "(SELECT " \
        "object_mac_address, " \
        "lbeacon_uuid, " \
        "avg_rssi, " \
        "battery_voltage, " \
        "initial_timestamp, " \
        "final_timestamp " \
        "FROM " \
        "(SELECT " \
        "ROW_NUMBER() OVER (" \
        "PARTITION BY object_mac_address " \
        "ORDER BY object_mac_address ASC, avg_rssi DESC" \
        ") as rank, " \
        "object_beacon_rssi_table.* " \
        "FROM "\
        "(SELECT " \
        "t.object_mac_address, " \
        "t.lbeacon_uuid, " \
        "ROUND(AVG(rssi), 0) as avg_rssi, " \
        "MIN(battery_voltage) as battery_voltage, " \
        "MIN(initial_timestamp) as initial_timestamp, " \
        "MAX(final_timestamp) as final_timestamp " \
        "FROM " \
        "tracking_table t " \
        "WHERE " \
        "final_timestamp >= NOW() - INTERVAL '%d seconds' AND " \
        "final_timestamp >= NOW() - (server_time_offset || 'seconds')::INTERVAL - " \
        "INTERVAL '%d seconds' " \
        "GROUP BY " \
        "object_mac_address, " \
        "lbeacon_uuid " \
        "HAVING AVG(rssi) > -100 " \
        "ORDER BY " \
        "object_mac_address ASC, " \
        "avg_rssi DESC, " \
        "lbeacon_uuid ASC" \
        ") object_beacon_rssi_table " \
        ") object_location_table " \
        "WHERE " \
        "object_location_table.rank <= 1 " \
        ") location_information " \
        "WHERE " \
        "object_summary_table.mac_address = " \
        "location_information.object_mac_address AND " \
		"object_summary_table.is_location_updated = 0;";
		
	char *sql_update_tag_base_location_template = 
	    "UPDATE object_summary_table "\
        "SET " \
        "base_x = tag_new_base.base_x, " \
	    "base_y = tag_new_base.base_y " \
        "FROM " \
        "(SELECT " \
        "object_mac_address, " \
        "ROUND(SUM(coordinate_x*weight)/SUM(weight),0) as base_x, " \
        "ROUND(SUM(coordinate_y*weight)/SUM(weight),0) as base_y " \
        "FROM " \
        "(SELECT " \
	    "object_mac_address, " \
        "lbeacon_uuid, " \
	    "ROUND(AVG(rssi),0) as average_rssi, " \
        "(SELECT weight from rssi_weight_table " \
        "WHERE avg(rssi) >= bottom_rssi AND avg(rssi) < upper_rssi LIMIT 1) " \
        "AS weight " \
        "FROM tracking_table " \
        "WHERE " \
        "final_timestamp > NOW() - interval '%d seconds' AND " \
        "final_timestamp >= NOW() - (server_time_offset || 'seconds')::INTERVAL - " \
        "INTERVAL '%d seconds' " \
        "GROUP BY object_mac_address, lbeacon_uuid " \
        "HAVING avg(rssi) >  -100" \
        "ORDER BY object_mac_address, lbeacon_uuid, average_rssi DESC " \
        ") beacon_rssi_table " \
        "INNER JOIN " \
        "lbeacon_table " \
        "ON beacon_rssi_table.lbeacon_uuid = lbeacon_table.uuid " \
        "GROUP BY object_mac_address) tag_new_base " \
        "WHERE object_summary_table.mac_address = tag_new_base.object_mac_address " \
        "AND " \
        "(" \
        "object_summary_table.base_x IS NULL " \
        "OR " \
        "object_summary_table.base_y IS NULL " \
        "OR " \
        "(ABS(object_summary_table.base_x - tag_new_base.base_x) >= %d) " \
        "OR " \
        "(ABS(object_summary_table.base_y - tag_new_base.base_y) >= %d) " \
        ")";

    memset(sql, 0, sizeof(sql));
    sprintf(sql, sql_reset_state_template);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot open database\n");

        return E_SQL_OPEN_DATABASE;
    }

    ret_val = SQL_execute(db_conn, sql);

    /* Update stable tags */
    memset(sql, 0, sizeof(sql));

    sprintf(sql, sql_update_stable_tag_template,
            database_pre_filter_time_window_in_sec,
            time_interval_in_sec,
            database_pre_filter_time_window_in_sec,
            time_interval_in_sec,
            rssi_difference_of_location_accuracy_tolerance);
  
    ret_val = SQL_execute(db_conn, sql);

    if(WORK_SUCCESSFULLY != ret_val){
        zlog_error(category_debug, "SQL_execute failed [%d]: %s",
                   ret_val, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }

    /* Update moving tags */
    memset(sql, 0, sizeof(sql));

    sprintf(sql, sql_update_moving_tag_template, 
            database_pre_filter_time_window_in_sec, 
            time_interval_in_sec);
  
    ret_val = SQL_execute(db_conn, sql);
    if(WORK_SUCCESSFULLY != ret_val){
        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   ret_val, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }
	
	/* Update base location of tags */
    memset(sql, 0, sizeof(sql));

    sprintf(sql, sql_update_tag_base_location_template, 
            database_pre_filter_time_window_in_sec, 
            time_interval_in_sec,
            base_location_tolerance_in_millimeter,
            base_location_tolerance_in_millimeter);
  
    ret_val = SQL_execute(db_conn, sql);
    if(WORK_SUCCESSFULLY != ret_val){
        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   ret_val, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);
    
    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_identify_geofence_violation(
    DBConnectionListHead *db_connection_list_head,
    char *mac_address){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];

    char *sql_insert_summary_table = 
        "UPDATE object_summary_table " \
        "SET " \
        "geofence_violation_timestamp = NOW() " \
        "WHERE mac_address = %s";

    char *pqescape_mac_address = NULL;

    memset(sql, 0, sizeof(sql));

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    pqescape_mac_address = 
        PQescapeLiteral(db_conn, mac_address, 
                        strlen(mac_address)); 
   
    sprintf(sql, 
            sql_insert_summary_table, 
            pqescape_mac_address);
    
    ret_val = SQL_execute(db_conn, sql);

    PQfreemem(pqescape_mac_address);
   
    if(WORK_SUCCESSFULLY != ret_val){
        
        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   ret_val, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }     

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);
    
    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_identify_location_not_stay_room(
    DBConnectionListHead *db_connection_list_head){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *sql_select_template = "UPDATE object_summary_table " \
                                "SET " \
                                "location_violation_timestamp = NOW() " \
                                "FROM " \
                                "(SELECT " \
                                "object_summary_table.mac_address, " \
                                "object_summary_table.uuid, " \
                                "monitor_type, " \
                                "lbeacon_table.room, " \
                                "object_table.room " \
                                "FROM "\
                                "object_summary_table " \
                                "INNER JOIN object_table ON " \
                                "object_summary_table.mac_address = " \
                                "object_table.mac_address " \
                                "INNER JOIN lbeacon_table ON " \
                                "object_summary_table.uuid = " \
                                "lbeacon_table.uuid " \
                                "INNER JOIN location_not_stay_room_config ON " \
                                "object_table.area_id = " \
                                "location_not_stay_room_config.area_id " \
                                "WHERE " \
                                "location_not_stay_room_config.is_active = 1 " \
                                "AND monitor_type & %d = %d " \
                                "AND lbeacon_table.room <> object_table.room " \
                                ") location_information " \
                                "WHERE object_summary_table.mac_address = " \
                                "location_information.mac_address;";

    memset(sql, 0, sizeof(sql));

    sprintf(sql, sql_select_template, 
            MONITOR_LOCATION,
            MONITOR_LOCATION);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    ret_val = SQL_execute(db_conn, sql);

    if(WORK_SUCCESSFULLY != ret_val){
        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   ret_val, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);
    
    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_identify_location_long_stay_in_danger(
    DBConnectionListHead *db_connection_list_head){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *sql_select_template = "UPDATE object_summary_table " \
                                "SET " \
                                "location_violation_timestamp = NOW() " \
                                "FROM " \
                                "(SELECT " \
                                "object_summary_table.mac_address, " \
                                "object_summary_table.uuid, " \
                                "monitor_type, " \
                                "danger_area " \
                                "FROM "\
                                "object_summary_table " \
                                "INNER JOIN object_table ON " \
                                "object_summary_table.mac_address = " \
                                "object_table.mac_address " \
                                "INNER JOIN lbeacon_table ON " \
                                "object_summary_table.uuid = " \
                                "lbeacon_table.uuid " \
                                "INNER JOIN location_long_stay_in_danger_config ON " \
                                "object_table.area_id = " \
                                "location_long_stay_in_danger_config.area_id " \
                                "WHERE " \
                                "location_long_stay_in_danger_config.is_active = 1 " \
                                "AND monitor_type & %d = %d " \
                                "AND danger_area = 1 " \
                                "AND EXTRACT(MIN FROM last_seen_timestamp - " \
                                "first_seen_timestamp) > "\
                                "location_long_stay_in_danger_config.stay_duration " \
                                ") location_information " \
                                "WHERE object_summary_table.mac_address = " \
                                "location_information.mac_address;";


    memset(sql, 0, sizeof(sql));

    sprintf(sql, sql_select_template, 
            MONITOR_LOCATION,
            MONITOR_LOCATION);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    ret_val = SQL_execute(db_conn, sql);

    if(WORK_SUCCESSFULLY != ret_val){
        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   ret_val, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_identify_last_movement_status(
    DBConnectionListHead *db_connection_list_head,
    int time_interval_in_min, 
    int each_time_slot_in_min,
    unsigned int rssi_delta){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];

    char *sql_select_template = "SELECT " \
                                "object_summary_table.mac_address, " \
                                "object_summary_table.uuid " \
                                "FROM object_summary_table " \
                                "INNER JOIN object_table ON " \
                                "object_summary_table.mac_address = " \
                                "object_table.mac_address " \
                                "INNER JOIN movement_config ON " \
                                "object_table.area_id = " \
                                "movement_config.area_id " \
                                "WHERE " \
                                "movement_config.is_active = 1 AND " \
                                "object_table.monitor_type & %d = %d " \
                                "ORDER BY " \
                                "mac_address ASC";

    const int NUMBER_FIELDS_OF_SQL_SELECT_TEMPLATE = 2;
    const int FIELD_INDEX_OF_MAC_ADDRESS = 0;
    const int FIELD_INDEX_OF_UUID = 1;

    PGresult *res = NULL;
    int current_row = 0;
    int total_fields = 0;
    int total_rows = 0;

    char *mac_address = NULL;
    char *lbeacon_uuid = NULL;

    char *sql_select_activity_template = 
        "SELECT time_slot, avg_rssi, diff " \
        "FROM ( " \
        "SELECT time_slot, avg_rssi, avg_rssi - LAG(avg_rssi) " \
        "OVER (ORDER BY time_slot) as diff " \
        "FROM ( " \
        "SELECT TIME_BUCKET('%d minutes', final_timestamp) as time_slot, " \
        "AVG(rssi) as avg_rssi " \
        "FROM tracking_table where " \
        "final_timestamp > NOW() - INTERVAL '%d minutes' " \
        "AND lbeacon_uuid = %s " \
        "AND object_mac_address = %s " \
        "GROUP BY time_slot" \
        ") " \
        "AS temp_time_slot_table )" \
        "AS temp_delta " \
        "WHERE diff > %d or diff < %d " \
        "ORDER BY time_slot DESC;";

    char *pqescape_mac_address = NULL;
    char *pqescape_lbeacon_uuid = NULL;

    PGresult *res_activity = NULL;
    int rows_activity = 0;
   
    char *time_slot_activity = NULL;

    char *sql_update_activity_template = 
        "UPDATE object_summary_table " \
        "SET movement_violation_timestamp = NOW()" \
        "WHERE mac_address = %s";
  
    memset(sql, 0, sizeof(sql));

    sprintf(sql, sql_select_template,
            MONITOR_MOVEMENT,
            MONITOR_MOVEMENT);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    res = PQexec(db_conn, sql);

    if(PQresultStatus(res) != PGRES_TUPLES_OK){
        PQclear(res);

        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   res, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }

    total_fields = PQnfields(res);
    total_rows = PQntuples(res);

    if(total_rows > 0 && 
       total_fields == NUMBER_FIELDS_OF_SQL_SELECT_TEMPLATE){

        for(current_row = 0 ; current_row < total_rows ; current_row++){
            mac_address = PQgetvalue(res, 
                                     current_row, 
                                     FIELD_INDEX_OF_MAC_ADDRESS);

            lbeacon_uuid = PQgetvalue(res, 
                                      current_row, 
                                      FIELD_INDEX_OF_UUID);

            if(strlen(lbeacon_uuid) == 0){
                continue;
            }

            pqescape_mac_address = 
                PQescapeLiteral(db_conn, mac_address, strlen(mac_address));
            pqescape_lbeacon_uuid = 
                PQescapeLiteral(db_conn, lbeacon_uuid, strlen(lbeacon_uuid));

            sprintf(sql, sql_select_activity_template, 
                    each_time_slot_in_min,
                    time_interval_in_min,
                    pqescape_lbeacon_uuid,
                    pqescape_mac_address,
                    rssi_delta,
                    0 - rssi_delta);

            res_activity = PQexec(db_conn, sql);

            PQfreemem(pqescape_mac_address);
            PQfreemem(pqescape_lbeacon_uuid);

            if(PQresultStatus(res_activity) != PGRES_TUPLES_OK){
                PQclear(res_activity);
                PQclear(res);
                    
                zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                           res_activity, PQerrorMessage(db_conn));

                SQL_release_database_connection(
                    db_connection_list_head,
                    db_serial_id);

                return E_SQL_EXECUTE;
            }

            rows_activity = PQntuples(res_activity);
         
            if(rows_activity == 0){
                pqescape_mac_address = 
                    PQescapeLiteral(db_conn, mac_address, 
                                    strlen(mac_address));
                
                memset(sql, 0, sizeof(sql));
                    
                sprintf(sql, sql_update_activity_template,
                        pqescape_mac_address);
                            
                ret_val = SQL_execute(db_conn, sql);

                PQfreemem(pqescape_mac_address);
               
                if(WORK_SUCCESSFULLY != ret_val){
                    PQclear(res_activity);   
                    PQclear(res);

                    zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                               ret_val, PQerrorMessage(db_conn));

                    SQL_release_database_connection(
                        db_connection_list_head,
                        db_serial_id);

                    return E_SQL_EXECUTE;
                }     
                PQclear(res_activity);

                continue;
            }
            PQclear(res_activity);
        }
    }

    PQclear(res);
    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_collect_violation_events(
    DBConnectionListHead *db_connection_list_head,
    ObjectMonitorType monitor_type,
    int time_interval_in_sec,
    int granularity_for_continuous_violations_in_sec){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];

    char *sql_insert_template = 
        "INSERT INTO " \
        "notification_table( " \
        "monitor_type, " \
        "mac_address, " \
        "uuid, " \
        "violation_timestamp, " \
        "processed) " \
        "SELECT %d, " \
        "mac_address, " \
        "uuid, " \
        "%s, " \
        "0 " \
        "FROM object_summary_table " \
        "WHERE "\
        "%s >= " \
        "NOW() - interval '%d seconds' " \
        "AND NOT EXISTS(" \
        "SELECT * FROM notification_table " \
        "WHERE monitor_type = %d " \
        "AND mac_address = mac_address " \
        "AND uuid = uuid " \
        "AND EXTRACT(EPOCH FROM(%s - " \
        "violation_timestamp)) < %d);";

    char *geofence_violation_timestamp = "geofence_violation_timestamp";
    char *panic_violation_timestamp = "panic_violation_timestamp";
    char *movement_violation_timestamp = "movement_violation_timestamp";
    char *location_violation_timestamp = "location_violation_timestamp";
    char *violation_timestamp_name = NULL;

    switch (monitor_type){
        case MONITOR_GEO_FENCE:
            violation_timestamp_name = geofence_violation_timestamp;
            break;
        case MONITOR_PANIC:
            violation_timestamp_name = panic_violation_timestamp;
            break;
        case MONITOR_MOVEMENT:
            violation_timestamp_name = movement_violation_timestamp;
            break;
        case MONITOR_LOCATION:
            violation_timestamp_name = location_violation_timestamp;
            break;
        default:
            zlog_error(category_debug, "Unknown monitor_type=[%d]", 
                       monitor_type);
            return E_INPUT_PARAMETER;
    }

    memset(sql, 0, sizeof(sql));
    sprintf(sql, 
            sql_insert_template, 
            monitor_type, 
            violation_timestamp_name,
            violation_timestamp_name,
            time_interval_in_sec,
            monitor_type,
            violation_timestamp_name,
            granularity_for_continuous_violations_in_sec);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    ret_val = SQL_execute(db_conn, sql);
    if(WORK_SUCCESSFULLY != ret_val){
        
        zlog_error(category_debug, 
                   "SQL_execute failed [%d]: %s", 
                   ret_val, 
                   PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }    
    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_get_and_update_violation_events(
    DBConnectionListHead *db_connection_list_head,
    char *buf,
    size_t buf_len){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];

    char *sql_select_template = 
        "SELECT id, monitor_type, mac_address, uuid, violation_timestamp " \
        "FROM "
        "notification_table " \
        "WHERE "\
        "processed != 1 " \
        "ORDER BY id ASC;";
    const int NUMBER_FIELDS_OF_SQL_SELECT_TEMPLATE = 5;
    const int FIELD_INDEX_OF_ID = 0;
    const int FIELD_INDEX_OF_MONITOR_TYPE = 1;
    const int FIELD_INDEX_OF_MAC_ADDRESS = 2;
    const int FIELD_INDEX_OF_UUID = 3;
    const int FIELD_INDEX_OF_VIOLATION_TIMESTAMP = 4;

    PGresult *res = NULL;
    int total_fields = 0;
    int total_rows = 0;
    int i;
    char one_record[SQL_TEMP_BUFFER_LENGTH];
    char *sql_update_template = 
        "UPDATE "
        "notification_table " \
        "SET "\
        "processed = 1 " \
        "WHERE id = %d;";

    int id_int;


    memset(sql, 0, sizeof(sql));
    sprintf(sql, sql_select_template);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
        zlog_error(category_debug,
                   "cannot operate database");

        return E_SQL_OPEN_DATABASE;
    }

    res = PQexec(db_conn, sql);

    if(PQresultStatus(res) != PGRES_TUPLES_OK){
        PQclear(res);

        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   res, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        return E_SQL_EXECUTE;
    }

    total_rows = PQntuples(res);
    total_fields = PQnfields(res);
    
    if(total_rows > 0 && 
       total_fields == NUMBER_FIELDS_OF_SQL_SELECT_TEMPLATE){
        for(i = 0 ; i < total_rows ; i++){
            memset(one_record, 0, sizeof(one_record));
            sprintf(one_record, "%s,%s,%s,%s,%s;", 
                    PQgetvalue(res, i, FIELD_INDEX_OF_ID),
                    PQgetvalue(res, i, FIELD_INDEX_OF_MONITOR_TYPE),
                    PQgetvalue(res, i, FIELD_INDEX_OF_MAC_ADDRESS),
                    PQgetvalue(res, i, FIELD_INDEX_OF_UUID),
                    PQgetvalue(res, i, FIELD_INDEX_OF_VIOLATION_TIMESTAMP));
            
            if(buf_len > strlen(buf) + strlen(one_record)){
                strcat(buf, one_record);
            
                memset(sql, 0, sizeof(sql));
                if(PQgetvalue(res, i, FIELD_INDEX_OF_ID) == NULL){
                    PQclear(res);

                    SQL_release_database_connection(
                        db_connection_list_head,
                        db_serial_id);

                    return E_API_PROTOCOL_FORMAT;
                }
                sprintf(sql, 
                        sql_update_template, 
                        atoi(PQgetvalue(res, i, FIELD_INDEX_OF_ID)));

                ret_val = SQL_execute(db_conn, sql);        
            }
        }
    }

    PQclear(res);
    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_reload_monitor_config(
    DBConnectionListHead *db_connection_list_head,
    int server_localtime_against_UTC_in_hour)
{
    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    char *table_name[] = {"geo_fence_config",
                          "location_not_stay_room_config", 
                          "location_long_stay_in_danger_config",
                          "movement_config" };

    char *sql_update_template = "UPDATE %s " \
                                "SET is_active = CASE " \
                                "WHEN " \
                                "(enable = 1 AND " \
                                "start_time < end_time AND " \
                                "CURRENT_TIME + interval '%d hours' >= " \
                                "start_time AND " \
                                "CURRENT_TIME + interval '%d hours' < " \
                                "end_time)" \
                                "OR " \
                                "(enable = 1 AND " \
                                "start_time > end_time AND " \
                                "(" \
                                "(CURRENT_TIME + interval '%d hours' >= " \
                                "start_time AND " \
                                "CURRENT_TIME + INTERVAL '%d hours' <= " \
                                "'23:59:59') " \
                                "OR " \
                                "(CURRENT_TIME + INTERVAL '%d hours' >= " \
                                "'00:00:00' AND " \
                                "CURRENT_TIME + INTERVAL '%d hours' < " \
                                "end_time)" \
                                ")" \
                                ") " \
                                "THEN 1" \
                                "ELSE 0" \
                                "END;";

    PGresult *res = NULL;
    int idx = 0;

    for(idx = 0; idx< sizeof(table_name)/sizeof(table_name[0]); idx++){
        memset(sql, 0, sizeof(sql));

        sprintf(sql, sql_update_template, 
                table_name[idx],
                server_localtime_against_UTC_in_hour,
                server_localtime_against_UTC_in_hour,
                server_localtime_against_UTC_in_hour,
                server_localtime_against_UTC_in_hour,
                server_localtime_against_UTC_in_hour,
                server_localtime_against_UTC_in_hour);

        if(WORK_SUCCESSFULLY != 
           SQL_get_database_connection(db_connection_list_head, 
                                       &db_conn, 
                                       &db_serial_id)){
            zlog_error(category_debug,
                       "cannot operate database");

            return E_SQL_OPEN_DATABASE;
        }

        ret_val = SQL_execute(db_conn, sql);

        if(WORK_SUCCESSFULLY != ret_val){
        
            zlog_error(category_debug, 
                       "SQL_execute failed [%d]: %s", 
                       ret_val, 
                       PQerrorMessage(db_conn));
    
            SQL_release_database_connection(
                db_connection_list_head,
                db_serial_id);

            return E_SQL_EXECUTE;
        }     

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);
    }

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_dump_active_geo_fence_settings(
    DBConnectionListHead *db_connection_list_head, 
    char *filename)
{
    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    
    char *sql_select_template = "SELECT " \
                                "area_id, " \
                                "id, " \
                                "name, " \
                                "perimeters, " \
                                "fences " \
                                "FROM geo_fence_config " \
                                "WHERE " \
                                "is_active = 1;";

    const int NUMBER_FIELDS_OF_SQL_SELECT_TEMPLATE = 5;
    const int FIELD_INDEX_OF_AREA_ID = 0;
    const int FIELD_INDEX_OF_ID = 1;
    const int FIELD_INDEX_OF_NAME = 2;
    const int FIELD_INDEX_OF_PERIMETRS = 3;
    const int FIELD_INDEX_OF_FENCES = 4;

    FILE *file = NULL;

    PGresult *res = NULL;
    int total_fields = 0;
    int total_rows = 0;
    int i = 0;

    file = fopen(filename, "wt");
    if(file == NULL){
        zlog_error(category_debug, "cannot open filepath %s", filename);
        return E_OPEN_FILE;
    }

    memset(sql, 0, sizeof(sql));
    sprintf(sql, sql_select_template);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
       zlog_error(category_debug,
                  "cannot open database");

       fclose(file);

       return E_SQL_OPEN_DATABASE;
    }

    res = PQexec(db_conn, sql);

    if(PQresultStatus(res) != PGRES_TUPLES_OK){

        PQclear(res);

        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   res, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head, 
            db_serial_id);

        fclose(file);

        return E_SQL_EXECUTE;
    }

    total_rows = PQntuples(res);
    total_fields = PQnfields(res);
    
    if(total_rows > 0 && 
       total_fields == NUMBER_FIELDS_OF_SQL_SELECT_TEMPLATE){
         
        for(i = 0 ; i < total_rows ; i++){
            fprintf(file, "%s;%s;%s;%s;%s;\n", 
                    PQgetvalue(res, i, FIELD_INDEX_OF_AREA_ID),
                    PQgetvalue(res, i, FIELD_INDEX_OF_ID),
                    PQgetvalue(res, i, FIELD_INDEX_OF_NAME),
                    PQgetvalue(res, i, FIELD_INDEX_OF_PERIMETRS),
                    PQgetvalue(res, i, FIELD_INDEX_OF_FENCES));
        }
    }

    PQclear(res);

    SQL_release_database_connection(
        db_connection_list_head, 
        db_serial_id);

    fclose(file);

    return WORK_SUCCESSFULLY;
}

ErrorCode SQL_dump_mac_address_under_geo_fence_monitor(
    DBConnectionListHead *db_connection_list_head, 
    char *filename){

    PGconn *db_conn = NULL;
    int db_serial_id = -1;
    ErrorCode ret_val = WORK_SUCCESSFULLY;
    char sql[SQL_TEMP_BUFFER_LENGTH];
    
    char *sql_select_template = "SELECT " \
                                "area_id, " \
                                "mac_address " \
                                "FROM object_table " \
                                "WHERE " \
                                "monitor_type & %d = %d " \
                                "ORDER BY area_id ASC;";

    const int NUMBER_FIELDS_OF_SQL_SELECT_TEMPLATE = 2;
    const int FIELD_INDEX_OF_AREA_ID = 0;
    const int FIELD_INDEX_OF_MAC_ADDRESS = 1;
    
    FILE *file = NULL;

    PGresult *res = NULL;
    int total_fields = 0;
    int total_rows = 0;
    int i = 0;

    file = fopen(filename, "wt");
    if(file == NULL){
        zlog_error(category_debug, "cannot open filepath %s", filename);
        return E_OPEN_FILE;
    }

    memset(sql, 0, sizeof(sql));
    sprintf(sql, sql_select_template, 
            MONITOR_GEO_FENCE, 
            MONITOR_GEO_FENCE);

    if(WORK_SUCCESSFULLY != 
       SQL_get_database_connection(db_connection_list_head, 
                                   &db_conn, 
                                   &db_serial_id)){
       zlog_error(category_debug,
                  "cannot open database\n");

       fclose(file);

       return E_SQL_OPEN_DATABASE;
    }

    res = PQexec(db_conn, sql);

    if(PQresultStatus(res) != PGRES_TUPLES_OK){

        PQclear(res);

        zlog_error(category_debug, "SQL_execute failed [%d]: %s", 
                   res, PQerrorMessage(db_conn));

        SQL_release_database_connection(
            db_connection_list_head,
            db_serial_id);

        fclose(file);

        return E_SQL_EXECUTE;
    }

    total_rows = PQntuples(res);
    total_fields = PQnfields(res);
    
    if(total_rows > 0 && 
       total_fields == NUMBER_FIELDS_OF_SQL_SELECT_TEMPLATE){
         
        for(i = 0 ; i < total_rows ; i++){

            fprintf(file, "%s;%s;\n", 
                    PQgetvalue(res, i, FIELD_INDEX_OF_AREA_ID),
                    PQgetvalue(res, i, FIELD_INDEX_OF_MAC_ADDRESS));
        }
    }

    PQclear(res);

    SQL_release_database_connection(
        db_connection_list_head,
        db_serial_id);

    fclose(file);

    return WORK_SUCCESSFULLY;

}