// It is recommended to always define PY_SSIZE_T_CLEAN before including Python.h
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <csp/csp.h>
#include <csp/csp_cmp.h>
#include <csp/interfaces/csp_if_zmqhub.h>
#include <csp/interfaces/csp_if_kiss.h>
#include <csp/drivers/usart.h>
#include <csp/drivers/can_socketcan.h>
#include <endian.h>

#define SOCKET_CAPSULE     "csp_socket_t"
#define CONNECTION_CAPSULE "csp_conn_t"
#define PACKET_CAPSULE     "csp_packet_t"

static PyObject * Error = NULL;

static int CSP_POINTER_HAS_BEEN_FREED = 0;  // used to indicate pointer has been freed, because a NULL pointer can't be set.

static void * get_capsule_pointer(PyObject * capsule, const char * expected_type, bool allow_null) {
	const char * capsule_name = PyCapsule_GetName(capsule);
	if (strcmp(capsule_name, expected_type) != 0) {
		PyErr_Format(PyExc_TypeError,
					 "capsule contains unexpected type, expected=%s, got=%s",
					 expected_type, capsule_name);  // TypeError is thrown
		return NULL;
	}
	void * ptr = PyCapsule_GetPointer(capsule, expected_type);
	if (ptr == &CSP_POINTER_HAS_BEEN_FREED) {
		ptr = NULL;
	}
	if ((ptr == NULL) && !allow_null) {
		PyErr_Format(PyExc_TypeError,
					 "capsule: type=%s - already freed",
					 expected_type);  // TypeError is thrown
		return NULL;
	}
	return ptr;
}

static csp_packet_t * get_obj_as_packet(PyObject * obj, bool allow_null) {
	return get_capsule_pointer(obj, PACKET_CAPSULE, allow_null);
}

static csp_conn_t * get_obj_as_conn(PyObject * obj, bool allow_null) {
	return get_capsule_pointer(obj, CONNECTION_CAPSULE, allow_null);
}

static csp_socket_t * get_obj_as_socket(PyObject * obj, bool allow_null) {
	return get_capsule_pointer(obj, SOCKET_CAPSULE, allow_null);
}

static PyObject * PyErr_Error(const char * message, int error) {
	PyErr_Format(Error, "%s, result/error: %d", message, error);  // should set error as member
	return NULL;
}

static void pycsp_free_csp_buffer(PyObject * obj) {
	csp_packet_t * packet = get_obj_as_packet(obj, true);
	if (packet) {
		csp_buffer_free(packet);
	}
	PyCapsule_SetPointer(obj, &CSP_POINTER_HAS_BEEN_FREED);
}

static void pycsp_free_csp_conn(PyObject * obj) {
	csp_conn_t * conn = get_obj_as_conn(obj, true);
	if (conn) {
		csp_close(conn);
	}
	PyCapsule_SetPointer(obj, &CSP_POINTER_HAS_BEEN_FREED);
}

static void pycsp_free_csp_socket(PyObject * obj) {
	csp_socket_t * socket = get_obj_as_socket(obj, true);
	if (socket) {
		csp_socket_close(socket);
		PyMem_RawFree(socket);
	}

	PyCapsule_SetPointer(obj, &CSP_POINTER_HAS_BEEN_FREED);
}

static PyObject * pycsp_service_handler(PyObject * self, PyObject * args) {
	PyObject * conn_capsule;
	PyObject * packet_capsule;
	if (!PyArg_ParseTuple(args, "OO", &conn_capsule, &packet_capsule)) {
		return NULL;  // TypeError is thrown
	}
	csp_conn_t * conn = get_obj_as_conn(conn_capsule, false);
	if (conn == NULL) {
		return NULL;
	}
	csp_packet_t * packet = get_obj_as_packet(packet_capsule, false);
	if (packet == NULL) {
		return NULL;
	}

	csp_service_handler(packet);
	PyCapsule_SetPointer(packet_capsule, &CSP_POINTER_HAS_BEEN_FREED);
	Py_RETURN_NONE;
}

static PyObject * pycsp_init(PyObject * self, PyObject * args) {
	if (!PyArg_ParseTuple(args, "sss|bIb", &csp_conf.hostname, &csp_conf.model,
						  &csp_conf.revision, &csp_conf.version, &csp_conf.conn_dfl_so, &csp_conf.dedup)) {

		return NULL;
	}

	csp_init();

	Py_RETURN_NONE;
}

static PyObject * pycsp_get_hostname(PyObject * self, PyObject * args) {
	return Py_BuildValue("s", csp_get_conf()->hostname);
}

static PyObject * pycsp_get_model(PyObject * self, PyObject * args) {
	return Py_BuildValue("s", csp_get_conf()->model);
}

static PyObject * pycsp_get_revision(PyObject * self, PyObject * args) {
	return Py_BuildValue("s", csp_get_conf()->revision);
}

static PyObject * pycsp_socket(PyObject * self, PyObject * args) {
	uint32_t opts = CSP_SO_NONE;
	if (!PyArg_ParseTuple(args, "|I", &opts)) {
		return NULL;  // TypeError is thrown
	}

	csp_socket_t * socket = PyMem_RawCalloc(1, sizeof(*socket));
	if (socket == NULL) {
		return PyErr_NoMemory();
	}

	socket->opts = opts;

	return PyCapsule_New(socket, SOCKET_CAPSULE, pycsp_free_csp_socket);
}

static PyObject * pycsp_accept(PyObject * self, PyObject * args) {
	PyObject * socket_capsule;
	uint32_t timeout = CSP_MAX_TIMEOUT;
	if (!PyArg_ParseTuple(args, "O|I", &socket_capsule, &timeout)) {
		return NULL;  // TypeError is thrown
	}
	csp_socket_t * socket = get_obj_as_socket(socket_capsule, false);
	if (socket == NULL) {
		return NULL;
	}
	csp_conn_t * conn;
	Py_BEGIN_ALLOW_THREADS;
	conn = csp_accept(socket, timeout);
	Py_END_ALLOW_THREADS;
	if (conn == NULL) {
		Py_RETURN_NONE;  // timeout -> None
	}

	return PyCapsule_New(conn, CONNECTION_CAPSULE, pycsp_free_csp_conn);
}

static PyObject * pycsp_read(PyObject * self, PyObject * args) {
	PyObject * conn_capsule;
	uint32_t timeout = 500;
	if (!PyArg_ParseTuple(args, "O|I", &conn_capsule, &timeout)) {
		return NULL;  // TypeError is thrown
	}
	csp_conn_t * conn = get_obj_as_conn(conn_capsule, false);
	if (conn == NULL) {
		return NULL;
	}
	csp_packet_t * packet;
	Py_BEGIN_ALLOW_THREADS;
	packet = csp_read(conn, timeout);
	Py_END_ALLOW_THREADS;
	if (packet == NULL) {
		Py_RETURN_NONE;  // timeout -> None
	}

	return PyCapsule_New(packet, PACKET_CAPSULE, pycsp_free_csp_buffer);
}

static PyObject * pycsp_send(PyObject * self, PyObject * args) {
	PyObject * conn_capsule;
	PyObject * packet_capsule;
	uint32_t timeout = 1000;
	if (!PyArg_ParseTuple(args, "OO|I", &conn_capsule, &packet_capsule, &timeout)) {
		return NULL;  // TypeError is thrown
	}
	csp_conn_t * conn = get_obj_as_conn(conn_capsule, false);
	if (conn == NULL) {
		return NULL;
	}
	csp_packet_t * packet = get_obj_as_packet(packet_capsule, false);
	if (packet == NULL) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS;
	csp_send(conn, packet);
	Py_END_ALLOW_THREADS;

	PyCapsule_SetPointer(packet_capsule, &CSP_POINTER_HAS_BEEN_FREED);

	Py_RETURN_NONE;
}

#if 0
static PyObject* pycsp_sfp_send(PyObject *self, PyObject *args) {
    PyObject* conn_capsule;
    Py_buffer data;
    unsigned int mtu;
    uint32_t timeout = 1000;
    if (!PyArg_ParseTuple(args, "Ow*II|I", &conn_capsule, &data, &mtu, &timeout)) {
        return NULL;
    }

    csp_conn_t* conn = get_obj_as_conn(conn_capsule, false);
    if (conn == NULL) {
        return NULL;
    }

    int res;
    Py_BEGIN_ALLOW_THREADS;
    res = csp_sfp_send(conn, data.buf, data.len, mtu, timeout);
    Py_END_ALLOW_THREADS;
    if (res != CSP_ERR_NONE) {
        return PyErr_Error("sfp_send()", res);
    }

    return Py_BuildValue("i", res);
}

static PyObject* pycsp_sfp_recv(PyObject *self, PyObject *args) {
    PyObject* conn_capsule;
    uint32_t timeout = 500;
    if (!PyArg_ParseTuple(args, "O|I", &conn_capsule, &timeout)) {
        return NULL; // TypeError is thrown
    }
    csp_conn_t* conn = get_obj_as_conn(conn_capsule, false);
    if (conn == NULL) {
        return NULL;
    }
    void *dataout = NULL;
    int return_datasize = 0;
    int res;
    Py_BEGIN_ALLOW_THREADS;
    res = csp_sfp_recv(conn, &dataout, &return_datasize, timeout);
    Py_END_ALLOW_THREADS;

    if (dataout == NULL) {
        Py_RETURN_NONE;
    }

    if (res != CSP_ERR_NONE) {
        return PyErr_Error("sfp_recv()", res);
    }

    return PyCapsule_New(dataout, PACKET_CAPSULE, pycsp_free_csp_buffer);
}
#endif

static PyObject * pycsp_transaction(PyObject * self, PyObject * args) {
	uint8_t prio;
	uint16_t dest;
	uint8_t port;
	uint32_t timeout;
	Py_buffer inbuf;
	Py_buffer outbuf;
	int allow_any_incoming_length = 0;
	if (!PyArg_ParseTuple(args, "bHbIw*w*|i", &prio, &dest, &port, &timeout, &outbuf, &inbuf, &allow_any_incoming_length)) {
		return NULL;  // TypeError is thrown
	}

	int incoming_buffer_len = allow_any_incoming_length ? -1 : inbuf.len;

	int res;
	Py_BEGIN_ALLOW_THREADS;
	res = csp_transaction(prio, dest, port, timeout, outbuf.buf, outbuf.len, inbuf.buf, incoming_buffer_len);
	Py_END_ALLOW_THREADS;
	if (res < 1) {
		return PyErr_Error("csp_transaction()", res);
	}

	return Py_BuildValue("i", res);
}

static PyObject * pycsp_sendto(PyObject * self, PyObject * args) {
	uint8_t prio;
	uint16_t dest;
	uint8_t dport;
	uint8_t src_port;
	uint32_t opts;
	PyObject * packet_capsule;
	if (!PyArg_ParseTuple(args, "bHbbIO", &prio, &dest, &dport, &src_port, &opts, &packet_capsule)) {
		Py_RETURN_NONE;
	}
	csp_packet_t * packet = get_obj_as_packet(packet_capsule, false);
	if (packet == NULL) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS;
	csp_sendto(prio, dest, dport, src_port, opts, packet);
	Py_END_ALLOW_THREADS;

	PyCapsule_SetPointer(packet_capsule, &CSP_POINTER_HAS_BEEN_FREED);

	Py_RETURN_NONE;
}

static PyObject * pycsp_recvfrom(PyObject * self, PyObject * args) {
	PyObject * socket_capsule;
	uint32_t timeout = 500;
	if (!PyArg_ParseTuple(args, "O|I", &socket_capsule, &timeout)) {
		return NULL;
	}
	csp_socket_t * socket = get_obj_as_socket(socket_capsule, false);
	if (socket == NULL) {
		return NULL;
	}
	csp_packet_t * packet = NULL;
	Py_BEGIN_ALLOW_THREADS;
	packet = csp_recvfrom(socket, timeout);
	Py_END_ALLOW_THREADS;
	if (packet == NULL) {
		Py_RETURN_NONE;
	}

	return PyCapsule_New(packet, PACKET_CAPSULE, pycsp_free_csp_buffer);
}

static PyObject * pycsp_sendto_reply(PyObject * self, PyObject * args) {
	PyObject * request_packet_capsule;
	PyObject * reply_packet_capsule;
	uint32_t opts = CSP_O_NONE;
	if (!PyArg_ParseTuple(args, "OO|I", &request_packet_capsule, &reply_packet_capsule, &opts)) {
		return NULL;  // TypeError is thrown
	}
	csp_packet_t * request = get_obj_as_packet(request_packet_capsule, false);
	if (request == NULL) {
		return NULL;
	}
	csp_packet_t * reply = get_obj_as_packet(reply_packet_capsule, false);
	if (reply == NULL) {
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS;
	csp_sendto_reply(request, reply, opts);
	Py_END_ALLOW_THREADS;

	PyCapsule_SetPointer(reply_packet_capsule, &CSP_POINTER_HAS_BEEN_FREED);
	Py_RETURN_NONE;
}

static PyObject * pycsp_connect(PyObject * self, PyObject * args) {
	uint8_t prio;
	uint16_t dest;
	uint8_t dport;
	uint32_t timeout;
	uint32_t opts;
	if (!PyArg_ParseTuple(args, "bHbII", &prio, &dest, &dport, &timeout, &opts)) {
		return NULL;  // TypeError is thrown
	}

	csp_conn_t * conn;
	Py_BEGIN_ALLOW_THREADS;
	conn = csp_connect(prio, dest, dport, timeout, opts);
	Py_END_ALLOW_THREADS;
	if (conn == NULL) {
		return PyErr_Error("csp_connect() timeout or failed", CSP_ERR_TIMEDOUT);
	}

	return PyCapsule_New(conn, CONNECTION_CAPSULE, pycsp_free_csp_conn);
}

static PyObject * pycsp_close(PyObject * self, PyObject * conn_capsule) {
	csp_conn_t * conn = get_obj_as_conn(conn_capsule, true);
	if (conn) {
		csp_close(conn);
		PyCapsule_SetPointer(conn_capsule, &CSP_POINTER_HAS_BEEN_FREED);
	}

	return Py_BuildValue("i", CSP_ERR_NONE);
}

static PyObject * pycsp_conn_dport(PyObject * self, PyObject * conn_capsule) {
	csp_conn_t * conn = get_obj_as_conn(conn_capsule, false);
	if (conn == NULL) {
		return NULL;  // TypeError is thrown
	}
	return Py_BuildValue("i", csp_conn_dport(conn));
}

static PyObject * pycsp_conn_sport(PyObject * self, PyObject * conn_capsule) {
	csp_conn_t * conn = get_obj_as_conn(conn_capsule, false);
	if (conn == NULL) {
		return NULL;  // TypeError is thrown
	}
	return Py_BuildValue("i", csp_conn_sport(conn));
}

static PyObject * pycsp_conn_dst(PyObject * self, PyObject * conn_capsule) {
	csp_conn_t * conn = get_obj_as_conn(conn_capsule, false);
	if (conn == NULL) {
		return NULL;  // TypeError is thrown
	}
	return Py_BuildValue("i", csp_conn_dst(conn));
}

static PyObject * pycsp_conn_src(PyObject * self, PyObject * conn_capsule) {
	csp_conn_t * conn = get_obj_as_conn(conn_capsule, false);
	if (conn == NULL) {
		return NULL;  // TypeError is thrown
	}
	return Py_BuildValue("i", csp_conn_src(conn));
}

static PyObject * pycsp_listen(PyObject * self, PyObject * args) {
	PyObject * socket_capsule;
	unsigned long conn_queue_len = 10;
	if (!PyArg_ParseTuple(args, "O|k", &socket_capsule, &conn_queue_len)) {
		return NULL;  // TypeError is thrown
	}

	csp_socket_t * sock = get_obj_as_socket(socket_capsule, false);
	if (sock == NULL) {
		return NULL;  // TypeError is thrown
	}

	int res = csp_listen(sock, conn_queue_len);
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_listen()", res);
	}

	Py_RETURN_NONE;
}

static PyObject * pycsp_bind(PyObject * self, PyObject * args) {
	PyObject * socket_capsule;
	uint8_t port;
	if (!PyArg_ParseTuple(args, "Ob", &socket_capsule, &port)) {
		return NULL;  // TypeError is thrown
	}

	csp_socket_t * sock = get_obj_as_socket(socket_capsule, false);
	if (sock == NULL) {
		return NULL;  // TypeError is thrown
	}

	int res = csp_bind(sock, port);
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_bind()", res);
	}

	Py_RETURN_NONE;
}

static void * csp_task_router(void * param) {

	/* Here there be routing */
	while (1) {
		csp_route_work();
	}

	return NULL;
}

static int csp_route_start_task(void) {

	pthread_attr_t attributes;
	pthread_t handle;
	int ret;

	if (pthread_attr_init(&attributes) != 0) {
		return CSP_ERR_NOMEM;
	}
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);  // no need to join with thread to free its resources

	ret = pthread_create(&handle, &attributes, csp_task_router, NULL);
	pthread_attr_destroy(&attributes);

	if (ret != 0) {
		printf("Failed to start router task, error: %d", ret);
		return ret;
	}

	return CSP_ERR_NONE;
}

static PyObject * pycsp_route_start_task(PyObject * self, PyObject * args) {
	int res = csp_route_start_task();
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_route_start_task()", res);
	}

	Py_RETURN_NONE;
}

static PyObject * pycsp_ping(PyObject * self, PyObject * args) {
	uint16_t node;
	uint32_t timeout = 1000;
	unsigned int size = 10;
	uint8_t conn_options = CSP_O_NONE;
	if (!PyArg_ParseTuple(args, "H|IIb", &node, &timeout, &size, &conn_options)) {
		return NULL;  // TypeError is thrown
	}

	int res;
	Py_BEGIN_ALLOW_THREADS;
	res = csp_ping(node, timeout, size, conn_options);
	Py_END_ALLOW_THREADS;

	if (res < 0) {
		return PyErr_Error("csp_ping()", res);
	}

	return Py_BuildValue("i", res);
}

static PyObject * pycsp_reboot(PyObject * self, PyObject * args) {
	uint16_t node;
	if (!PyArg_ParseTuple(args, "H", &node)) {
		return NULL;  // TypeError is thrown
	}

	csp_reboot(node);
	Py_RETURN_NONE;
}

static PyObject * pycsp_shutdown(PyObject * self, PyObject * args) {
	uint16_t node;
	if (!PyArg_ParseTuple(args, "H", &node)) {
		return NULL;  // TypeError is thrown
	}

	csp_shutdown(node);
	Py_RETURN_NONE;
}

static PyObject * pycsp_rdp_set_opt(PyObject * self, PyObject * args) {
	unsigned int window_size;
	unsigned int conn_timeout_ms;
	unsigned int packet_timeout_ms;
	unsigned int delayed_acks;
	unsigned int ack_timeout;
	unsigned int ack_delay_count;
	if (!PyArg_ParseTuple(args, "IIIIII", &window_size, &conn_timeout_ms,
						  &packet_timeout_ms, &delayed_acks,
						  &ack_timeout, &ack_delay_count)) {
		return NULL;  // TypeError is thrown
	}
#if (CSP_USE_RDP)
	csp_rdp_set_opt(window_size, conn_timeout_ms, packet_timeout_ms,
					delayed_acks, ack_timeout, ack_delay_count);
#endif
	Py_RETURN_NONE;
}

static PyObject * pycsp_rdp_get_opt(PyObject * self, PyObject * args) {

	unsigned int window_size = 0;
	unsigned int conn_timeout_ms = 0;
	unsigned int packet_timeout_ms = 0;
	unsigned int delayed_acks = 0;
	unsigned int ack_timeout = 0;
	unsigned int ack_delay_count = 0;
#if (CSP_USE_RDP)
	csp_rdp_get_opt(&window_size,
					&conn_timeout_ms,
					&packet_timeout_ms,
					&delayed_acks,
					&ack_timeout,
					&ack_delay_count);
#endif
	return Py_BuildValue("IIIIII",
						 window_size,
						 conn_timeout_ms,
						 packet_timeout_ms,
						 delayed_acks,
						 ack_timeout,
						 ack_delay_count);
}

#if CSP_USE_RTABLE

static PyObject * pycsp_rtable_set(PyObject * self, PyObject * args) {
	uint16_t node;
	int mask;
	char * interface_name;
	uint16_t via = CSP_NO_VIA_ADDRESS;
	if (!PyArg_ParseTuple(args, "His|H", &node, &mask, &interface_name, &via)) {
		return NULL;  // TypeError is thrown
	}

	int res = csp_rtable_set(node, mask, csp_iflist_get_by_name(interface_name), via);
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_rtable_set()", res);
	}

	Py_RETURN_NONE;
}

static PyObject * pycsp_rtable_clear(PyObject * self, PyObject * args) {
	csp_rtable_clear();
	Py_RETURN_NONE;
}

static PyObject * pycsp_rtable_check(PyObject * self, PyObject * args) {
	char * buffer;
	if (!PyArg_ParseTuple(args, "s", &buffer)) {
		return NULL;  // TypeError is thrown
	}

	int res = csp_rtable_check(buffer);
	if (res <= 0) {
		return PyErr_Error("csp_rtable_check()", res);
	}
	return Py_BuildValue("i", res);
}

static PyObject * pycsp_rtable_load(PyObject * self, PyObject * args) {
	char * buffer;
	if (!PyArg_ParseTuple(args, "s", &buffer)) {
		return NULL;  // TypeError is thrown
	}

	int res = csp_rtable_load(buffer);
	if (res <= 0) {
		return PyErr_Error("csp_rtable_load()", res);
	}
	return Py_BuildValue("i", res);
}

static PyObject * pycsp_print_routes(PyObject * self, PyObject * args) {
	csp_rtable_print();
	Py_RETURN_NONE;
}

#endif

static PyObject * pycsp_buffer_get(PyObject * self, PyObject * args) {
	void * packet = csp_buffer_get(0);
	if (packet == NULL) {
		return PyErr_Error("csp_buffer_get() - no free buffers or data overrun", CSP_ERR_NOMEM);
	}

	return PyCapsule_New(packet, PACKET_CAPSULE, pycsp_free_csp_buffer);
}

static PyObject * pycsp_buffer_free(PyObject * self, PyObject * args) {
	PyObject * packet_capsule;
	if (!PyArg_ParseTuple(args, "O", &packet_capsule)) {
		return NULL;  // TypeError is thrown
	}

	csp_packet_t * packet = get_obj_as_packet(packet_capsule, true);
	if (packet) {
		csp_buffer_free(packet);
	}
	PyCapsule_SetPointer(packet_capsule, &CSP_POINTER_HAS_BEEN_FREED);
	Py_RETURN_NONE;
}

static PyObject * pycsp_buffer_remaining(PyObject * self, PyObject * args) {
	return Py_BuildValue("i", csp_buffer_remaining());
}

static PyObject * pycsp_cmp_ident(PyObject * self, PyObject * args) {
	uint16_t node;
	uint32_t timeout = 1000;
	if (!PyArg_ParseTuple(args, "H|I", &node, &timeout)) {
		return NULL;  // TypeError is thrown
	}

	struct csp_cmp_message msg;
	memset(&msg, 0, sizeof(msg));
	int res;
	Py_BEGIN_ALLOW_THREADS;
	res = csp_cmp_ident(node, timeout, &msg);
	Py_END_ALLOW_THREADS;
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_cmp_ident()", res);
	}
	return Py_BuildValue("isssss",
						 res,
						 msg.ident.hostname,
						 msg.ident.model,
						 msg.ident.revision,
						 msg.ident.date,
						 msg.ident.time);
}

static PyObject * pycsp_cmp_route_set(PyObject * self, PyObject * args) {
	uint16_t node;
	uint32_t timeout = 1000;
	uint8_t addr;
	uint8_t via;
	char * ifstr;
	if (!PyArg_ParseTuple(args, "HIbbs", &node, &timeout, &addr, &via, &ifstr)) {
		return NULL;  // TypeError is thrown
	}

	struct csp_cmp_message msg;
	memset(&msg, 0, sizeof(msg));
	msg.route_set_v2.dest_node = htobe16(addr);
	msg.route_set_v2.next_hop_via = htobe16(via);
	strncpy(msg.route_set_v2.interface, ifstr, sizeof(msg.route_set_v2.interface) - 1);

	int res;
	Py_BEGIN_ALLOW_THREADS;
	res = csp_cmp_route_set_v2(node, timeout, &msg);
	Py_END_ALLOW_THREADS;
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_cmp_route_set()", res);
	}

	Py_RETURN_NONE;
}

static PyObject * pycsp_cmp_peek(PyObject * self, PyObject * args) {
	uint16_t node;
	uint32_t timeout;
	uint32_t addr;
	uint8_t len;
	Py_buffer outbuf;
	if (!PyArg_ParseTuple(args, "HIIbw*", &node, &timeout, &addr, &len, &outbuf)) {
		Py_RETURN_NONE;
	}

	if ((len > CSP_CMP_PEEK_MAX_LEN) || (len > outbuf.len)) {
		return PyErr_Error("csp_cmp_peek() - exceeding max size", CSP_ERR_INVAL);
	}

	struct csp_cmp_message msg;
	memset(&msg, 0, sizeof(msg));
	msg.peek.addr = htobe32(addr);
	msg.peek.len = len;

	int res;
	Py_BEGIN_ALLOW_THREADS;
	res = csp_cmp_peek(node, timeout, &msg);
	Py_END_ALLOW_THREADS;
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_cmp_peek()", res);
	}
	memcpy(outbuf.buf, msg.peek.data, len);
	outbuf.len = len;

	Py_RETURN_NONE;
}

static PyObject * pycsp_cmp_poke(PyObject * self, PyObject * args) {
	uint16_t node;
	uint32_t timeout;
	uint8_t len;
	uint32_t addr;
	Py_buffer inbuf;

	if (!PyArg_ParseTuple(args, "HIIbw*", &node, &timeout, &addr, &len, &inbuf)) {
		Py_RETURN_NONE;
	}

	if (len > CSP_CMP_POKE_MAX_LEN) {
		return PyErr_Error("csp_cmp_poke() - exceeding max size", CSP_ERR_INVAL);
	}
	struct csp_cmp_message msg;
	msg.poke.addr = htobe32(addr);
	msg.poke.len = len;
	memcpy(msg.poke.data, inbuf.buf, len);

	int res;
	Py_BEGIN_ALLOW_THREADS;
	res = csp_cmp_poke(node, timeout, &msg);
	Py_END_ALLOW_THREADS;
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_cmp_poke()", res);
	}

	Py_RETURN_NONE;
}

static PyObject * pycsp_cmp_clock_set(PyObject * self, PyObject * args) {
	uint16_t node;
	uint32_t sec;
	uint32_t nsec;
	uint32_t timeout = 1000;
	if (!PyArg_ParseTuple(args, "HII|I", &node, &sec, &nsec, &timeout)) {
		Py_RETURN_NONE;
	}

	if (sec == 0) {
		return PyErr_Error("csp_cmp_clock(set) - seconds are 0", CSP_ERR_INVAL);
	}

	struct csp_cmp_message msg;
	memset(&msg, 0, sizeof(msg));
	msg.clock.tv_sec = htobe32(sec);
	msg.clock.tv_nsec = htobe32(nsec);

	int res;
	Py_BEGIN_ALLOW_THREADS;
	res = csp_cmp_clock(node, timeout, &msg);
	Py_END_ALLOW_THREADS;
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_cmp_clock(set)", res);
	}

	Py_RETURN_NONE;
}

static PyObject * pycsp_cmp_clock_get(PyObject * self, PyObject * args) {
	uint16_t node;
	uint32_t timeout = 1000;
	if (!PyArg_ParseTuple(args, "H|I", &node, &timeout)) {
		Py_RETURN_NONE;
	}

	struct csp_cmp_message msg;
	memset(&msg, 0, sizeof(msg));

	int res;
	Py_BEGIN_ALLOW_THREADS;
	res = csp_cmp_clock(node, timeout, &msg);
	Py_END_ALLOW_THREADS;
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_cmp_clock(get)", res);
	}

	return Py_BuildValue("II",
						 be32toh(msg.clock.tv_sec),
						 be32toh(msg.clock.tv_nsec));
}

#if CSP_HAVE_LIBZMQ
static PyObject * pycsp_zmqhub_init(PyObject * self, PyObject * args) {
	uint16_t addr;
	char * host;
	if (!PyArg_ParseTuple(args, "Hs", &addr, &host)) {
		return NULL;  // TypeError is thrown
	}

	int res = csp_zmqhub_init(addr, host, 0, NULL);
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_zmqhub_init()", res);
	}

	Py_RETURN_NONE;
}
#endif /* CSP_HAVE_LIBZMQ */

static PyObject * pycsp_can_socketcan_init(PyObject * self, PyObject * args) {
	char * ifc;
	int bitrate = 1000000;
	int promisc = 0;
	uint16_t addr = 0;
	if (!PyArg_ParseTuple(args, "s|Hii", &ifc, &addr, &bitrate, &promisc)) {
		return NULL;
	}

	int res = csp_can_socketcan_open_and_add_interface(ifc, CSP_IF_CAN_DEFAULT_NAME, addr, bitrate, promisc, NULL);
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_can_socketcan_open_and_add_interface()", res);
	}

	Py_RETURN_NONE;
}

static PyObject * pycsp_kiss_init(PyObject * self, PyObject * args) {
	char * device;
	uint32_t baudrate = 500000;
	uint32_t mtu = 512;
	uint16_t addr;
	const char * if_name = CSP_IF_KISS_DEFAULT_NAME;
	if (!PyArg_ParseTuple(args, "sH|IIs", &device, &addr, &baudrate, &mtu, &if_name)) {
		return NULL;  // TypeError is thrown
	}

	csp_usart_conf_t conf = {.device = device, .baudrate = baudrate};
	int res = csp_usart_open_and_add_kiss_interface(&conf, if_name, addr, NULL);
	if (res != CSP_ERR_NONE) {
		return PyErr_Error("csp_usart_open_and_add_kiss_interface()", res);
	}

	Py_RETURN_NONE;
}

static PyObject * pycsp_packet_set_data(PyObject * self, PyObject * args) {
	PyObject * packet_capsule;
	Py_buffer data;
	if (!PyArg_ParseTuple(args, "Oy*", &packet_capsule, &data)) {
		return NULL;  // TypeError is thrown
	}

	csp_packet_t * packet = get_obj_as_packet(packet_capsule, false);
	if (packet == NULL) {
		return NULL;  // TypeError is thrown
	}
	if (data.len > (int)sizeof(packet->data)) {
		return PyErr_Error("packet_set_data() - exceeding data size", CSP_ERR_INVAL);
	}

	memcpy(packet->data, data.buf, data.len);
	packet->length = data.len;

	Py_RETURN_NONE;
}

static PyObject * pycsp_packet_get_data(PyObject * self, PyObject * packet_capsule) {
	csp_packet_t * packet = get_obj_as_packet(packet_capsule, false);
	if (packet == NULL) {
		return NULL;  // TypeError is thrown
	}
	return Py_BuildValue("y#", packet->data, (size_t)packet->length);
}

static PyObject * pycsp_packet_get_length(PyObject * self, PyObject * packet_capsule) {
	csp_packet_t * packet = get_obj_as_packet(packet_capsule, false);
	if (packet == NULL) {
		return NULL;  // TypeError is thrown
	}
	return Py_BuildValue("H", packet->length);
}

static PyObject * pycsp_print_connections(PyObject * self, PyObject * args) {
	csp_conn_print_table();
	Py_RETURN_NONE;
}

static PyObject * pycsp_print_interfaces(PyObject * self, PyObject * args) {
	csp_iflist_print();
	Py_RETURN_NONE;
}

static PyMethodDef methods[] = {

	/* csp/csp.h */
	{"service_handler", pycsp_service_handler, METH_VARARGS, ""},
	{"init", pycsp_init, METH_VARARGS, ""},
	{"get_hostname", pycsp_get_hostname, METH_NOARGS, ""},
	{"get_model", pycsp_get_model, METH_NOARGS, ""},
	{"get_revision", pycsp_get_revision, METH_NOARGS, ""},
	{"socket", pycsp_socket, METH_VARARGS, ""},
	{"accept", pycsp_accept, METH_VARARGS, ""},
	{"read", pycsp_read, METH_VARARGS, ""},
	{"send", pycsp_send, METH_VARARGS, ""},
	//{"sfp_send",            pycsp_sfp_send,            METH_VARARGS, ""},
	//{"sfp_recv",            pycsp_sfp_recv,            METH_VARARGS, ""},
	{"transaction", pycsp_transaction, METH_VARARGS, ""},
	{"sendto_reply", pycsp_sendto_reply, METH_VARARGS, ""},
	{"sendto", pycsp_sendto, METH_VARARGS, ""},
	{"recvfrom", pycsp_recvfrom, METH_VARARGS, ""},
	{"connect", pycsp_connect, METH_VARARGS, ""},
	{"close", pycsp_close, METH_O, ""},
	{"conn_dport", pycsp_conn_dport, METH_O, ""},
	{"conn_sport", pycsp_conn_sport, METH_O, ""},
	{"conn_dst", pycsp_conn_dst, METH_O, ""},
	{"conn_src", pycsp_conn_src, METH_O, ""},
	{"listen", pycsp_listen, METH_VARARGS, ""},
	{"bind", pycsp_bind, METH_VARARGS, ""},
	{"route_start_task", pycsp_route_start_task, METH_NOARGS, ""},
	{"ping", pycsp_ping, METH_VARARGS, ""},
	{"reboot", pycsp_reboot, METH_VARARGS, ""},
	{"shutdown", pycsp_shutdown, METH_VARARGS, ""},
	{"rdp_set_opt", pycsp_rdp_set_opt, METH_VARARGS, ""},
	{"rdp_get_opt", pycsp_rdp_get_opt, METH_NOARGS, ""},

#if CSP_USE_RTABLE
	/* csp/csp_rtable.h */
	{"rtable_set", pycsp_rtable_set, METH_VARARGS, ""},
	{"rtable_clear", pycsp_rtable_clear, METH_NOARGS, ""},
	{"rtable_check", pycsp_rtable_check, METH_VARARGS, ""},
	{"rtable_load", pycsp_rtable_load, METH_VARARGS, ""},
	{"print_routes", pycsp_print_routes, METH_NOARGS, ""},
#endif

	/* csp/csp_buffer.h */
	{"buffer_free", pycsp_buffer_free, METH_VARARGS, ""},
	{"buffer_get", pycsp_buffer_get, METH_VARARGS, ""},
	{"buffer_remaining", pycsp_buffer_remaining, METH_NOARGS, ""},

	/* csp/csp_cmp.h */
	{"cmp_ident", pycsp_cmp_ident, METH_VARARGS, ""},
	{"cmp_route_set", pycsp_cmp_route_set, METH_VARARGS, ""},
	{"cmp_peek", pycsp_cmp_peek, METH_VARARGS, ""},
	{"cmp_poke", pycsp_cmp_poke, METH_VARARGS, ""},
	{"cmp_clock_set", pycsp_cmp_clock_set, METH_VARARGS, ""},
	{"cmp_clock_get", pycsp_cmp_clock_get, METH_VARARGS, ""},

#if CSP_HAVE_LIBZMQ
	/* csp/interfaces/csp_if_zmqhub.h */
	{"zmqhub_init", pycsp_zmqhub_init, METH_VARARGS, ""},
#endif /* CSP_HAVE_LIBZMQ */
	{"kiss_init", pycsp_kiss_init, METH_VARARGS, ""},

	/* csp/drivers/can_socketcan.h */
	{"can_socketcan_init", pycsp_can_socketcan_init, METH_VARARGS, ""},

	/* helpers */
	{"packet_get_length", pycsp_packet_get_length, METH_O, ""},
	{"packet_get_data", pycsp_packet_get_data, METH_O, ""},
	{"packet_set_data", pycsp_packet_set_data, METH_VARARGS, ""},
	{"print_connections", pycsp_print_connections, METH_NOARGS, ""},
	{"print_interfaces", pycsp_print_interfaces, METH_NOARGS, ""},

	/* sentinel */
	{NULL, NULL, 0, NULL}};

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	"libcsp_py3",
	NULL,
	-1,
	methods,
	NULL,
	NULL,
	NULL,
	NULL};

PyMODINIT_FUNC PyInit_libcsp_py3(void) {

	PyObject * m = PyModule_Create(&moduledef);

	/* Exceptions */
	Error = PyErr_NewException((char *)"csp.Error", NULL, NULL);
	Py_INCREF(Error);

	/* Add exception object to your module */
	PyModule_AddObject(m, "Error", Error);

	/* RESERVED PORTS */
	PyModule_AddIntConstant(m, "CSP_CMP", CSP_CMP);
	PyModule_AddIntConstant(m, "CSP_PING", CSP_PING);
	PyModule_AddIntConstant(m, "CSP_PS", CSP_PS);
	PyModule_AddIntConstant(m, "CSP_MEMFREE", CSP_MEMFREE);
	PyModule_AddIntConstant(m, "CSP_REBOOT", CSP_REBOOT);
	PyModule_AddIntConstant(m, "CSP_BUF_FREE", CSP_BUF_FREE);
	PyModule_AddIntConstant(m, "CSP_UPTIME", CSP_UPTIME);
	PyModule_AddIntConstant(m, "CSP_ANY", CSP_ANY);

	/* PRIORITIES */
	PyModule_AddIntConstant(m, "CSP_PRIO_CRITICAL", CSP_PRIO_CRITICAL);
	PyModule_AddIntConstant(m, "CSP_PRIO_HIGH", CSP_PRIO_HIGH);
	PyModule_AddIntConstant(m, "CSP_PRIO_NORM", CSP_PRIO_NORM);
	PyModule_AddIntConstant(m, "CSP_PRIO_LOW", CSP_PRIO_LOW);

	/* FLAGS */
	PyModule_AddIntConstant(m, "CSP_FFRAG", CSP_FFRAG);
	PyModule_AddIntConstant(m, "CSP_FHMAC", CSP_FHMAC);
	PyModule_AddIntConstant(m, "CSP_FRDP", CSP_FRDP);
	PyModule_AddIntConstant(m, "CSP_FCRC32", CSP_FCRC32);

	/* SOCKET OPTIONS */
	PyModule_AddIntConstant(m, "CSP_SO_NONE", CSP_SO_NONE);
	PyModule_AddIntConstant(m, "CSP_SO_RDPREQ", CSP_SO_RDPREQ);
	PyModule_AddIntConstant(m, "CSP_SO_RDPPROHIB", CSP_SO_RDPPROHIB);
	PyModule_AddIntConstant(m, "CSP_SO_HMACREQ", CSP_SO_HMACREQ);
	PyModule_AddIntConstant(m, "CSP_SO_HMACPROHIB", CSP_SO_HMACPROHIB);
	PyModule_AddIntConstant(m, "CSP_SO_CRC32REQ", CSP_SO_CRC32REQ);
	PyModule_AddIntConstant(m, "CSP_SO_CRC32PROHIB", CSP_SO_CRC32PROHIB);
	PyModule_AddIntConstant(m, "CSP_SO_CONN_LESS", CSP_SO_CONN_LESS);

	/* CONNECT OPTIONS */
	PyModule_AddIntConstant(m, "CSP_O_NONE", CSP_O_NONE);
	PyModule_AddIntConstant(m, "CSP_O_RDP", CSP_O_RDP);
	PyModule_AddIntConstant(m, "CSP_O_NORDP", CSP_O_NORDP);
	PyModule_AddIntConstant(m, "CSP_O_HMAC", CSP_O_HMAC);
	PyModule_AddIntConstant(m, "CSP_O_NOHMAC", CSP_O_NOHMAC);
	PyModule_AddIntConstant(m, "CSP_O_CRC32", CSP_O_CRC32);
	PyModule_AddIntConstant(m, "CSP_O_NOCRC32", CSP_O_NOCRC32);

	/* csp/csp_error.h */
	PyModule_AddIntConstant(m, "CSP_ERR_NONE", CSP_ERR_NONE);
	PyModule_AddIntConstant(m, "CSP_ERR_NOMEM", CSP_ERR_NOMEM);
	PyModule_AddIntConstant(m, "CSP_ERR_INVAL", CSP_ERR_INVAL);
	PyModule_AddIntConstant(m, "CSP_ERR_TIMEDOUT", CSP_ERR_TIMEDOUT);
	PyModule_AddIntConstant(m, "CSP_ERR_USED", CSP_ERR_USED);
	PyModule_AddIntConstant(m, "CSP_ERR_NOTSUP", CSP_ERR_NOTSUP);
	PyModule_AddIntConstant(m, "CSP_ERR_BUSY", CSP_ERR_BUSY);
	PyModule_AddIntConstant(m, "CSP_ERR_ALREADY", CSP_ERR_ALREADY);
	PyModule_AddIntConstant(m, "CSP_ERR_RESET", CSP_ERR_RESET);
	PyModule_AddIntConstant(m, "CSP_ERR_NOBUFS", CSP_ERR_NOBUFS);
	PyModule_AddIntConstant(m, "CSP_ERR_TX", CSP_ERR_TX);
	PyModule_AddIntConstant(m, "CSP_ERR_DRIVER", CSP_ERR_DRIVER);
	PyModule_AddIntConstant(m, "CSP_ERR_AGAIN", CSP_ERR_AGAIN);
	PyModule_AddIntConstant(m, "CSP_ERR_NOSYS", CSP_ERR_NOSYS);
	PyModule_AddIntConstant(m, "CSP_ERR_HMAC", CSP_ERR_HMAC);
	PyModule_AddIntConstant(m, "CSP_ERR_CRC32", CSP_ERR_CRC32);
	PyModule_AddIntConstant(m, "CSP_ERR_SFP", CSP_ERR_SFP);

	/* misc */
	PyModule_AddIntConstant(m, "CSP_NO_VIA_ADDRESS", CSP_NO_VIA_ADDRESS);
	PyModule_AddIntConstant(m, "CSP_MAX_TIMEOUT", CSP_MAX_TIMEOUT);

	return m;
}
