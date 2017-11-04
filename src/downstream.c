/**
 * Copyright 2015 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
/**
 * @file downstream.c
 *
 * @description This describes functions required to manage downstream messages.
 *
 */

#include "downstream.h"
#include "upstream.h" 
#include "connection.h"
#include "partners_check.h"
#include "ParodusInternal.h"

/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/

/**
 * @brief listenerOnMessage function to create WebSocket listener to receive connections
 *
 * @param[in] msg The message received from server for various process requests
 * @param[in] msgSize message size
 */
void listenerOnMessage(void * msg, size_t msgSize)
{
    int rv =0;
    wrp_msg_t *message;
    char* destVal = NULL;
    char dest[32] = {'\0'};
    int msgType;
    int bytes =0;
    int destFlag =0;
    size_t size = 0;
    int resp_size = -1 ;
    const char *recivedMsg = NULL;
    char *str= NULL;
    wrp_msg_t *resp_msg = NULL;
    void *resp_bytes;
    cJSON *response = NULL;
    reg_list_item_t *temp = NULL;

    recivedMsg =  (const char *) msg;

    ParodusInfo("Received msg from server\n");
    if(recivedMsg!=NULL) 
    {
        /*** Decoding downstream recivedMsg to check destination ***/
        rv = wrp_to_struct(recivedMsg, msgSize, WRP_BYTES, &message);

        if(rv > 0)
        {
            ParodusPrint("\nDecoded recivedMsg of size:%d\n", rv);
            msgType = message->msg_type;
            ParodusInfo("msgType received:%d\n", msgType);
            
            if(message->msg_type == WRP_MSG_TYPE__AUTH)
            {
            	ParodusInfo("Authorization Status received with Status code :%d\n", message->u.auth.status);
            }

            if(message->msg_type == WRP_MSG_TYPE__REQ)
            {
                ParodusPrint("numOfClients registered is %d\n", get_numOfClients());
                int ret = validate_partner_id(message, NULL);
                if(ret < 0)
                {
                    response = cJSON_CreateObject();
                    cJSON_AddNumberToObject(response, "statusCode", 430);
                    cJSON_AddStringToObject(response, "message", "Invalid partner_id");
                }

                if((message->u.req.dest !=NULL) && (ret >= 0))
                {
                    destVal = message->u.req.dest;
                    strtok(destVal , "/");
                    parStrncpy(dest,strtok(NULL , "/"), sizeof(dest));
                    ParodusInfo("Received downstream dest as :%s and transaction_uuid :%s\n", dest, message->u.req.transaction_uuid);
                    temp = get_global_node();
                    //Checking for individual clients & Sending to each client

                    while (NULL != temp)
                    {
                        ParodusPrint("node is pointing to temp->service_name %s \n",temp->service_name);
                        // Sending message to registered clients
                        if( strcmp(dest, temp->service_name) == 0)
                        {
                            ParodusPrint("sending to nanomsg client %s\n", dest);
                            bytes = nn_send(temp->sock, recivedMsg, msgSize, 0);
                            ParodusInfo("sent downstream message to reg_client '%s'\n",temp->url);
                            ParodusPrint("downstream bytes sent:%d\n", bytes);
                            destFlag =1;
                            break;
                        }
                        ParodusPrint("checking the next item in the list\n");
                        temp= temp->next;
                    }

                    //if any unknown dest received sending error response to server
                    if(destFlag ==0)
                    {
                        ParodusError("Unknown dest:%s\n", dest);
                        response = cJSON_CreateObject();
                        cJSON_AddNumberToObject(response, "statusCode", 531);
                        cJSON_AddStringToObject(response, "message", "Service Unavailable");
                    }
                }

                if(destFlag == 0 || ret < 0)
                {
                    resp_msg = (wrp_msg_t *)malloc(sizeof(wrp_msg_t));
                    memset(resp_msg, 0, sizeof(wrp_msg_t));

                    resp_msg ->msg_type = msgType;
                    resp_msg ->u.req.source = message->u.req.dest;
                    resp_msg ->u.req.dest = message->u.req.source;
                    resp_msg ->u.req.transaction_uuid=message->u.req.transaction_uuid;

                    if(response != NULL)
                    {
                        str = cJSON_PrintUnformatted(response);
                        ParodusInfo("Payload Response: %s\n", str);

                        resp_msg ->u.req.payload = (void *)str;
                        resp_msg ->u.req.payload_size = strlen(str);

                        ParodusPrint("msgpack encode\n");
                        resp_size = wrp_struct_to( resp_msg, WRP_BYTES, &resp_bytes );
                        if(resp_size > 0)
                        {
                            size = (size_t) resp_size;
                            sendUpstreamMsgToServer(&resp_bytes, size);
                        }
                        free(str);
                        cJSON_Delete(response);
                        free(resp_bytes);
                        resp_bytes = NULL;
                    }
                    free(resp_msg);
                }
            }
        }
        else
        {
            ParodusError( "Failure in msgpack decoding for receivdMsg: rv is %d\n", rv );
        }
        ParodusPrint("free for downstream decoded msg\n");
        wrp_free_struct(message);
    }
}