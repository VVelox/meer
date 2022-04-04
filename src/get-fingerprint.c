/*
** Copyright (C) 2018-2022 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2018-2022 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#ifdef HAVE_LIBHIREDIS

#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <hiredis/hiredis.h>

#include "meer-def.h"
#include "meer.h"
#include "oui.h"
#include "util.h"

#include "output-plugins/redis.h"
#include "get-fingerprint.h"

extern struct _MeerConfig *MeerConfig;
extern struct _MeerOutput *MeerOutput;
extern struct _MeerCounters *MeerCounters;
extern struct _Fingerprint_Networks *Fingerprint_Networks;

void Fingerprint_DHCP ( struct json_object *json_obj, const char *json_string )
{

    struct json_object *tmp = NULL;

    struct json_object *json_obj_dhcp = NULL;
    struct json_object *tmp_dhcp = NULL;

    char key[512] = { 0 };

    char *assigned_ip = NULL;
    char *dest_ip = NULL;

    char *dhcp = NULL;

    if (json_object_object_get_ex(json_obj, "dest_ip", &tmp))
        {
            dest_ip = (char *)json_object_get_string(tmp);
        }


    if (json_object_object_get_ex(json_obj, "dhcp", &tmp))
        {

            dhcp = (char *)json_object_get_string(tmp);

            if ( Validate_JSON_String( dhcp ) == 0 )
                {

                    json_obj_dhcp = json_tokener_parse(dhcp);

                    if (json_object_object_get_ex(json_obj_dhcp, "assigned_ip", &tmp_dhcp))
                        {
                            assigned_ip = (char *)json_object_get_string(tmp_dhcp);
                        }
                }
        }

    if ( !strcmp(assigned_ip, "0.0.0.0" ) && strcmp(dest_ip, "255.255.255.255") )
        {
            assigned_ip = dest_ip;
        }

    snprintf(key, sizeof(key), "%s|dhcp|%s", FINGERPRINT_REDIS_KEY, assigned_ip);
    key[ sizeof( key ) - 1 ] = '\0';

    Redis_Writer( "SET", key, json_string, FINGERPRINT_DHCP_REDIS_EXPIRE );


    free(json_obj_dhcp );

}


void Fingerprint_JSON_Redis( struct json_object *json_obj, struct _FingerprintData *FingerprintData, char *str)
{

    struct json_object *tmp = NULL;
    struct json_object *json_obj_alert = NULL;
    struct json_object *json_obj_http = NULL;

    struct json_object *encode_json = NULL;
    encode_json = json_object_new_object();

    struct json_object *encode_json_fingerprint = NULL;
    encode_json_fingerprint = json_object_new_object();

    struct json_object *encode_json_http = NULL;
    encode_json_http = json_object_new_object();

    char src_ip[64] = { 0 };
    char timestamp[32] = { 0 };
    char app_proto[32] = { 0 };

    uint64_t flow_id = 0;
    uint64_t signature_id = 0;

    char key[128] = { 0 };

    bool flag = false;

    char *string_f = malloc(MeerConfig->payload_buffer_size);

    if ( string_f == NULL )
        {
            fprintf(stderr, "[%s, line %d] Fatal Error:  Can't allocate memory! Abort!\n", __FILE__, __LINE__);
            exit(-1);
        }

    memset(string_f, 0, MeerConfig->payload_buffer_size);

    char *new_string = malloc(MeerConfig->payload_buffer_size);

    if ( new_string == NULL )
        {
            fprintf(stderr, "[%s, line %d] Fatal Error:  Can't allocate memory! Abort!\n", __FILE__, __LINE__);
            exit(-1);
        }

    char *http = malloc(MeerConfig->payload_buffer_size);

    if ( http == NULL )
        {
            fprintf(stderr, "[%s, line %d] Fatal Error:  Can't allocate memory! Abort!\n", __FILE__, __LINE__);
            exit(-1);
        }

    memset(http, 0, MeerConfig->payload_buffer_size);

    if (json_object_object_get_ex(json_obj, "src_ip", &tmp))
        {
            strlcpy( src_ip, json_object_get_string(tmp), sizeof(src_ip) );
        }

    if ( src_ip[0] == '\0' )
        {
            Meer_Log(WARN, "[%s, line %d] Got a NULL src_ip address!", __FILE__, __LINE__);
        }

    if (json_object_object_get_ex(json_obj, "timestamp", &tmp))
        {
            strlcpy( timestamp, json_object_get_string(tmp), sizeof(timestamp) );
        }

    if ( timestamp[0] == '\0' )
        {
            Meer_Log(WARN, "[%s, line %d] Got a NULL timestamp!", __FILE__, __LINE__);
        }

    if (json_object_object_get_ex(json_obj, "app_proto", &tmp))
        {
            strlcpy( app_proto, json_object_get_string(tmp), sizeof(app_proto) );
        }

    if (json_object_object_get_ex(json_obj, "flow_id", &tmp))
        {
            flow_id = json_object_get_int64(tmp);
        }

    if ( flow_id == 0 )
        {
            Meer_Log(WARN, "[%s, line %d] No flow ID found!", __FILE__, __LINE__);
        }

    /* Write out fingerprint|ip|{IP} key */

    json_object *jtimestamp = json_object_new_string( timestamp );
    json_object_object_add(encode_json,"timestamp", jtimestamp);

    json_object *jip = json_object_new_string( src_ip );
    json_object_object_add(encode_json,"ip", jip);

    snprintf(string_f, MeerConfig->payload_buffer_size, "%s", json_object_to_json_string(encode_json));
    string_f[ MeerConfig->payload_buffer_size - 1] = '\0';

    snprintf(key, sizeof(key), "%s|ip|%s", FINGERPRINT_REDIS_KEY, src_ip);
    key[ sizeof(key) - 1] = '\0';

    Redis_Writer( "SET", key, string_f, FINGERPRINT_IP_REDIS_EXPIRE);

    /* Write out fingerprint|event|{IP} key */

    json_object_object_add(encode_json, "event_type", json_object_new_string("fingerprint"));
    json_object_object_add(encode_json, "timestamp", json_object_new_string( timestamp ));
    json_object_object_add(encode_json, "flow_id", json_object_new_int64( flow_id ));
    json_object_object_add(encode_json, "src_ip", json_object_new_string( src_ip ));

    /* Sagan doesn't have an "app_proto" */

    if ( app_proto[0] != '\0' )
        {
            json_object_object_add(encode_json, "app_proto", json_object_new_string( app_proto ));
        }


    if (json_object_object_get_ex(json_obj, "src_dns", &tmp))
        {
            json_object_object_add(encode_json, "src_host", json_object_new_string( json_object_get_string(tmp) ));
        }

    if (json_object_object_get_ex(json_obj, "dest_dns", &tmp))
        {
            json_object_object_add(encode_json, "dest_host", json_object_new_string( json_object_get_string(tmp) ));
        }

    /* host */

    if (json_object_object_get_ex(json_obj, "host", &tmp))
        {
            json_object_object_add(encode_json, "host", json_object_new_string( json_object_get_string(tmp) ));
        }
    else
        {
            Meer_Log(WARN, "[%s, line %d] Got a NULL host!", __FILE__, __LINE__);
        }

    /* in_iface */

    if (json_object_object_get_ex(json_obj, "in_iface", &tmp))
        {
            json_object_object_add(encode_json, "in_iface", json_object_new_string( json_object_get_string(tmp) ));
        }
    else
        {
            Meer_Log(WARN, "[%s, line %d] Got a NULL in_iface!", __FILE__, __LINE__);
        }

    /* src_port */

    if (json_object_object_get_ex(json_obj, "src_port", &tmp))
        {
            json_object_object_add(encode_json, "src_port", json_object_new_int( json_object_get_int(tmp) ));
        }
    else
        {
            Meer_Log(WARN, "[%s, line %d] Got a NULL src_port!", __FILE__, __LINE__);
        }

    /* dest_port */

    if (json_object_object_get_ex(json_obj, "dest_ip", &tmp))
        {
            json_object_object_add(encode_json, "dest_ip", json_object_new_string( json_object_get_string(tmp) ));
        }
    else
        {
            Meer_Log(WARN, "[%s, line %d] Got a NULL dest_ip!", __FILE__, __LINE__);
        }

    /* dest_port */

    if (json_object_object_get_ex(json_obj, "dest_port", &tmp))
        {
            json_object_object_add(encode_json, "dest_port", json_object_new_int( json_object_get_int(tmp) ));
        }
    else
        {
            Meer_Log(WARN, "[%s, line %d] Got a NULL dest_port!", __FILE__, __LINE__);
        }

    /* proto */

    if (json_object_object_get_ex(json_obj, "proto", &tmp))
        {
            json_object_object_add(encode_json, "proto", json_object_new_string( json_object_get_string(tmp) ));
        }
    else
        {
            Meer_Log(WARN, "[%s, line %d] Got a NULL proto!", __FILE__, __LINE__);
        }

    /* program (Sagan specific data) */

    if (json_object_object_get_ex(json_obj, "program", &tmp))
        {
            json_object_object_add(encode_json, "program", json_object_new_string( json_object_get_string(tmp) ));
        }


    /* Specific "fingerprints" */

    if (json_object_object_get_ex(json_obj, "payload", &tmp))
        {
            json_object_object_add(encode_json_fingerprint, "payload", json_object_new_string( json_object_get_string(tmp) ));
        }

    if ( FingerprintData->os[0] != '\0' )
        {
            json_object_object_add(encode_json_fingerprint, "os", json_object_new_string( FingerprintData->os ));
        }

    if ( FingerprintData->source[0] != '\0' )
        {
            json_object_object_add(encode_json_fingerprint, "source", json_object_new_string( FingerprintData->source ));
        }

    if ( FingerprintData->type[0] != '\0' )
        {
            json_object_object_add(encode_json_fingerprint, "client_server", json_object_new_string( FingerprintData->type ));
        }

    if ( FingerprintData->expire != 0 )
        {
            json_object_object_add(encode_json_fingerprint, "expire", json_object_new_int( FingerprintData->expire ));
        }

    /***********************************/
    /* Add "alert" data to fingerprint */
    /***********************************/

    if ( json_object_object_get_ex(json_obj, "alert", &tmp))
        {

            const char *alert_data = json_object_get_string(tmp);

            if ( alert_data == NULL )
                {
                    Meer_Log(WARN, "[%s, line %d] Unabled to get alert data!", __FILE__, __LINE__);
                }

            json_obj_alert = json_tokener_parse( alert_data );

            if ( json_obj_alert == NULL )
                {
                    Meer_Log(WARN, "Unable to json_tokener_parse: %s", json_object_get_string(tmp) );
                }

            /* signature_id */

            if (json_object_object_get_ex(json_obj_alert, "signature_id", &tmp))
                {
                    signature_id = json_object_get_int64(tmp);
                    json_object_object_add(encode_json_fingerprint, "signature_id", json_object_new_int64(signature_id));
                }

            /* signature */

            if (json_object_object_get_ex(json_obj_alert, "signature", &tmp))
                {
                    json_object_object_add(encode_json_fingerprint, "signature", json_object_new_string(json_object_get_string(tmp) ));
                }

            /* rev */

            if (json_object_object_get_ex(json_obj_alert, "rev", &tmp))
                {
                    json_object_object_add(encode_json_fingerprint, "rev", json_object_new_int64(json_object_get_int64(tmp) ));
                }

        }
    else
        {
            Meer_Log(WARN, "[%s, line %d] Alert data is NULL?!?!!", __FILE__, __LINE__);
        }

    /* Add "fingerprint" nest */

    snprintf(string_f, MeerConfig->payload_buffer_size, "%s", json_object_to_json_string_ext(encode_json, JSON_C_TO_STRING_PLAIN) );
    string_f[ strlen(string_f) - 1] = '\0';

    snprintf(new_string, MeerConfig->payload_buffer_size, "%s, \"fingerprint\": %s}", string_f, json_object_to_json_string_ext(encode_json_fingerprint, JSON_C_TO_STRING_PLAIN) );

    strlcpy(string_f, new_string, MeerConfig->payload_buffer_size);

    /***********************************/
    /* Add "http" data to fingerprint */
    /***********************************/

    flag = false;

    if ( !strcmp(app_proto, "http" ) )
        {

            if ( json_object_object_get_ex(json_obj, "http", &tmp))
                {

                    const char *http_data = json_object_get_string(tmp);

                    if ( http_data == NULL )
                        {
                            Meer_Log(WARN, "[%s, line %d] Unabled to get http data!", __FILE__, __LINE__);
                            return;
                        }

                    json_obj_http = json_tokener_parse( http_data );

                    if ( json_obj_http == NULL )
                        {
                            Meer_Log(WARN, "Unable to json_tokener_parse: %s", json_object_get_string(tmp) );
                            return;
                        }

                    if (json_object_object_get_ex(json_obj_http, "http_user_agent", &tmp))
                        {
                            json_object_object_add(encode_json_http, "http_user_agent", json_object_new_string(json_object_get_string(tmp) ));
                            flag = true;
                        }

                    if (json_object_object_get_ex(json_obj_http, "xff", &tmp))
                        {
                            json_object_object_add(encode_json_http, "xff", json_object_new_string(json_object_get_string(tmp) ));
                            flag = true;
                        }
                }

            snprintf(http, MeerConfig->payload_buffer_size, "%s", json_object_to_json_string_ext(encode_json_http, JSON_C_TO_STRING_PLAIN));
            http[  MeerConfig->payload_buffer_size - 1] = '\0';

        }

    /* Verify we have http data, so we don't have a empty {} nest */

    if ( flag == true )
        {

            string_f[ strlen(string_f) - 1] = '\0';
            snprintf(new_string, MeerConfig->payload_buffer_size, "%s, \"http\": %s}", string_f, http);
            strlcpy(string_f, new_string, MeerConfig->payload_buffer_size);
        }

    snprintf(key, sizeof(key), "%s|event|%s|%" PRIu64 "", FINGERPRINT_REDIS_KEY, src_ip, signature_id);
    key[ sizeof(key) -1 ] = '\0';

    Redis_Writer( "SET", key, string_f, FingerprintData->expire );

    json_object_put(encode_json);
    json_object_put(encode_json_fingerprint);
    json_object_put(encode_json_http);
    json_object_put(json_obj_alert);

    snprintf(str, MeerConfig->payload_buffer_size, "%s", string_f);
    str[ MeerConfig->payload_buffer_size - 1 ] = '\0';

    free( new_string );
    free( string_f );
    free( http );

}

bool Is_Fingerprint( struct json_object *json_obj, struct _FingerprintData *FingerprintData )
{

    struct json_object *tmp = NULL;
    struct json_object *json_obj_alert= NULL;
    struct json_object *json_obj_metadata= NULL;

    bool ret = false;

    const char *alert_data = NULL;
    const char *metadata = NULL;

    char *fingerprint_d_os = NULL;
    char *fingerprint_d_type = NULL;
    char *fingerprint_d_expire = NULL;
    char *fingerprint_d_source = NULL;

    char *fingerprint_os = "unknown";
    char *fingerprint_source = "unknown";
    char *fingerprint_expire = NULL;

    char *ptr1 = NULL;

    if ( json_object_object_get_ex(json_obj, "alert", &tmp) )
        {

            alert_data = json_object_get_string(tmp);

            if ( alert_data == NULL )
                {
                    Meer_Log(WARN, "[%s, line %d] Unable to get alert data!", __FILE__, __LINE__);
                    return(false);
                }

            json_obj_alert = json_tokener_parse(alert_data);

            if ( json_obj_alert == NULL )
                {
                    Meer_Log(WARN, "Unable to json_tokener_parse: %s", alert_data);
                    return(false);
                }

            if ( json_object_object_get_ex(json_obj_alert, "metadata", &tmp) )
                {

                    metadata = json_object_get_string(tmp);
                    json_obj_metadata = json_tokener_parse(metadata);

                    /* Get OS type */

                    if ( json_object_object_get_ex(json_obj_metadata, "fingerprint_os", &tmp))
                        {

                            ret = true;
                            fingerprint_d_os =  (char *)json_object_get_string(tmp);

                            strtok_r(fingerprint_d_os, "\"", &ptr1);

                            if ( ptr1 == NULL )
                                {
                                    Meer_Log(WARN, "[%s, line %d] Failure to decode fingerprint_os from %s", __FILE__, __LINE__, fingerprint_d_os);
                                }

                            fingerprint_os = strtok_r(NULL, "\"", &ptr1);

                            if ( fingerprint_os == NULL )
                                {
                                    Meer_Log(WARN, "[%s, line %d] Failure to decode fingerprint_os from %s", __FILE__, __LINE__, fingerprint_d_os);
                                }

                            strlcpy(FingerprintData->os, fingerprint_os, sizeof(FingerprintData->os));

                        }

                    /* Fingerprint source (packet/log) */

                    if ( json_object_object_get_ex(json_obj_metadata, "fingerprint_source", &tmp))
                        {

                            ret = true;

                            fingerprint_d_source =  (char *)json_object_get_string(tmp);

                            strtok_r(fingerprint_d_source, "\"", &ptr1);

                            if ( ptr1 == NULL )
                                {
                                    Meer_Log(WARN, "[%s, line %d] Failure to decode fingerprint_source from %s", __FILE__, __LINE__, fingerprint_d_source);
                                }

                            fingerprint_source = strtok_r(NULL, "\"", &ptr1);

                            if ( fingerprint_source == NULL )
                                {
                                    Meer_Log(WARN, "[%s, line %d] Failure to decode fingerprint_os from %s", __FILE__, __LINE__, fingerprint_d_source);
                                }

                            strlcpy(FingerprintData->source, fingerprint_source, sizeof(FingerprintData->source));
                        }


                    /* Fingerprint expire time - in seconds */

                    if ( json_object_object_get_ex(json_obj_metadata, "fingerprint_expire", &tmp))
                        {

                            ret = true;

                            fingerprint_d_expire =  (char *)json_object_get_string(tmp);

                            strtok_r(fingerprint_d_expire, "\"", &ptr1);

                            if ( ptr1 == NULL )
                                {
                                    Meer_Log(WARN, "[%s, line %d] Failure to decode fingerprint_expire from %s", __FILE__, __LINE__, fingerprint_d_expire);
                                }

                            fingerprint_expire = strtok_r(NULL, "\"", &ptr1);

                            if ( fingerprint_expire == NULL )
                                {
                                    Meer_Log(WARN, "[%s, line %d] Failure to decode fingerprint_expire from %s", __FILE__, __LINE__, fingerprint_d_expire);
                                }

                            FingerprintData->expire = atoi( fingerprint_expire );
                        }

                    /* Fingerprint type (client/server) */

                    if ( json_object_object_get_ex(json_obj_metadata, "fingerprint_type", &tmp))
                        {

                            ret = true;

                            fingerprint_d_type =  (char *)json_object_get_string(tmp);

                            if ( strcasestr( fingerprint_d_type, "client") )
                                {
                                    strlcpy(FingerprintData->type, "client", sizeof(FingerprintData->type));
                                }

                            else if ( strcasestr( fingerprint_d_type, "server") )
                                {
                                    strlcpy(FingerprintData->type, "server", sizeof(FingerprintData->type));
                                }
                        }

//                    json_object_put(tmp);
                    json_object_put(json_obj_alert);
                    json_object_put(json_obj_metadata);

                    return(ret);

                }
            else
                {

                    /* No metadata a found */

//                    json_object_put(tmp);
                    json_object_put(json_obj_alert);

                    return(false);

                }

        }

    json_object_put(json_obj_alert);
    json_object_put(json_obj_metadata);

    return(false);
}

/******************************************************************************/
/* Get_Fingerprint - This looks up "fingerprint" data, if it is within scope. */
/* Scope is determined by metadata type,  source, destination, etc            */
/******************************************************************************/

void Get_Fingerprint( struct json_object *json_obj, const char *json_string, char *str )
{

    bool valid_fingerprint_net = false;
    unsigned char ip[MAXIPBIT] = { 0 };

    uint8_t a = 0;
    uint16_t z = 0;
    uint16_t i = 0;

    uint16_t key_count = 0;

    char *tmp_ip = NULL;
    char *tmp_type = NULL;

    char src_ip[64] = { 0 };
    char dest_ip[64] = { 0 };

    char tmp_command[256] = { 0 };

    char *tmp_redis = malloc(MeerConfig->payload_buffer_size);

    if ( tmp_redis == NULL )
        {
            fprintf(stderr, "[%s, line %d] Fatal Error: Can't allocate memory! Abort!\n", __FILE__, __LINE__);
            exit(-1);
        }

    memset(tmp_redis, 0, MeerConfig->payload_buffer_size );

    char *new_json_string = malloc( MeerConfig->payload_buffer_size);

    if ( new_json_string == NULL )
        {
            fprintf(stderr, "[%s, line %d] Fatal Error: Can't allocate memory! Abort!\n", __FILE__, __LINE__);
            exit(-1);
        }

    char *final_json_string = malloc( MeerConfig->payload_buffer_size );

    if ( final_json_string == NULL )
        {
            fprintf(stderr, "[%s, line %d] Fatal Error: Can't allocate memory! Abort!\n", __FILE__, __LINE__);
            exit(-1);
        }

    memset(final_json_string, 0, MeerConfig->payload_buffer_size);

    redisReply *reply;

    struct json_object *tmp = NULL;
    struct json_object *json_obj_fingerprint = NULL;

    strlcpy( new_json_string, json_string, MeerConfig->payload_buffer_size);

    if (json_object_object_get_ex(json_obj, "src_ip", &tmp))
        {
            strlcpy(src_ip, json_object_get_string(tmp), sizeof( src_ip ));
        }

    if (json_object_object_get_ex(json_obj, "dest_ip", &tmp))
        {
            strlcpy(dest_ip, json_object_get_string(tmp), sizeof( dest_ip ));
        }


    /* a = 0 = src,  a = 1 = dest */

    for (a = 0; a < 2; a++ )
        {

            valid_fingerprint_net = false;

            if ( a == 0 )
                {

                    tmp_ip = src_ip;;
                    tmp_type = "src";

                    IP2Bit(src_ip, ip);

                    for ( z = 0; z < MeerCounters->fingerprint_network_count; z++ )
                        {
                            if ( Is_Inrange( ip, (unsigned char *)&Fingerprint_Networks[z].range, 1) )
                                {
                                    valid_fingerprint_net = true;
                                }
                        }

                }
            else
                {

                    tmp_ip = dest_ip;
                    tmp_type = "dest";

                    IP2Bit(dest_ip, ip);

                    for ( z = 0; z < MeerCounters->fingerprint_network_count; z++ )
                        {
                            if ( Is_Inrange( ip, (unsigned char *)&Fingerprint_Networks[z].range, 1) )
                                {
                                    valid_fingerprint_net = true;
                                }
                        }
                }

            /* It's a good subnet to look for fingerprints,  lets start */

            if ( valid_fingerprint_net == true )
                {

                    snprintf(tmp_command, sizeof(tmp_command), "GET %s|dhcp|%s", FINGERPRINT_REDIS_KEY, tmp_ip);
                    tmp_command[ sizeof(tmp_command) - 1 ] = '\0';

                    Redis_Reader(tmp_command, tmp_redis, MeerConfig->payload_buffer_size);

                    if ( tmp_redis[0] != '\0' )
                        {

                            new_json_string[ strlen(new_json_string) - 2 ] = '\0';	/* Snip */

                            /* Append DHCP JSON */

                            snprintf(final_json_string, MeerConfig->payload_buffer_size, "%s, \"fingerprint_dhcp_%s\": %s }", new_json_string, tmp_type, tmp_redis);

                            final_json_string[ MeerConfig->payload_buffer_size - 1 ] = '\0';

                            /* Copy final_json_string to new_json_string in case we have more modifications
                               to make */

                            strlcpy(new_json_string, final_json_string, MeerConfig->payload_buffer_size);

                            reply = redisCommand(MeerOutput->c_redis, "SCAN 0 MATCH %s|event|%s|* count 1000000", FINGERPRINT_REDIS_KEY, tmp_ip);
                            key_count = reply->element[1]->elements;

                            if ( key_count > 0 )
                                {

                                    /* Start getting individual fingerprint data */

                                    for ( i = 0; i < key_count; i++ )
                                        {

                                            redisReply *kr = reply->element[1]->element[i];
                                            snprintf(tmp_command, sizeof(tmp_command), "GET %s", kr->str);
                                            tmp_command[ sizeof(tmp_command) - 1 ] = '\0';

                                            Redis_Reader(tmp_command, tmp_redis, MeerConfig->payload_buffer_size);

                                            /* Validate our JSON ! */

                                            if ( Validate_JSON_String( tmp_redis ) == 0 )
                                                {
                                                    json_obj_fingerprint = json_tokener_parse(tmp_redis);
                                                }
                                            else
                                                {
                                                    Meer_Log(WARN, "Incomplete or invalid fingerprint JSON.");
                                                    continue;
                                                }

                                            if ( json_object_object_get_ex(json_obj_fingerprint, "fingerprint", &tmp))
                                                {

                                                    new_json_string[ strlen(new_json_string) - 2 ] = '\0'; /* Snip */

                                                    snprintf(final_json_string, MeerConfig->payload_buffer_size, "%s, \"fingerprint_%s_%d\": %s }", new_json_string, tmp_type, i, json_object_get_string(tmp) );
                                                    final_json_string[ MeerConfig->payload_buffer_size - 1 ] = '\0';


                                                    /* Copy final_json_string to new_json_string in case we have more modifications
                                                       to make */

                                                    strlcpy(new_json_string, final_json_string, MeerConfig->payload_buffer_size);

                                                }
                                        }
                                }
                        }
                }

        }  /* for (a = 0; a < 2; a++ ) */


    snprintf(str, MeerConfig->payload_buffer_size, "%s", new_json_string);
    str[ MeerConfig->payload_buffer_size - 1 ] = '\0';

    json_object_put(json_obj_fingerprint);

    free(tmp_redis);
    free(final_json_string);
    free(new_json_string);

}

#endif
