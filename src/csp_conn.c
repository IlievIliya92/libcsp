/*
Cubesat Space Protocol - A small network-layer protocol designed for Cubesats
Copyright (C) 2010 GomSpace ApS (gomspace.com)
Copyright (C) 2010 AAUSAT3 Project (aausat3.space.aau.dk) 

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdlib.h>
#include <stdio.h>

/* CSP includes */
#include <csp/csp.h>
#include <csp/csp_thread.h>
#include <csp/csp_queue.h>
#include <csp/csp_semaphore.h>
#include <csp/csp_malloc.h>
#include <csp/csp_time.h>

/* Static connection pool */
csp_conn_t arr_conn[MAX_STATIC_CONNS];

/** csp_conn_init
 * Initialises the connection pool
 */
void csp_conn_init(void) {

	int i;
	for (i = 0; i < MAX_STATIC_CONNS; i++) {
        csp_bin_sem_create(&arr_conn[i].tx_sem);
        arr_conn[i].rx_queue = csp_queue_create(20, sizeof(csp_packet_t *));
		arr_conn[i].state = SOCKET_CLOSED;
		arr_conn[i].rxmalloc = NULL;
	}

}

/** csp_conn_find
 * Used by the incoming data handler this function searches
 * for an already established connection with a given incoming identifier.
 * The mask field is used to select which parts of the identifier that constitute a
 * unique connection
 * 
 * @return A connection pointer to the matching connection or NULL if no matching connection was found
 */
csp_conn_t * csp_conn_find(uint32_t id, uint32_t mask) {

	/* Search for matching connection */
	int i;
	csp_conn_t * conn;

    for (i = 0; i < MAX_STATIC_CONNS; i++) {
		conn = &arr_conn[i];
		if(((conn->idin.ext & mask) == (id & mask)) && (conn->state != SOCKET_CLOSED))
			return conn;
    }

    return NULL;

}

/** csp_conn_new
 * Finds an unused conn or creates a conn 
 * 
 * @return a pointer to the newly established connection or NULL
 */
csp_conn_t * csp_conn_new(csp_id_t idin, csp_id_t idout) {

    /* Search for free connection */
    int i;
    csp_conn_t * conn;

	for (i = 0; i < MAX_STATIC_CONNS; i++) {
		conn = &arr_conn[i];

		if(conn->state == SOCKET_CLOSED) {
			conn->state = SOCKET_OPEN;
            conn->idin = idin;
            conn->idout = idout;
            return conn;
        }
    }
    
    return NULL;
  
}

/** csp_close
 * Closes a given connection and frees the buffer if more than 8 bytes
 * if the connection uses an outgoing port this port must also be closed
 * A dynamically allocated connection must be freed.
 */

void csp_close(csp_conn_t * conn) {
   
	/* Ensure connection queue is empty */
	csp_packet_t * packet;
    while(csp_queue_dequeue(conn->rx_queue, &packet, 0) == CSP_QUEUE_OK)
    	csp_buffer_free(packet);

	/* Remove dynamic allocated buffer */
	if (conn->rxmalloc != NULL) {
		csp_buffer_free(conn->rxmalloc);
		conn->rxmalloc = NULL;
		csp_debug("Close conn with active RX malloc!\r\n");
	}

    /* Set to closed */
    conn->state = SOCKET_CLOSED;

}

/** csp_connect
 * Used to establish outgoing connections
 * This function searches the port table for free slots and finds an unused
 * connection from the connection pool
 * There is no handshake in the CSP protocol
 * @return a pointer to a new connection or NULL
 */
csp_conn_t * csp_connect(uint8_t prio, uint8_t dest, uint8_t dport) {

    /* Find an unused port.
	 * @todo: Implement sourceport conflict resolving */
	unsigned char sport = 31;

	/* Generate CAN identifier */
	csp_id_t incoming_id, outgoing_id;
	incoming_id.pri = prio;
	incoming_id.dst = my_address;
	incoming_id.src = dest;
	incoming_id.dport = sport;
	incoming_id.sport = dport;
	outgoing_id.pri = prio;
	outgoing_id.dst = dest;
	outgoing_id.src = my_address;
	outgoing_id.dport = dport;
	outgoing_id.sport = sport;
    csp_conn_t * conn = csp_conn_new(incoming_id, outgoing_id);

    return conn;

}
