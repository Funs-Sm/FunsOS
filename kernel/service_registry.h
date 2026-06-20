#ifndef SERVICE_REGISTRY_H
#define SERVICE_REGISTRY_H

/* System service registry - registers all core system services
 * and provides status reporting. */

int register_core_services(void);
void print_service_status(void);

#endif
