module main

import sync

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

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
pub enum AddrFamily {
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

fn C.socket(domain int, typ int, protocol int) int

fn C.htons(host u16) u16

fn C.bind(sockfd int, address &C.sockaddr_in, addrlen u32) int

fn C.listen(sockfd int, backlog int) int

fn C.accept(sockfd int, address &C.sockaddr_in, addrlen &u32) int

fn C.setsockopt(sockfd int, level int, optname int, optval &int, optlen u32) int

fn C.recv(sockfd int, buffer &u8, len u32, flags int) int

fn C.send(sockfd int, buffer &u8, len u32, flags int) int

fn C.epoll_create1(flags int) int

fn C.epoll_ctl(epfd int, op int, fd int, event &EpollEvent) int

fn C.epoll_wait(epfd int, events &EpollEvent, maxevents int, timeout int) int

fn C.fcntl(fd int, cmd int, arg int) int

fn C.close(fd int) int

struct Server {
mut:
	server_socket    int
	epoll_fd         int
	lock_flag        sync.Mutex
	client_count     u32
	has_clients      bool
	thread_pool_size u32 = u32(8)
	threads          [max_thread_pool_size]thread
}

pub struct C.sockaddr_in {
mut:
	sin_family AddrFamily
	sin_port   u16
	sin_addr   int
	sin_zero   [8]u8
}

const port = 8081
const buffer_size = 140
const response_body = '{ "message": "Hello, world!" }'
const max_connection_size = 512
const initial_thread_pool_size = 8
const max_thread_pool_size = 16
const response = 'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ${response_body.len}\r\nConnection: keep-alive\r\n\r\n${response_body}'

union Epoll_data_t {
	ptr voidptr
	fd  int // File descriptor
	u32 u32
	u64 u64
}

struct EpollEvent {
	events u32
	data   Epoll_data_t
}

// Function to set a socket to non-blocking mode
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
		eprintln('Socket creation failed')
		return -1
	}

	server_address := C.sockaddr_in{
		sin_family: AddrFamily.ip
		sin_port:   C.htons(port)
		sin_addr:   C.INADDR_ANY
	}

	if C.bind(server_fd, &server_address, sizeof(server_address)) < 0 {
		eprintln('Bind failed')
		C.close(server_fd)
		return -1
	}
	if C.listen(server_fd, max_connection_size) < 0 {
		eprintln('Listen failed')
		C.close(server_fd)
		return -1
	}
	return server_fd
}

// Function to add a file descriptor to the epoll instance
fn add_fd_to_epoll(epoll_fd int, fd int, events u32) int {
	ev := EpollEvent{
		events: events
		data:   Epoll_data_t{
			fd: fd
		}
	}
	if C.epoll_ctl(epoll_fd, C.EPOLL_CTL_ADD, fd, &ev) == -1 {
		eprintln('epoll_ctl: fd')
		return -1
	}
	return 0
}

// Function to remove a file descriptor from the epoll instance
fn remove_fd_from_epoll(epoll_fd int, fd int) {
	C.epoll_ctl(epoll_fd, C.EPOLL_CTL_DEL, fd, (unsafe { nil }))
}

fn handle_accept(server_fd int, epoll_fd int, lock_flag &sync.Mutex) {
	for {
		client_fd := C.accept(server_fd, unsafe { nil }, unsafe { nil })
		if client_fd < 0 {
			// Check for EAGAIN or EWOULDBLOCK, usually represented by errno 11.
			if C.errno == C.EAGAIN || C.errno == C.EWOULDBLOCK {
				break // No more incoming connections; exit loop.
			}
			eprintln('Accept failed with error code: ${C.errno}')
			return
		}

		// Set the client socket to non-blocking mode if accepted successfully
		set_blocking(client_fd, false)

		// Protect epoll operations with mutex
		lock {
			if add_fd_to_epoll(epoll_fd, client_fd, u32(C.EPOLLIN | C.EPOLLET)) == -1 {
				C.close(client_fd) // Close client socket if adding to epoll fails
			}
		}
	}
}


// Function to handle client closure
fn handle_client_closure(client_fd int, epoll_fd int, lock_flag &sync.Mutex) {
	// lock_flag.lock()
	remove_fd_from_epoll(epoll_fd, client_fd)
	// lock_flag.unlock()
	C.close(client_fd)
}

// Function to process events from epoll
fn process_events(epoll_fd int, lock_flag &sync.Mutex) {
	events := [max_connection_size]EpollEvent{}
	num_events := C.epoll_wait(epoll_fd, &events[0], max_connection_size, -1)
	for i := 0; i < num_events; i++ {
		if events[i].events & (C.EPOLLHUP | C.EPOLLERR) != 0 {
			handle_client_closure(unsafe { events[i].data.fd }, epoll_fd, lock_flag)
			continue
		}

		if events[i].events & C.EPOLLIN != 0 {
			mut buffer := [buffer_size]u8{}

			bytes_read := C.recv(unsafe { events[i].data.fd }, &buffer[0], sizeof(buffer) - 1,
				0)

			if bytes_read > 0 {
				// Send the response back to the client
				buffer[bytes_read] = u8(0)

				C.send(unsafe { events[i].data.fd }, response.str, response.len, 0)
				handle_client_closure(unsafe { events[i].data.fd }, epoll_fd, lock_flag)
			} else if bytes_read == 0
				|| (bytes_read < 0 && C.errno != C.EAGAIN && C.errno != C.EWOULDBLOCK) {
				handle_client_closure(unsafe { events[i].data.fd }, epoll_fd, lock_flag)
			}
		}
	}
}

// Worker thread function
fn worker_thread(epoll_fd int, lock_flag &sync.Mutex) {
	for {
		process_events(epoll_fd, lock_flag)
	}
}

// Event loop for accepting clients
fn event_loop(server_fd int, epoll_fd int, lock_flag &sync.Mutex) {
	for {
		handle_accept(server_fd, epoll_fd, lock_flag)
	}
}

fn cleanup(mut server Server) {
	println('Terminating...\n')
	for i := 0; i < server.thread_pool_size; i++ {
		// pthread_cancel(threads[i])
		// pthread_join(threads[i], (unsafe { nil }))

		// server.threads[i].cancel()
		// server.threads[i].join(unsafe { nil })
	}
	C.close(server.server_socket)
	C.close(server.epoll_fd)

	println('Server terminated')
}

fn main() {
	mut server := Server{}
	// os.signal_opt(.int, fn [mut server] (signum os.Signal) {
	// 	cleanup(mut server)
	// })!
	// os.signal_opt(.term, fn [mut server] (signum os.Signal) {
	// 	cleanup(mut server)
	// })!
	server.lock_flag = sync.new_mutex()

	// Create server socket and epoll instance
	server.server_socket = create_server_socket()
	if server.server_socket < 0 {
		return
	}
	server.epoll_fd = C.epoll_create1(0)
	if server.epoll_fd < 0 {
		eprintln('epoll_create1 failed')
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

	// Start worker threads
	for i in 0 .. initial_thread_pool_size {
		server.threads[i] = spawn worker_thread(server.epoll_fd, &server.lock_flag)
	}

	// Start the event loop for accepting clients
	event_loop(server.server_socket, server.epoll_fd, &server.lock_flag)
}
