/*
wrk -t16 -c512 -d60s http://127.0.0.1:8083

```sh
Running 1m test @ http://127.0.0.1:8083
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.56ms    1.92ms  29.55ms   84.83%
    Req/Sec    25.03k     3.12k   43.96k    71.10%
  23926297 requests in 1.00m, 2.74GB read
Requests/sec: 398130.30
Transfer/sec:     46.70MB
```
*/

module main

import sync
import os

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdio.h>
#include <netinet/in.h>

// https://man7.org/linux/man-pages/man2/socket.2.html
pub enum SocketType {
	tcp       = C.SOCK_STREAM    // used for TCP sockets
	udp       = C.SOCK_DGRAM     // used for UDP sockets
	seqpacket = C.SOCK_SEQPACKET // used for sequenced-packet sockets
	raw       = C.SOCK_RAW       // used for raw sockets
	rdm       = C.SOCK_RDM       // used for reliable datagram sockets
	// Since Linux 2.6.27, the type argument serves a second purpose: in
	// addition to specifying a socket type, it may include the bitwise
	// OR of any of the following values, to modify the behavior of
	// socket():
	nonblock = C.SOCK_NONBLOCK // set the O_NONBLOCK file status flag on the new open file description
	cloexec  = C.SOCK_CLOEXEC  // set the close-on-exec (FD_CLOEXEC) flag on the new file descriptor
}

// AddrFamily are the available address families/domain
pub enum AddrFamily as u16 {
	unix      = C.AF_UNIX      // Local communication
	local     = C.AF_LOCAL     // Synonym for AF_UNIX
	ip        = C.AF_INET      // IPv4 Internet protocols
	ax_25     = C.AF_AX25      // Amateur radio AX.25
	ipx       = C.AF_IPX       // Novell IPX is a discontinued proprietary suite of network protocols
	appletalk = C.AF_APPLETALK // AppleTalk is a discontinued proprietary suite of networking protocols developed by Apple Computer for their Macintosh computers.
	x25       = C.AF_X25       // ITU-T X.25 / ISO-8208 protocol suite
	ip6       = C.AF_INET6     // IPv6 Internet protocols
	decnet    = C.AF_DECnet    // DECnet is a discontinued proprietary suite of network protocols developed by Digital Equipment Corporation
	key       = C.AF_KEY       // Internal key-management function
	netlink   = C.AF_NETLINK   // Kernel user interface device
	packet    = C.AF_PACKET    // Low-level packet interface
	rds       = C.AF_RDS       // Reliable Datagram Sockets
	pppox     = C.AF_PPPOX     // Generic PPP transport layer
	llc       = C.AF_LLC       // Logical Link Control
	ib        = C.AF_IB        // InfiniBand communication
	mpls      = C.AF_MPLS      // Multiprotocol Label Switching
	can       = C.AF_CAN       // Controller Area Network
	tipc      = C.AF_TIPC      // TIPC (Transparent Inter Process Communication) is a protocol that is specially designed for intra-cluster communication.
	bluetooth = C.AF_BLUETOOTH // Bluetooth sockets
	alg       = C.AF_ALG       // Interface to kernel crypto API
	vsock     = C.AF_VSOCK     // VSOCK (formerly known as VMCI, Virtual Machine Communication Interface) is a communications interface used for communication between virtual machines and the host operating system.
	kcm       = C.AF_KCM       // Kernel Connection Multiplexor
	xdp       = C.AF_XDP       // XDP (eXpress Data Path) is a high performance, programmable packet processing framework that runs in the Linux kernel.
}

fn C.socket(__domain int, __type int, __protocol int) int

fn C.htons(__hostshort u16) u16

fn C.bind(sockfd int, addr &Addr, addrlen u32) int

fn C.listen(__fd int, __n int) int

fn C.accept(sockfd int, address &C.sockaddr_in, addrlen &u32) int

fn C.setsockopt(__fd int, __level int, __optname int, __optval voidptr, __optlen u32) int

fn C.recv(__fd int, __buf voidptr, __n usize, __flags int) int

fn C.send(__fd int, __buf voidptr, __n usize, __flags int) int

fn C.epoll_create1(__flags int) int

fn C.epoll_ctl(__epfd int, __op int, __fd int, __event &C.epoll_event) int

fn C.epoll_wait(__epfd int, __events &C.epoll_event, __maxevents int, __timeout int) int

fn C.fcntl(fd int, cmd int, arg int) int

fn C.close(fd int) int

fn C.pthread_cancel(_thread thread) int

fn C.pthread_join(_thread thread, retval &voidptr) int

fn C.perror(s &u8) voidptr

struct Server {
mut:
	server_socket int
	epoll_fd      int
	lock_flag     sync.Mutex
	has_clients   int
	threads       [max_thread_pool_size]thread
}

// pub struct C.sockaddr_in {
// mut:
// 	sin_family AddrFamily
// 	sin_port   u16
// 	sin_addr   int
// 	sin_zero   [8]u8
// }

const sock_stream = C.SOCK_STREAM
const sock_nonblock = C.SOCK_NONBLOCK

const max_unix_path = 108

const port = 8082
const buffer_size = 140
const response_body = '{"message": "Hello, world!"}'
const max_connection_size = 512
const max_thread_pool_size = 16
const response = 'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ${response_body.len}\r\nConnection: keep-alive\r\n\r\n${response_body}'.bytes()

@[typedef]
union C.epoll_data {
mut:
	ptr voidptr
	fd  int
	u32 u32
	u64 u64
}

pub struct C.epoll_event {
	events u32
	data   C.epoll_data
}

struct In_addr {
	// s_addr In_addr_t
	// s_addr u32
	s_addr int
}

enum In_port_t as u16 {
	ipproto_hopopts  = 0
	ipproto_routing  = 43
	ipproto_fragment = 44
	ipproto_icmpv6   = 58
	ipproto_none     = 59
	ipproto_dstopts  = 60
	ipproto_mh       = 135
}

// type In_port_t = u16

// struct In6_addr {
// 	__in6_u Union (unnamed union at /usr/include/netinet/in.h
// }
struct Sockaddr_in {
	// sin_family Sa_family_t
	sin_family u16
	// sin_port   In_port_t
	sin_port u16
	sin_addr In_addr
	sin_zero [8]u8
}

@[_pack: '1']
pub struct Ip6 {
	port      u16
	flow_info u32
	addr      [16]u8
	scope_id  u32
}

@[_pack: '1']
pub struct Ip {
	port u16
	addr [4]u8
	// Pad to size so that socket functions
	// dont complain to us (see  in.h and bind())
	// TODO(emily): I would really like to use
	// some constant calculations here
	// so that this doesnt have to be hardcoded
	sin_pad [8]u8
}

pub struct Unix {
	path [max_unix_path]u8
}

union AddrData {
	Unix
	Ip
	Ip6
}

@[_pack: '1']
pub struct Addr {
pub:
	f    u16
	addr AddrData
}

fn set_blocking(fd int, blocking bool) {
	flags := C.fcntl(fd, C.F_GETFL, 0)
	if flags == -1 {
		return
	}
	if blocking {
		C.fcntl(fd, C.F_SETFL, flags & ~C.O_NONBLOCK)
	} else {
		C.fcntl(fd, C.F_SETFL, flags | C.O_NONBLOCK)
	}
}

fn create_server_socket() int {
	server_fd := C.socket(int(AddrFamily.ip), C.SOCK_STREAM | C.SOCK_NONBLOCK, 0)
	if server_fd < 0 {
		eprintln(@LOCATION)
		C.perror('Socket creation failed'.str)
		return -1
	}

	// Enable SO_REUSEPORT
	opt := 1
	if C.setsockopt(server_fd, C.SOL_SOCKET, C.SO_REUSEPORT, &opt, sizeof(opt)) < 0 {
		eprintln(@LOCATION)
		C.perror('setsockopt SO_REUSEPORT failed'.str)
		C.close(server_fd)
		return -1
	}

	server_addr := Sockaddr_in{
		sin_family: u16(AddrFamily.ip)
		sin_port:   C.htons(port)
		sin_addr:   In_addr{C.INADDR_ANY}
		sin_zero:   [8]u8{}
	}

	if C.bind(server_fd, voidptr(&server_addr), sizeof(server_addr)) < 0 {
		eprintln(@LOCATION)
		C.perror('Bind failed'.str)
		C.close(server_fd)
		return -1
	}
	if C.listen(server_fd, max_connection_size) < 0 {
		eprintln(@LOCATION)
		C.perror('Listen failed'.str)
		C.close(server_fd)
		return -1
	}
	return server_fd
}

fn add_fd_to_epoll(epoll_fd int, fd int, events u32) int {
	mut ev := C.epoll_event{
		events: events
	}
	ev.data.fd = fd
	if C.epoll_ctl(epoll_fd, C.EPOLL_CTL_ADD, fd, &ev) == -1 {
		eprintln(@LOCATION)
		C.perror('epoll_ctl'.str)
		return -1
	}
	return 0
}

// Function to remove a file descriptor from the epoll instance
fn remove_fd_from_epoll(epoll_fd int, fd int) {
	C.epoll_ctl(epoll_fd, C.EPOLL_CTL_DEL, fd, C.NULL)
}

// Updated handle_accept function to use atomic client count
fn handle_accept(server &Server) {
	for {
		client_fd := C.accept(server.server_socket, C.NULL, C.NULL)
		if client_fd < 0 {
			// Check for EAGAIN or EWOULDBLOCK, usually represented by errno 11.
			if C.errno == C.EAGAIN || C.errno == C.EWOULDBLOCK {
				break // No more incoming connections; exit loop.
			}

			eprintln(@LOCATION)
			C.perror('Accept failed'.str)
			return
		}

		// Set the client socket to non-blocking mode if accepted successfully
		set_blocking(client_fd, false)

		// Protect epoll operations with mutex
		unsafe {
			server.lock_flag.lock()
			if add_fd_to_epoll(server.epoll_fd, client_fd, u32(C.EPOLLIN | C.EPOLLET)) == -1 {
				C.close(client_fd)
			}

			server.lock_flag.unlock()
		}
	}
}

// Handle client closure with atomic client count decrement
fn handle_client_closure(server &Server, client_fd int) {
	unsafe {
		server.lock_flag.lock()

		remove_fd_from_epoll(client_fd, client_fd)

		server.lock_flag.unlock()
	}
}

// Process events from epoll with updated client count management
fn process_events(server &Server) {
	events := [max_connection_size]C.epoll_event{}
	num_events := C.epoll_wait(server.epoll_fd, &events[0], max_connection_size, -1)
	for i := 0; i < num_events; i++ {
		if events[i].events & (C.EPOLLHUP | C.EPOLLERR) != 0 {
			handle_client_closure(server, unsafe { events[i].data.fd })
			continue
		}

		if events[i].events & C.EPOLLIN != 0 {
			mut request_buffer := [buffer_size]u8{}
			// request_buffer := unsafe { C.malloc(buffer_size) }
			// if request_buffer == C.NULL {
			// 	C.perror('malloc failed'.str)
			// 	continue
			// }
			// unsafe { C.memset(request_buffer, 0, buffer_size) }
			bytes_read := C.recv(unsafe { events[i].data.fd }, &request_buffer[0], buffer_size - 1,
				0)

			if bytes_read > 0 {
				unsafe {
					request_buffer[bytes_read] = `\0` // Null-terminate the request_buffer
				}
				response_buffer := response
				C.send(unsafe { events[i].data.fd }, response_buffer.data, response_buffer.len,
					0)
				handle_client_closure(server, unsafe { events[i].data.fd })
				// C.free(response_buffer)
			} else if bytes_read == 0
				|| (bytes_read < 0 && C.errno != C.EAGAIN && C.errno != C.EWOULDBLOCK) {
				handle_client_closure(server, unsafe { events[i].data.fd })
			}
			// C.free(request_buffer)
		}
	}
}

// Worker thread function
fn worker_thread(server &Server) {
	for {
		process_events(server)
	}
	return
}

// Event loop for accepting clients
fn event_loop(server &Server) {
	for {
		handle_accept(server)
	}
}

fn cleanup(mut server Server, threads [max_thread_pool_size]thread) {
	println('Terminating...\n')
	server.lock_flag.lock()
	for i := 0; i < max_thread_pool_size; i++ {
		C.pthread_cancel(threads[i])
		C.pthread_join(threads[i], (unsafe { nil }))
	}
	server.lock_flag.unlock()

	C.close(server.server_socket)
	C.close(server.epoll_fd)
	println('Server terminated')
	exit(0)
}

fn main() {
	mut server := Server{}
	// os.signal_opt(.int, fn [mut server] (signum os.Signal) {
	// 	cleanup(mut server, server.threads)
	// })!
	// os.signal_opt(.term, fn [mut server] (signum os.Signal) {
	// 	cleanup(mut server, server.threads)
	// })!

	// unsafe { C.memset(&server, 0, sizeof(server)) }
	server.server_socket = create_server_socket()
	if server.server_socket < 0 {
		return
	}
	server.epoll_fd = C.epoll_create1(0)
	if server.epoll_fd < 0 {
		eprintln(@LOCATION)
		C.perror('epoll_create1 failed'.str)
		C.close(server.server_socket)
		return
	}

	server.lock_flag.lock()
	if add_fd_to_epoll(server.epoll_fd, server.server_socket, u32(C.EPOLLIN)) == -1 {
		C.close(server.server_socket)
		C.close(server.epoll_fd)
		server.lock_flag.unlock()
		return
	}

	server.lock_flag.unlock()
	server.lock_flag.init()
	// Start worker threads
	for i := 0; i < max_thread_pool_size; i++ {
		server.threads[i] = spawn worker_thread(&server)
	}

	println('Server started on http://localhost:${port}\n')

	// Start the event loop for accepting clients
	event_loop(&server)
}
