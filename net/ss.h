#ifndef SS_H
#define SS_H

#include "stdint.h"

#define SS_OPT_TCP      0x01
#define SS_OPT_UDP      0x02
#define SS_OPT_LISTEN   0x04
#define SS_OPT_NUMERIC  0x08
#define SS_OPT_ALL      0x10

#define SS_DEFAULT_OPT (SS_OPT_TCP | SS_OPT_UDP)

typedef struct {
    uint32_t options;
    uint16_t sport_filter;
    uint16_t dport_filter;
    uint32_t src_ip_filter;
    uint32_t dst_ip_filter;
} ss_config_t;

void ss_init(void);
int  ss_format(ss_config_t *config, char *out, uint32_t cap);
int  ss_format_summary(char *out, uint32_t cap);

int  ss_parse_args(int argc, char *argv[], ss_config_t *config);

#endif
