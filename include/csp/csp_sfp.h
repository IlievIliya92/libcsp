/*****************************************************************************
 * **File:** csp/csp_sfp.h
 *
 * **Description:** Simple Fragmentation Protocol (SFP).
 *
 * The SFP API can transfer a blob of data across an established CSP connection,
 * by chopping the data into smaller chunks of data, that can fit into a single CSP message.
 *
 * SFP will add a small header to each packet, containing information about the transfer.
 * SFP is usually sent over a RDP connection (which also adds a header),
 ****************************************************************************/
#pragma once

#include <string.h> // memcpy()

#include <csp/csp_types.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Send data over a CSP connection.
 *
 * Data will be send in chunks of \a mtu bytes. The MTU must be small enough to fit
 * into a CSP packat + SFP header + other transport headers.
 *
 * csp_sfp_recv() or csp_sfp_recv_fp() can be used at the other end to receive data.
 *
 * This is useful if you wish to send data stored in flash memory or another location, where standard memcpy() doesn't work.
 *
 * @param[in] conn established connection for sending SFP packets.
 * @param[in] data data to send
 * @param[in] datasize  tsize of \a data
 * @param[in] mtu  maximum transfer unit (bytes), max data chunk to send.
 * @param[in] timeout unused as of CSP version 1.6
 * @param[in] memcpyfcn memory copy function.
 * @return #CSP_ERR_NONE on success, otherwise an error.
 */
int csp_sfp_send_own_memcpy(csp_conn_t * conn, const void * data, unsigned int datasize, unsigned int mtu, uint32_t timeout, csp_memcpy_fnc_t memcpyfcn);

/**
 * Send data over a CSP connection.
 *
 * Uses csp_sfp_send_own_memcpy() with standard memcpy().
 *
 * @param[in] conn established connection for sending SFP packets.
 * @param[in] data data to send
 * @param[in] datasize  size of \a data
 * @param[in] mtu  maximum transfer unit (bytes), max data chunk to send.
 * @param[in] timeout unused as of CSP version 1.6
 * @return #CSP_ERR_NONE on success, otherwise an error.
 */
static inline int csp_sfp_send(csp_conn_t * conn, const void * data, unsigned int datasize, unsigned int mtu, uint32_t timeout) {
	return csp_sfp_send_own_memcpy(conn, data, datasize, mtu, timeout, (csp_memcpy_fnc_t) &memcpy);
}

/**
 * Receive data over a CSP connection.
 *
 * This is the counterpart to the csp_sfp_send() and csp_sfp_send_own_memcpy().
 *
 * @param[in] conn established connection for receiving SFP packets.
 * @param[out] dataout received data on success. Allocated with malloc(), so
 * 			   should be freed with free(). The pointer will be NULL on failure.
 * @param[out] datasize size of received data.
 * @param[in] timeout timeout in ms to wait for csp_read()
 * @param[in] first_packet First packet of a SFP transfer.
 * 			  Use NULL to receive first packet on the connection.
 * @return #CSP_ERR_NONE on success, otherwise an error.
 */
int csp_sfp_recv_fp(csp_conn_t * conn, void ** dataout, int * datasize, uint32_t timeout, csp_packet_t * first_packet);

/**
 * Receive data over a CSP connection.
 *
 * This is the counterpart to the csp_sfp_send() and csp_sfp_send_own_memcpy().
 *
 * @param[in] conn established connection for receiving SFP packets.
 * @param[out] dataout received data on success. Allocated with malloc(), so
 * 			  should be freed with free(). The pointer will be NULL on failure.
 * @param[out] datasize size of received data.
 * @param[in] timeout timeout in ms to wait for csp_read()
 * @return #CSP_ERR_NONE on success, otherwise an error.
*/
static inline int csp_sfp_recv(csp_conn_t * conn, void ** dataout, int * datasize, uint32_t timeout) {
	return csp_sfp_recv_fp(conn, dataout, datasize, timeout, NULL);
}

#ifdef __cplusplus
}
#endif
