/*
  Copyright (c) 2016 Academia Sinica, Institute of Information Science

  License:

     GPL 3.0 : The content of this file is subject to the terms and cnditions
     defined in file 'COPYING.txt', which is part of this source code package.

  Project Name:

     BeDIS

  File Name:

     Geo-Fencing.c

  File Description:

     This file contains programs for detecting whether objects we tracked and
     restricted in the fence triggers Geo-Fence.

  Version:

     1.0, 20190415

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

     Gary Xiao     , garyh0205@hotmail.com

 */

#include "Geo-Fencing.h"


ErrorCode geo_fence_initial(pgeo_fence_config geo_fence_config,
                            int number_worker_threads, int recv_port,
                            int api_recv_port, int decision_threshold){

    geo_fence_config -> decision_threshold = decision_threshold;

    geo_fence_config -> is_running = true;

    geo_fence_config -> recv_port = recv_port;

    geo_fence_config -> api_recv_port = api_recv_port;

    geo_fence_config -> number_schedule_workers = number_worker_threads;

    geo_fence_config -> worker_thread = thpool_init(number_worker_threads);

    if(mp_init( &(geo_fence_config -> pkt_content_mempool),
       sizeof(spkt_content), SLOTS_IN_MEM_POOL) != MEMORY_POOL_SUCCESS)
        return E_MALLOC;

    if(mp_init( &(geo_fence_config -> tracked_mac_list_head_mempool),
       sizeof(stracked_mac_list_head), SLOTS_IN_MEM_POOL) != MEMORY_POOL_SUCCESS)
        return E_MALLOC;

    if(mp_init( &(geo_fence_config -> rssi_list_node_mempool),
       sizeof(srssi_list_node), SLOTS_IN_MEM_POOL) != MEMORY_POOL_SUCCESS)
        return E_MALLOC;

    if (udp_initial(&geo_fence_config -> udp_config, recv_port, api_recv_port)
        != WORK_SUCCESSFULLY)
        return E_WIFI_INIT_FAIL;

    if (startThread( &geo_fence_config -> process_api_recv_thread,
        (void *)process_api_recv, geo_fence_config) != WORK_SUCCESSFULLY){
        return E_START_THREAD;
    }

    return WORK_SUCCESSFULLY;

}


ErrorCode geo_fence_free(pgeo_fence_config geo_fence_config){

    geo_fence_config -> is_running = false;

    Sleep(WAITING_TIME);

    udp_release( &geo_fence_config -> udp_config);

    thpool_destroy(&geo_fence_config -> worker_thread);

    mp_destroy(&geo_fence_config -> pkt_content_mempool);
}


static void *process_geo_fence_routine(void *_pkt_content){

    ppkt_content pkt_content = (ppkt_content)_pkt_content;

    pgeo_fence_config geo_fence_config = pkt_content -> geo_fence_config;

    ptracked_mac_list_head current_mac_list_head, tmp_mac_list_head;

    prssi_list_node current_rssi_list_node, tmp_rssi_list_node;

    char uuid[UUID_LENGTH];

    char gateway_ip[NETWORK_ADDR_LENGTH];

    int object_type;

    int number_of_objects;

    char tmp[WIFI_MESSAGE_LENGTH];

    memset(tmp, 0, WIFI_MESSAGE_LENGTH);

    memset(uuid, 0, UUID_LENGTH);

    memset(gateway_ip, 0, NETWORK_ADDR_LENGTH);

    sscanf(pkt_content->content, "%s;%s;%d;%d;%s", uuid, gateway_ip,
                                 object_type, number_of_objects, tmp);

    if (is_in_geo_fence(uuid) == NULL){
        return;
    }

    do {

        for(;number_of_objects > 0;number_of_objects --){

            char tmp_data[WIFI_MESSAGE_LENGTH];

            char mac_address[LENGTH_OF_MAC_ADDRESS];

            int init_time,final_time, rssi;

            memset(tmp_data, 0, WIFI_MESSAGE_LENGTH);

            sscanf(tmp, "%s;%d;%d;%d;%s", mac_address, init_time, final_time,
                                          rssi, tmp_data);

            /* Filter Geo-Fencing */

            if (is_mac_in_geo_fence()){

            }

            if (current_mac_list_head = is_in_mac_list(geo_fence_config,
                                                       mac_address) == NULL){
                if (rssi >= geo_fence_config -> decision_threshold){

                    tmp_mac_list_head = mp_alloc(
                                 &pkt_content -> tracked_mac_list_head_mempool);

                    tmp_rssi_list_node = mp_alloc(
                                        &pkt_content -> rssi_list_node_mempool);

                    if(current_rssi_list_node = is_in_mac_list(current_mac_list_head, uuid) == NULL){




                    }
                    else{





                    }

                }

            }
            else{





            }

            memset(tmp, 0, WIFI_MESSAGE_LENGTH);

            memcpy(tmp, tmp_data, strlen(tmp_data) * sizeof(char));

        }

        if (strlen(tmp) > 0){
            sscanf(tmp, "%d;%d;%s", object_type, number_of_objects, tmp_data);

            memset(tmp, 0, WIFI_MESSAGE_LENGTH);
            memcpy(tmp, tmp_data, strlen(tmp_data) * sizeof(char));
        }

    } while(strlen(tmp) > 0);
}


static void *process_api_recv(void *_geo_fence_config){

    int return_value;

    sPkt temppkt;

    char *tmp_addr;

    ppkt_content pkt_content;

    pgeo_fence_config geo_fence_config = (pgeo_fence_config)_geo_fence_config;

    while(geo_fence_config -> is_running == true){

        temppkt = udp_getrecv( &geo_fence_config -> udp_config);

        if(temppkt.type == UDP){

            tmp_addr = udp_hex_to_address(temppkt.address);

            pkt_content = mp_alloc(&(geo_fence_config -> pkt_content_mempool));

            memset(pkt_content, 0, strlen(pkt_content) * sizeof(char));

            memcpy(pkt_content -> ip_address, tmp_addr, strlen(tmp_addr) *
                   sizeof(char));

            memcpy(pkt_content -> content, temppkt.content,
                   temppkt.content_size);

            pkt_content -> content_size = temppkt.content_size;

            pkt_content -> geo_fence_config = geo_fence_config;

            while(geo_fence_config -> worker_thread -> num_threads_working ==
                  geo_fence_config -> worker_thread -> num_threads_alive){
                Sleep(WAITING_TIME);
            }

            return_value = thpool_add_work(geo_fence_config -> worker_thread,
                                           process_geo_fence_routine,
                                           pkt_content,
                                           0);

            free(tmp_addr);
        }
    }
}


static ErrorCode init_tracked_mac_list_head(
                                  ptracked_mac_list_head tracked_mac_list_head){

    init_entry(&tracked_mac_list_head -> mac_list_entry);

    init_entry(tracked_mac_list_head -> rssi_list_head);

}


static ErrorCode init_rssi_list_node(prssi_list_node rssi_list_node){

    init_entry(&rssi_list_node -> rssi_list_node);

}


static ptracked_mac_list_head is_in_mac_list(pgeo_fence_config geo_fence_config,
                                             char *mac_address){

    int return_value;

    List_Entry *mac_entry_pointer,  *next_mac_entry_pointer;

    ptracked_mac_list_head current_mac_list_head;

    list_for_each_safe(mac_entry_pointer, next_mac_entry_pointer,
                       &geo_fence_config -> ptracked_mac_list_head){

        current_mac_list_head = ListEntry(mac_entry_pointer,
                                          stracked_mac_list_head,
                                          mac_list_entry);

        return_value = strncmp(mac_address,
                               current_mac_list_head -> mac_address,
                               LENGTH_OF_MAC_ADDRESS);

        if (return_value == 0)
            return current_mac_list_head;

    }

    return NULL;

}


static prssi_list_node is_in_rssi_list(
                                   ptracked_mac_list_head tracked_mac_list_head,
                                   char *uuid){

    int return_value;

    List_Entry *rssi_entry_pointer, *next_rssi_entry_pointer;

    prssi_list_node current_rssi_list_node;

    list_for_each_safe(rssi_entry_pointer, next_rssi_entry_pointer,
                       &tracked_mac_list_head -> rssi_list_head){

        current_rssi_list_node = ListEntry(rssi_entry_pointer,
                                           srssi_list_node,
                                           rssi_list_node);

        return_value = strncmp(uuid,
                               current_rssi_list_node -> uuid,
                               UUID_LENGTH);

        if (return_value == 0)
            return current_rssi_list_node;

    }

    return NULL;

}
