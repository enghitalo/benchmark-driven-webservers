/*
wrk -t16 -c512 -d10s http://127.0.0.1:8080

Running 1m test @ http://127.0.0.1:8080
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     9.81ms   61.52ms   1.67s    96.62%
    Req/Sec     5.11k     1.76k   10.80k    67.75%
  4870666 requests in 1.00m, 0.00B read
  Socket errors: connect 0, read 4870667, write 0, timeout 173
Requests/sec:  81046.15
Transfer/sec:       0.00B
*/
import net
import sync
import time

const port = 8080
const backlog = 512
const num_workers = 16
const response = 'HTTP/1.1 200 OK\r\n' + 'Date: Wed, 23 Oct 2024 12:00:00 GMT\r\n' +
	'Content-Type: application/json\r\n' + 'Content-Length: 27\r\n' +
	'Connection: keep-alive\r\n\r\n' + '{\r\n  "message": "Hello, world!"\r\n}'

// @[heap]
// pub struct TcpConn {
// pub mut:
// 	sock           TcpSocket
// 	handle         int
// 	write_deadline time.Time
// 	read_deadline  time.Time
// 	read_timeout   time.Duration
// 	write_timeout  time.Duration
// 	is_blocking    bool = true
// }

// struct TcpSocket {
// 	Socket
// }

// pub struct Socket {
// pub:
// 	handle int
// }

struct Node {
	client net.TcpConn
mut:
	next &Node = unsafe { nil }
}

struct LinkedList {
mut:
	head &Node = unsafe { nil }
	tail &Node = unsafe { nil }
}

struct Server {
mut:
	server_socket net.TcpListener
	client_queue  LinkedList
	queue_lock    sync.Mutex
	client_count  u32
}

@[unsafe]
fn (list &LinkedList) free() {
	mut current := list.head
	for current != unsafe { nil } {
		mut next := current.next
		current.next = unsafe { nil }
		unsafe { free(current) }
		current = next
	}
	list.head = unsafe { nil }
	list.tail = unsafe { nil }
}

fn (mut ll LinkedList) enqueue(client net.TcpConn) {
	new_node := &Node{
		client: client
	}
	if ll.tail == unsafe { nil } {
		ll.head = new_node
		ll.tail = new_node
	} else {
		ll.tail.next = new_node
		ll.tail = new_node
	}
}

fn (mut ll LinkedList) dequeue() ?net.TcpConn {
	if ll.head == unsafe { nil } {
		return none
	}
	client := ll.head.client
	ll.head = ll.head.next
	if ll.head == unsafe { nil } {
		ll.tail = unsafe { nil }
	}
	return client
}

fn (mut s Server) enqueue_client(client net.TcpConn) {
	s.queue_lock.@lock()
	s.client_queue.enqueue(client)
	s.queue_lock.unlock()
	C.atomic_fetch_add_u32(&s.client_count, 1)
}

fn (mut s Server) dequeue_client() net.TcpConn {
	for {
		if C.atomic_load_u32(&s.client_count) > 0 {
			s.queue_lock.@lock()
			client := s.client_queue.dequeue() or {
				s.queue_lock.unlock()
				continue
			}
			s.queue_lock.unlock()
			C.atomic_fetch_sub_u32(&s.client_count, 1)
			return client
		}
		time.sleep(100 * time.microsecond)
	}

	return net.TcpConn{}
}

fn (mut s Server) worker_thread() {
	for {
		mut client := s.dequeue_client()
		mut buffer := []u8{len: 1024}
		client.read(mut buffer) or { continue }
		client.write(unsafe { response.str.vbytes(response.len) }) or { continue }
		// client.write_string(response) or { continue }
		client.close() or { continue }
	}
}

fn main() {
	mut server := &Server{
		server_socket: net.listen_tcp(.ip6, ':${port}') or { panic(err) }
	}

	for _ in 0 .. num_workers {
		spawn server.worker_thread()
	}

	for {
		mut client := server.server_socket.accept() or { continue }
		// client.set_blocking(false) or { panic(err) }
		server.enqueue_client(client)
	}
}
