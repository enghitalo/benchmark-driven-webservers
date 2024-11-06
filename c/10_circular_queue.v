/*
wrk -t16 -c512 -d60s http://127.0.0.1:8080

Running 30s test @ http://127.0.0.1:8080
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.20ms    1.30ms  43.98ms   71.73%
    Req/Sec     7.52k   566.85     9.81k    73.09%
  3597961 requests in 30.10s, 411.75MB read
Requests/sec: 119535.90
Transfer/sec:     13.68MB
*/
module circular_queue

import sync
import time

#include <arpa/inet.h>

pub enum SocketType {
	udp       = C.SOCK_DGRAM
	tcp       = C.SOCK_STREAM
	seqpacket = C.SOCK_SEQPACKET
}

// AddrFamily are the available address families
pub enum AddrFamily {
	unix   = C.AF_UNIX
	ip     = C.AF_INET
	ip6    = C.AF_INET6
	unspec = C.AF_UNSPEC
}

fn C.socket(domain AddrFamily, typ SocketType, protocol int) int

fn C.htons(host u16) u16

fn C.bind(sockfd int, address &C.sockaddr_in, addrlen u32) int

fn C.listen(sockfd int, backlog int) int

fn C.accept(sockfd int, address &C.sockaddr_in, addrlen &u32) int

fn C.setsockopt(sockfd int, level int, optname int, optval &int, optlen u32) int

fn C.recv(sockfd int, buffer &u8, len u32, flags int) int

fn C.send(sockfd int, buffer &u8, len u32, flags int) int

pub struct C.sockaddr_in {
mut:
	sin_family AddrFamily
	sin_port   u16
	sin_addr   int
}

const port = 8080
const buffer_size = 140
const response_body = '{ "message": "Hello, world!" }'
const max_waiting_queue_size = 512
const initial_thread_pool_size = 8
const max_thread_pool_size = 8
const response = 'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ${response_body.len}\r\nConnection: keep-alive\r\n\r\n${response_body}'

const backlog = 512

struct Server {
mut:
	server_socket    int
	client_queue     [max_waiting_queue_size]int
	queue_head       int
	queue_tail       int
	queue_lock       sync.Mutex
	client_count     u32
	has_clients      bool
	thread_pool_size u32 = u32(8)
	shutdown_flag    bool
}

// Enqueues a client connection
fn (mut s Server) enqueue_client(client int) {
	s.queue_lock.lock()
	if s.client_count < max_waiting_queue_size {
		s.client_queue[s.queue_tail] = client
		s.queue_tail = (s.queue_tail + 1) % max_waiting_queue_size
		s.client_count++
		s.has_clients = true
	} else {
		C.close(client)
	}
	s.queue_lock.unlock()
}

// Dequeues a client connection
fn (mut s Server) dequeue_client() int {
	mut client := -1
	for {
		s.queue_lock.lock()
		if s.client_count > 0 {
			client = s.client_queue[s.queue_head]
			s.queue_head = (s.queue_head + 1) % max_waiting_queue_size
			s.client_count--
			if s.client_count == 0 {
				s.has_clients = false
			}
			s.queue_lock.unlock()
			break
		}
		s.queue_lock.unlock()
		time.sleep(100 * time.microsecond)
	}
	return client
}

// Worker thread function
fn (mut s Server) worker_thread() {
	for {
		if s.shutdown_flag {
			break
		}
		client := s.dequeue_client()
		if client < 0 {
			continue
		}
		buffer := [buffer_size]u8{}
		if C.recv(client, &buffer[0], buffer_size, 0) <= 0 {
			C.close(client)
			continue
		}
		C.send(client, response.str, response.len, 0)
		C.close(client)
	}
}

fn (server Server) client_count_monitor() {
	for {
		if server.shutdown_flag {
			break
		}
		println('Client count: ${server.client_count}')
		time.sleep(1 * time.second)
	}
}

fn main() {
	mut server := &Server{
		server_socket: C.socket(AddrFamily.ip, SocketType.tcp, 0)
	}

	if server.server_socket < 0 {
		eprintln('socket failed')
		return
	}

	opt := 1
	C.setsockopt(server.server_socket, C.SOL_SOCKET, C.SO_REUSEADDR | C.SO_REUSEPORT,
		&opt, sizeof(opt))

	address := C.sockaddr_in{
		sin_family: AddrFamily.ip
		sin_port:   C.htons(port)
		sin_addr:   C.INADDR_ANY
	}
	if C.bind(server.server_socket, &address, sizeof(address)) < 0
		|| C.listen(server.server_socket, backlog) < 0 {
		eprintln('bind or listen failed')
		return
	}

	println('Listening on http://localhost:${port}')

	for i := 0; i < server.thread_pool_size; i++ {
		spawn server.worker_thread()
	}
	// spawn server.client_count_monitor()

	mut addrlen := sizeof(address)
	for {
		client := C.accept(server.server_socket, &address, &addrlen)
		if client < 0 {
			eprintln('accept failed')
			continue
		}
		server.enqueue_client(client)
	}
}
