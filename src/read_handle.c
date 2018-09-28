/*
Copyright (c) 2009-2014 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.
 
The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.
 
Contributors:
   Roger Light - initial implementation and documentation.
*/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <config.h>

#include <mosquitto_broker.h>
#include <mqtt3_protocol.h>
#include <memory_mosq.h>
#include <read_handle.h>
#include <send_mosq.h>
#include <util_mosq.h>

#ifdef WITH_SYS_TREE
extern uint64_t g_pub_bytes_received;
#endif

typedef struct init_context INITCONTEXT;
struct init_context
{
	struct mosquitto *context;
	struct init_context *next;
};

int mqtt3_packet_handle(struct mosquitto_db *db, struct mosquitto *context)
{
	if(!context) return MOSQ_ERR_INVAL;
	//printf("payload --> %s \n", (context->in_packet.payload)& 0xF0);
	//printf("remaining_mult --> %ld \n", context->in_packet.remaining_mult);
	printf("remaining_length --> %ld \n", context->in_packet.remaining_length);
	//printf("packet_length --> %ld \n", context->in_packet.packet_length);
	//printf("to_process --> %ld \n", context->in_packet.to_process);
	printf("pos --> %ld \n", context->in_packet.pos);
	//printf("mid --> %hd \n", context->in_packet.mid);
	//printf("command --> %d \n", (context->in_packet.command) & 0xF0);
	//printf("remaining_count --> %s \n", context->in_packet.remaining_count);

	switch((context->in_packet.command)&0xF0){
		case PINGREQ:
			return _mosquitto_handle_pingreq(context);
		case PINGRESP:
			return _mosquitto_handle_pingresp(context);
		case PUBACK:
			printf("mqtt3_packet_handle PUBACK\n");
			return _mosquitto_handle_pubackcomp(db, context, "PUBACK");
		case PUBCOMP:
			return _mosquitto_handle_pubackcomp(db, context, "PUBCOMP");
		case PUBLISH:
			printf("~~~~~~~~~~~~~~~~~~~~~~~~\n");
			printf("mqtt3_packet_handle PUBLISH\n");
			return mqtt3_handle_publish(db, context);		// publish	-> 내부적으로 subscribe 를 찾아서 메세지를 달아줌 ★★★★★★★★★★★★★★★★★★★★★★★
		/*case INFOLBS:
			printf("INFOLBS!!!!!!!!!!!!!!!!!!!\n");
			return mqtt3_handle_publish(db, context);*/
		case PUBREC:
			return _mosquitto_handle_pubrec(context);
		case PUBREL:
			return _mosquitto_handle_pubrel(db, context);
		case CONNECT:
			printf("connect !!!\n");
			return mqtt3_handle_connect(db, context);
		case DISCONNECT:
			return mqtt3_handle_disconnect(db, context);
		case SUBSCRIBE:
			return mqtt3_handle_subscribe(db, context);		// subscribe	★★★★★★★★★★★★★★★★★★★★★★★
		case UNSUBSCRIBE:
			return mqtt3_handle_unsubscribe(db, context);
#ifdef WITH_BRIDGE
		case CONNACK:
			return mqtt3_handle_connack(db, context);
		case SUBACK:
			return _mosquitto_handle_suback(context);
		case UNSUBACK:
			return _mosquitto_handle_unsuback(context);
#endif
		default:
			/* If we don't recognise the command, return an error straight away. */
			return MOSQ_ERR_PROTOCOL;
	}
}



int mqtt3_handle_publish(struct mosquitto_db *db, struct mosquitto *context)
{
	
	struct mosquitto *temp_context = NULL;
	static int qoqo = 0;
	char *catch_message = NULL;
	char *catch_message_b = NULL;
	char *catch_message_a = NULL;
	char *catch_id = NULL;
	char *catch_local_b_one = NULL;
	char *catch_local_b_two = NULL;
	char *catch_local_a_one = NULL;
	char *catch_local_a_two = NULL;
	char *topic;
	void *payload = NULL;
	uint32_t payloadlen;
	uint8_t dup, qos, retain;
	uint16_t mid = 0;
	int rc = 0;
	uint8_t header = context->in_packet.command;
	int res = 0;
	struct mosquitto_msg_store *stored = NULL;
	int len;
	char *topic_mount;
	printf("~~~~~~~~~~~~~~~~~~~~~~~~\n");

	INITCONTEXT *head = getINITCONTEXT();
	INITCONTEXT *printhead = getINITCONTEXT();

#ifdef WITH_BRIDGE
	char *topic_temp;
	int i;
	struct _mqtt3_bridge_topic *cur_topic;
	bool match;
#endif

	dup = (header & 0x08)>>3;
	qos = (header & 0x06)>>1;
	if(qos == 3){				// 현재 qos 3 이면 안됨
		/*_mosquitto_log_printf(NULL, MOSQ_LOG_INFO,
				"Invalid QoS in PUBLISH from %s, disconnecting.", context->id);
		return 1;
		*/
		/*printf("==========   qos 3 ==========\n");
		printf("qos --> %d \n", qos);
		printf("payload ----> %s \n",context->in_packet.payload);
		printf("==============================\n");*/
	}
	retain = (header & 0x01);	// 메세지를 유지하도록 설정되어있는지 를 알아냄	메세지 헤더의 첫번재 비트에서 얻는다.
	
	printf("dup --> %d \n " ,dup);
	printf("qos --> %d \n", qos);
	printf("retain --> %d \n", retain);
	

	

	if(_mosquitto_read_string(&context->in_packet, &topic)) return 1;	//	topic 을 context 에 담는 곳
	if(STREMPTY(topic)){
		/* Invalid publish topic, disconnect client. */
		_mosquitto_free(topic);
		return 1;
	}
#ifdef WITH_BRIDGE
	if(context->bridge && context->bridge->topics && context->bridge->topic_remapping){
		for(i=0; i<context->bridge->topic_count; i++){
			cur_topic = &context->bridge->topics[i];
			if((cur_topic->direction == bd_both || cur_topic->direction == bd_in) 
					&& (cur_topic->remote_prefix || cur_topic->local_prefix)){

				/* Topic mapping required on this topic if the message matches */

				rc = mosquitto_topic_matches_sub(cur_topic->remote_topic, topic, &match);
				if(rc){
					_mosquitto_free(topic);
					return rc;
				}
				if(match){
					if(cur_topic->remote_prefix){
						/* This prefix needs removing. */
						if(!strncmp(cur_topic->remote_prefix, topic, strlen(cur_topic->remote_prefix))){
							topic_temp = _mosquitto_strdup(topic+strlen(cur_topic->remote_prefix));
							if(!topic_temp){
								_mosquitto_free(topic);
								return MOSQ_ERR_NOMEM;
							}
							_mosquitto_free(topic);
							topic = topic_temp;
						}
					}

					if(cur_topic->local_prefix){
						/* This prefix needs adding. */
						len = strlen(topic) + strlen(cur_topic->local_prefix)+1;
						topic_temp = _mosquitto_malloc(len+1);
						if(!topic_temp){
							_mosquitto_free(topic);
							return MOSQ_ERR_NOMEM;
						}
						snprintf(topic_temp, len, "%s%s", cur_topic->local_prefix, topic);
						topic_temp[len] = '\0';

						_mosquitto_free(topic);
						topic = topic_temp;
					}
					break;
				}
			}
		}
	}
#endif
	if(mosquitto_pub_topic_check(topic) != MOSQ_ERR_SUCCESS){	// topic check
		/* Invalid publish topic, just swallow it. */
		_mosquitto_free(topic);
		return 1;
	}
	
	
	if(qos > 0){
		if(_mosquitto_read_uint16(&context->in_packet, &mid)){
			_mosquitto_free(topic);
			return 1;
		}
	}

	payloadlen = context->in_packet.remaining_length - context->in_packet.pos;
	printf("variable_length --> %ld \n", context->in_packet.pos);
	printf("payload_length --> %ld \n", payloadlen);

#ifdef WITH_SYS_TREE
	g_pub_bytes_received += payloadlen;
#endif
	if(context->listener && context->listener->mount_point){		//토픽 트리에 쌓는것으로 추정??
		len = strlen(context->listener->mount_point) + strlen(topic) + 1;
		topic_mount = _mosquitto_malloc(len+1);
		if(!topic_mount){
			_mosquitto_free(topic);
			return MOSQ_ERR_NOMEM;
		}
		//printf("??? %s,,,,%s\n", context->listener->mount_point, topic);
		snprintf(topic_mount, len, "%s%s", context->listener->mount_point, topic);
		topic_mount[len] = '\0';

		_mosquitto_free(topic);
		topic = topic_mount;
	}


	if(payloadlen){
		if(db->config->message_size_limit && payloadlen > db->config->message_size_limit){
			_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Dropped too large PUBLISH from %s (d%d, q%d, r%d, m%d, '%s', ... (%ld bytes))", context->id, dup, qos, retain, mid, topic, (long)payloadlen);
			goto process_bad_message;
		}
		payload = _mosquitto_calloc(payloadlen+1, 1);
		if(!payload){
			_mosquitto_free(topic);
			return 1;
		}
		if(_mosquitto_read_bytes(&context->in_packet, payload, payloadlen)){
			_mosquitto_free(topic);
			_mosquitto_free(payload);
			return 1;
		}
	}

	/* Check for topic access */
	rc = mosquitto_acl_check(db, context, topic, MOSQ_ACL_WRITE);
	if(rc == MOSQ_ERR_ACL_DENIED){
		_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Denied PUBLISH from %s (d%d, q%d, r%d, m%d, '%s', ... (%ld bytes))", context->id, dup, qos, retain, mid, topic, (long)payloadlen);
		goto process_bad_message;
	}else if(rc != MOSQ_ERR_SUCCESS){
		_mosquitto_free(topic);
		if(payload) _mosquitto_free(payload);
		return rc;
	}

	_mosquitto_log_printf(NULL, MOSQ_LOG_DEBUG, "Received PUBLISH from %s (d%d, q%d, r%d, m%d, '%s', ... (%ld bytes))", context->id, dup, qos, retain, mid, topic, (long)payloadlen);
	if(qos > 0){
		mqtt3_db_message_store_find(context, mid, &stored);
	}

	
	if (!stored) {
		dup = 0;
		if (mqtt3_db_message_store(db, context->id, mid, topic, qos, payloadlen, payload, retain, &stored, 0)) {
			_mosquitto_free(topic);
			if (payload) _mosquitto_free(payload);
			return 1;
		}
	}
	else {
		dup = 1;
	}
	/*if (strcmp(topic, "change") != 0) {
		printf("topic is not !! change\n");

		if (!stored) {
			dup = 0;
			if (mqtt3_db_message_store(db, context->id, mid, topic, qos, payloadlen, payload, retain, &stored, 0)) {
				_mosquitto_free(topic);
				if (payload) _mosquitto_free(payload);
				return 1;
			}
		}
		else {
			dup = 1;
		}
	}
	else {
		printf("topic is change \n");
	}*/

	


	/*printf("zzzzzzzzzzzzzzzzzzzzzzz\n");
	printf("zzzz   %s\n", db->subs.children->children->children->topic);	//트리의 첫번재 부분만 조짐 
	printf("vvvv   %s\n", topic);*/

	/*if (qos == 3) {
		printf("==========   qos 3 ==========\n");
		printf("qos --> %d \n", qos);
		printf("payload ----> %s \n",payload);
		printf("==============================\n");
		qoqo++;
		printf("qoqoqoqoqo ---->>> %d\n", qoqo);

		return rc;
	}*/
	
	if (qos == 3) {
		
		printf("==========   qos 3 ==========\n");
		printf("qos --> %d \n", qos);
		printf("payload ----> %s \n", payload);
		printf("==============================\n");
		qoqo++;
		printf("qoqoqoqoqo ---->>> %d\n", qoqo);


		//printf("");
		//printf("first topic tree  ---->  %s\n", db->subs.children->children->children->topic);
		//printf("topic in context id ---> %s\n", db->subs.children->children->children->subs->context->id);
		//printf("second topic tree  ---->  %s\n", db->subs.children->children->children->next->topic);
		catch_message = strdup(payload);
		printf("%s\n",catch_message);
		char *ptr = strtok(catch_message,"/");
		catch_id = strdup(ptr);
		
		int i = 0;
		char *sub_ptr = NULL;

		while (ptr != NULL) {
			ptr = strtok(NULL,"/");
			if (i == 0) {
				catch_message_b = strdup(ptr);
				printf("%s\n", catch_message_b);
				/*sub_ptr = strtok(catch_message_b,",");
				catch_local_b_one = strdup(sub_ptr);
				sub_ptr = strtok(NULL, ",");
				catch_local_b_two = strdup(sub_ptr);
				printf("%s\n", catch_local_b_one);
				printf("%s\n", catch_local_b_two);*/
			}
			else if (i == 1) {
				catch_message_a = strdup(ptr);
				printf("%s\n", catch_message_a);
				/*catch_message_a = strdup(ptr);
				printf("%s\n", catch_message_a);
				sub_ptr = strtok(catch_message_a, ",");
				catch_local_a_one = strdup(sub_ptr);
				sub_ptr = strtok(NULL, ",");
				catch_local_a_two = strdup(sub_ptr);
				printf("%s\n", catch_local_a_one);
				printf("%s\n", catch_local_a_two);*/
			}
			i++;
		}
		//////////////////////////////////////////////////
		sub_ptr = strtok(catch_message_b, ",");
		catch_local_b_one = strdup(sub_ptr);
		sub_ptr = strtok(NULL, ",");
		catch_local_b_two = strdup(sub_ptr);
		printf("%s\n", catch_local_b_one);
		printf("%s\n", catch_local_b_two);

		sub_ptr = strtok(catch_message_a, ",");
		catch_local_a_one = strdup(sub_ptr);
		sub_ptr = strtok(NULL, ",");
		catch_local_a_two = strdup(sub_ptr);
		printf("%s\n", catch_local_a_one);
		printf("%s\n", catch_local_a_two);
		//////////////////////////////////////////////////

		
		
		/*while (ptr != NULL) {
			ptr = strtok(NULL, ",");
			if (i == 0) {
				catch_local_one = strdup(ptr);
			}
			else if(i == 1) {
				catch_local_two = strdup(ptr);
			}

			i++;
		}*/
		
		printf("before location gu name ---> %s\n", catch_local_b_one);
		printf("before location dong name ---> %s\n", catch_local_b_two);
		
		printf("after location gu name ---> %s\n", catch_local_a_one);
		printf("after location dong name ---> %s\n", catch_local_a_two);

		struct _mosquitto_subhier *check_subhier = NULL;
		struct _mosquitto_subhier *check_subhier2 = NULL;
		struct _mosquitto_subleaf *check_subleaf = NULL;
		//check_subhier = db->subs.children->children->children;
		if (strcmp(catch_local_b_one, "null") == 0 && strcmp(catch_local_b_two, "null") == 0) {	//처음인 경우
			printf("처음인 경우\n");

			INITCONTEXT *q = head;
			INITCONTEXT *p = q;
			
			while (q != NULL)
			{
				
					//printf("%s %s !!!\n", catch_id, q->context->id);
				if (strcmp(catch_id, q->context->id) == 0) {
					printf("찾았다!\n");

					//p->next = q->next;
					//remove(q);

					printf("====변경후 context 연결리스트===\n");
					//display(head);
					printf("================================\n");
					if (catch_local_a_one != NULL && catch_local_a_two != NULL) {
						mqtt3_sub_add(db, q->context, catch_local_a_one, 0, &db->subs);
						mqtt3_sub_add(db, q->context, q->context->id, 0, &db->subs);
						mqtt3_sub_add(db, q->context, catch_local_a_two, 0, &db->subs);
					}
					break;
				}
				

				p = q;
				q = q->next;
			}
			
			//mqtt3_sub_add(db, context, catch_local_a_one, 0, &db->subs);
			//mqtt3_sub_add(db, context, catch_local_a_two, 0, &db->subs);
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");
			printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ \n");

		}
		
		else if (strcmp(catch_local_b_one, "null") == 0 && strcmp(catch_local_b_two, "null") != 0) {		//구는 그대로 동만 바뀔경우
			printf("구는 그대로 동만 바뀔경우\n");
			
			if (db->subs.children->children->children != NULL) {
				check_subhier = db->subs.children->children->children;
			}
			while (check_subhier != NULL) {
				if (strcmp(check_subhier->topic, catch_local_b_two) == 0) {
					printf("동만 바뀔때 찾았다!!!!\n");
					if (check_subhier->subs != NULL) {
						check_subleaf = check_subhier->subs;
						while (check_subleaf != NULL){
							if (strcmp(check_subleaf->context->id, catch_id) == 0) {
								printf("동만 바뀔때 컨텍스트 찾았다!!!!\n");
								temp_context = check_subleaf->context;
								mqtt3_sub_remove(db, temp_context, catch_local_b_two, &db->subs);
								mqtt3_sub_add(db, temp_context, catch_local_a_two, 0, &db->subs);
								break;
							}
							check_subleaf = check_subleaf->next;
						}
					}
					break;
				}
				check_subhier = check_subhier->next;
			}

			//mqtt3_sub_remove(db, context, catch_local_b_two, &db->subs);
			//mqtt3_sub_add(db, context, catch_local_a_two, 0, &db->subs);
			
			
		}
		else if (strcmp(catch_local_b_one, "null") != 0 && strcmp(catch_local_b_two, "null") != 0) {	//구 , 동 전부 바뀔경우
			printf("구 , 동 전부 바뀔경우\n");
			if (db->subs.children->children->children != NULL) {
				check_subhier = db->subs.children->children->children;
				check_subhier2 = db->subs.children->children->children;
			}

			while (check_subhier != NULL) {
				if (strcmp(check_subhier->topic, catch_local_b_one) == 0) {
					printf("구 바뀔때 찾았다!!!!\n");
					if (check_subhier->subs != NULL) {
						check_subleaf = check_subhier->subs;
						while (check_subleaf != NULL) {
							if (strcmp(check_subleaf->context->id, catch_id) == 0) {
								printf("구 바뀔때 컨텍스트 찾았다!!!!\n");
								temp_context = check_subleaf->context;
								mqtt3_sub_remove(db, temp_context, catch_local_b_one, &db->subs);
								mqtt3_sub_add(db, temp_context, catch_local_a_one, 0, &db->subs);
								break;
							}
							check_subleaf = check_subleaf->next;
						}
					}
					break;
				}
				check_subhier = check_subhier->next;
			}
			
			while (check_subhier2 != NULL) {
				if (strcmp(check_subhier2->topic, catch_local_b_two) == 0) {
					printf("동만 바뀔때 찾았다!!!!\n");
					if (check_subhier2->subs != NULL) {
						check_subleaf = check_subhier2->subs;
						while (check_subleaf != NULL) {
							if (strcmp(check_subleaf->context->id, catch_id) == 0) {
								printf("동만 바뀔때 컨텍스트 찾았다!!!!\n");
								temp_context = check_subleaf->context;
								mqtt3_sub_remove(db, temp_context, catch_local_b_two, &db->subs);
								mqtt3_sub_add(db, temp_context, catch_local_a_two, 0, &db->subs);
								break;
							}
							check_subleaf = check_subleaf->next;
						}
					}
					break;
				}
				check_subhier2 = check_subhier2->next;
			}
			/*char * temp_topic = check_subhier->topic;
			while (check_subhier != NULL) {
				printf("초입부\n");
				temp_topic = check_subhier->topic;
				if (strcmp(temp_topic, catch_local_b_one) == 0) {
					printf("구 바뀔때 찾았다!!!!\n");
					if (check_subhier->subs != NULL) {
						check_subleaf = check_subhier->subs;
						while (check_subleaf != NULL) {
							if (strcmp(check_subleaf->context->id, catch_id) == 0) {
								printf("구 바뀔때 컨텍스트 찾았다!!!!\n");
								temp_context = check_subleaf->context;
								mqtt3_sub_remove(db, temp_context, catch_local_b_one, &db->subs);
								mqtt3_sub_add(db, temp_context, catch_local_a_one, 0, &db->subs);
								break;
							}
							check_subleaf = check_subleaf->next;
						}
						printf("낄낄낄\n");
					}
					
					printf("깔깔깔\n");
					temp_topic = NULL;
					temp_topic = check_subhier->topic;
				}else if (strcmp(temp_topic, catch_local_b_two) == 0) {
					printf("동 바뀔때 찾았다!!!!\n");
					if (check_subhier->subs != NULL) {
						check_subleaf = check_subhier->subs;
						while (check_subleaf != NULL) {
							if (strcmp(check_subleaf->context->id, catch_id) == 0) {
								printf("동 바뀔때 컨텍스트 찾았다!!!!\n");
								temp_context = check_subleaf->context;
								mqtt3_sub_remove(db, temp_context, catch_local_b_two, &db->subs);
								mqtt3_sub_add(db, temp_context, catch_local_a_two, 0, &db->subs);
								break;
							}
							check_subleaf = check_subleaf->next;
						}
					}
					
				}
				printf("여기\n");
				check_subhier = check_subhier->next;
				if (check_subhier == NULL) break;
				printf("요기\n");
			}*/
		}
		
		//printf("loop check topic                --> %s\n", check_subhier->topic);
		//printf("loop check topic in context id  --> %s\n", check_subhier->subs->context->id);

		//check_subhier = check_subhier->next;
		/*while (check_subhier != NULL) {

			if (strcmp(catch_local_b_one, "null") == 0 && strcmp(catch_local_b_two, "null") == 0 ) {	//처음인 경우
				printf("처음인 경우\n");
				
				mqtt3_sub_add(db, context, catch_local_a_one, 0, &db->subs);
				
			}
			else if (strcmp(catch_local_b_one, "null")== 0  && strcmp(catch_local_b_two, "null") !=0) {		//구는 그대로 동만 바뀔경우
				printf("구는 그대로 동만 바뀔경우\n");
			}
			else if (strcmp(catch_local_b_one, "null") != 0 && strcmp(catch_local_b_two, "null") != 0) {	//구 , 동 전부 바뀔경우
				printf("구 , 동 전부 바뀔경우\n");
			}

			printf("loop check topic                --> %s\n", check_subhier->topic);
			printf("loop check topic in context id  --> %s\n",check_subhier->subs->context->id);

			check_subhier = check_subhier->next;
		}*/
		
		
		
		/*printf("first topic tree  ---->  %s\n", db->subs.children->children->children->topic);
		printf("topic in context id ---> %s\n", db->subs.children->children->children->subs->context->id);
		printf("second topic tree  ---->  %s\n", db->subs.children->children->children->next->topic);*/

		return rc;
	}
	
	//char *ptr = strtok(catch_message,"/");
	
	/*while (ptr != NULL) {
		printf("%s\n", ptr);
		ptr = strtok(NULL, ",");
	}*/

	switch(qos){		//qos 별로 db에 저장한다.
		case 0:
			if(mqtt3_db_messages_queue(db, context->id, topic, qos, retain, &stored)) rc = 1;		// message 가 게시되는 곳
			printf("=======sendsendsendsend=====\n");
			break;
		case 1:
			if(mqtt3_db_messages_queue(db, context->id, topic, qos, retain, &stored)) rc = 1;
			if(_mosquitto_send_puback(context, mid)) rc = 1;
			break;
		case 2:
			if(!dup){
				res = mqtt3_db_message_insert(db, context, mid, mosq_md_in, qos, retain, stored);
			}else{
				res = 0;
			}
			/* mqtt3_db_message_insert() returns 2 to indicate dropped message
			 * due to queue. This isn't an error so don't disconnect them. */
			if(!res){
				if(_mosquitto_send_pubrec(context, mid)) rc = 1;
			}else if(res == 1){
				rc = 1;
			}
			break;
	}
	_mosquitto_free(topic);
	if(payload) _mosquitto_free(payload);

	return rc;
process_bad_message:
	_mosquitto_free(topic);
	if(payload) _mosquitto_free(payload);
	switch(qos){
		case 0:
			return MOSQ_ERR_SUCCESS;
		case 1:
			return _mosquitto_send_puback(context, mid);
		case 2:
			mqtt3_db_message_store_find(context, mid, &stored);
			if(!stored){
				if(mqtt3_db_message_store(db, context->id, mid, NULL, qos, 0, NULL, false, &stored, 0)){
					return 1;
				}
				res = mqtt3_db_message_insert(db, context, mid, mosq_md_in, qos, false, stored);
			}else{
				res = 0;
			}
			if(!res){
				res = _mosquitto_send_pubrec(context, mid);
			}
			return res;
	}
	return 1;
}

