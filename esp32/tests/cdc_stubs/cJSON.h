#pragma once
#include <stdbool.h>
typedef struct cJSON cJSON;
cJSON *cJSON_CreateObject(void);
bool cJSON_AddNumberToObject(cJSON *,const char *,double);
bool cJSON_AddStringToObject(cJSON *,const char *,const char *);
bool cJSON_PrintPreallocated(cJSON *,char *,int,bool);
void cJSON_Delete(cJSON *);
