#ifndef _conez_http_h
#define _conez_http_h

void http_dir();
void http_nvs();
void http_reboot();
void http_root();
void http_config_get();
void http_config_post();
void http_config_reset();

int http_setup();
int http_loop();

#endif