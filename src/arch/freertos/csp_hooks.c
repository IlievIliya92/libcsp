#include <csp/csp_hooks.h>
#include "csp_macro.h"

__weak uint32_t csp_memfree_hook(void) {
	return 0;
}

__weak unsigned int csp_ps_hook(csp_packet_t * packet) {
	(void)packet; /* Avoid compiler warnings about unused parameter */
	return 0;
}

__weak void csp_reboot_hook(void) {
}

__weak void csp_shutdown_hook(void) {
}
