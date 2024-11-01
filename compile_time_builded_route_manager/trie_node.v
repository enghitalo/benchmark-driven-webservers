module main

import benchmark

struct MethodNode {
	method_str &u8 = unsafe { nil }
	method_len int
	handler    ?fn ()
	next       &MethodNode = unsafe { nil }
}

struct TrieNode {
	segment_str &u8 = unsafe { nil }
	segment_len int
mut:
	children map[string]&TrieNode
	methods  &MethodNode = unsafe { nil }
}

// Split path into segments
fn split_path(path string) []string {
	return path.trim('/').split('/')
}

// Create a new Trie node with a segment as a raw byte pointer
fn create_trie_node(segment_str &u8, segment_len int) &TrieNode {
	return &TrieNode{
		segment_str: unsafe { segment_str }
		segment_len: segment_len
		children:    map[string]&TrieNode{}
	}
}

// Insert a route into the Trie
fn insert_route(mut root TrieNode, path string, method_str &u8, method_len int, handler fn ()) {
	segments := split_path(path)
	mut current := unsafe { &root }

	for segment in segments {
		segment_ptr := segment.str
		segment_len := segment.len

		// Check if a child with the same segment exists
		segment_key := segment
		// dump(segment_key)
		if segment_key in current.children {
			current = current.children[segment_key] or { return }
		} else {
			// If no child matches, create a new node for this segment
			child_node := create_trie_node(segment_ptr, segment_len)
			current.children[segment_key] = child_node
			current = child_node
		}
	}

	// Add method and handler at the leaf node
	new_method := &MethodNode{
		method_str: unsafe { method_str }
		method_len: method_len
		handler:    handler
		next:       current.methods
	}
	current.methods = new_method
}

// Search for a route in the Trie
fn search_route(mut root TrieNode, path string, method &u8, method_len int) ?fn () {
	segments := split_path(path)
	mut current := unsafe { &root }

	for segment in segments {
		segment_key := segment

		// Check for the segment in the Trie
		if segment_key in current.children {
			current = current.children[segment_key] or { return none }
		} else {
			return none // Path not found
		}
	}

	// Check for the method in the final node
	mut method_node := current.methods
	for method_node != unsafe { nil } {
		if method_node.method_len == method_len {
			if unsafe { vmemcmp(method_node.method_str, method, method_len) } == 0 {
				return method_node.handler
			}
		}
		method_node = method_node.next
	}

	return none // Method not found
}

// Example handler function
fn example_handler() {
	println('Handler called!')
}

pub struct ControllerPath {
pub:
	method  string
	path    []string
	handler ?fn () = unsafe { nil }
}

fn main() {
	println('builded')
	mut root := create_trie_node(unsafe { nil }, 0)

	// Insert a route with method and path as raw bytes
	// method := 'GET'
	methods := ['GET', 'POST', 'PUT', 'DELETE']

	mut controllers := []ControllerPath{}

	mut b := benchmark.start()
	for method in methods {
		for i in 0 .. 1_000 {
			controllers << ControllerPath{
				method:  method
				path:    '/api/v1/users${i}'.split('/')
				handler: example_handler
			}
		}
	}

	b.measure(('create_controllers'))

	for method in methods {
		for _ in 0 .. 1_000 {
			_ := old_search_route(controllers, '/api/v1/users999', method) or {
				println('Route not found! in old_search_route')
				return
			}
		}
	}

	b.measure(('old_search_route'))

	for method in methods {
		for i in 0 .. 1_000 {
			insert_route(mut root, '/api/v1/users${i}', method.str, method.len, example_handler)
		}
	}

	b.measure(('insert_route'))

	for method in methods {
		for _ in 0 .. 1_000 {
			// Search for the route with method as raw bytes
			_ := search_route(mut root, '/api/v1/users999', method.str, method.len) or {
				println('Route not found! in search_route')
				return
			}
		}
	}

	b.measure(('search_route'))
}

fn old_search_route(controllers []ControllerPath, path string, method string) ?fn () {
	for controller in controllers {
		if controller.path.join('/') == path && controller.method == method {
			return controller.handler
		}
	}
	return none
}
