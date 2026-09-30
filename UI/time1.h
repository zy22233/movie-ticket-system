#ifndef __TIME1_H__
#define __TIME1_H__

#include "ui.h"
extern int tcpfd;

void date1(lv_obj_t *label);
void clock1(lv_obj_t *label);
void week1(lv_obj_t *label);

void weather1(lv_obj_t *label);
void *weather_thread(void *arg);   
void weather_update(void);

int bemfa_Client(const char * com);
void * led_flow(void *arg);
void * recv_msg(void *arg);
void * ping_msg(void *arg);


#endif
