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

#ifndef _CSP_PORT_H_
#define _CSP_PORT_H_

#include <stdint.h>

#include <csp/csp.h>

/** @brief Port states */
typedef enum {
    PORT_CLOSED = 0,
    PORT_OPEN = 1,
} csp_port_state_t;

/** @brief Port struct */
typedef struct {
    csp_port_state_t state;         // Port state
    void (*callback) (csp_conn_t*); // Pointer to callback function
    csp_socket_t * socket;          // New connections are added to this socket's conn queue
} csp_port_t;

extern csp_port_t ports[];
void csp_port_init(void);

#endif // _CSP_PORT_H_