/*
wrk -t16 -c512 -d10s http://127.0.0.1:8080

Running 10s test @ http://127.0.0.1:8080
  16 threads and 512 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    21.72ms  122.35ms   1.81s    96.77%
    Req/Sec     2.84k     1.18k    6.24k    65.87%
  447761 requests in 10.10s, 0.00B read
  Socket errors: connect 0, read 447761, write 0, timeout 166
Requests/sec:  44330.27
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

struct Server {
mut:
	server_socket net.TcpListener
	client_queue  []net.TcpConn
	queue_lock    sync.Mutex
	client_count  u32
}

fn (mut s Server) enqueue_client(client net.TcpConn) {
	s.queue_lock.@lock()
	s.client_queue << client
	s.queue_lock.unlock()
	C.atomic_fetch_add_u32(&s.client_count, 1)
}

fn (mut s Server) dequeue_client() net.TcpConn {
	for {
		if C.atomic_load_u32(&s.client_count) > 0 {
			s.queue_lock.@lock()
			if s.client_queue.len > 0 {
				client := s.client_queue[0]
				s.client_queue.delete(0)
				s.queue_lock.unlock()
				C.atomic_fetch_sub_u32(&s.client_count, 1)
				return client
			}
			s.queue_lock.unlock()
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
		client.write_string(response) or { continue }
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
		server.enqueue_client(client)
	}
}
