module main

struct MethodNode {
	method_str &u8 = unsafe { nil }
	method_len int
	handler    ?fn (string)
	next       &MethodNode = unsafe { nil }
}

fn (m &TrieNode) str() string {
	unsafe {
		println(m.segment_str.vstring_with_len(m.segment_len)) // users999
	}
	return 'm.str()'
}

struct TrieNode {
	segment_str &u8 = unsafe { nil }
	segment_len int
mut:
	children map[string]&TrieNode
	methods  &MethodNode = unsafe { nil }
	is_param bool
	param_name string
}

// Split path into segments
fn split_path(path string) []string {
	return path.trim('/').split('/')
}

// Create a new Trie node with a segment as a raw byte pointer
fn create_trie_node(segment_str &u8, segment_len int, is_param bool, param_name string) &TrieNode {
	return &TrieNode{
		segment_str: unsafe { segment_str }
		segment_len: segment_len
		children:    map[string]&TrieNode{}
		is_param:    is_param
		param_name:  param_name
	}
}

// Insert a route into the Trie
fn insert_route(mut root TrieNode, path string, method_str &u8, method_len int, handler fn (string)) {
	segments := split_path(path)
	mut current := unsafe { &root }

	for segment in segments {
		segment_ptr := segment.str
		segment_len := segment.len
		is_param := segment.starts_with(':')
		param_name := if is_param { segment[1..] } else { '' }

		// Check if a child with the same segment exists
		segment_key := if is_param { ':' } else { segment }
		if segment_key in current.children {
			current = current.children[segment_key] or { return }
		} else {
			// If no child matches, create a new node for this segment
			child_node := create_trie_node(segment_ptr, segment_len, is_param, param_name)
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
fn search_route(mut root TrieNode, path string, method &u8, method_len int) ?fn (string) {
	segments := split_path(path)
	mut current := unsafe { &root }
	mut params := map[string]string{}

	for segment in segments {
		segment_key := segment

		// Check for the segment in the Trie
		if segment_key in current.children {
			current = current.children[segment_key] or { return none }
		} else if ':' in current.children {
			current = current.children[':'] or { return none }
			params[current.param_name] = segment_key
		} else {
			return none // Path not found
		}
	}

	// Check for the method in the final node
	mut method_node := current.methods
	for method_node != unsafe { nil } {
		if method_node.method_len == method_len {
			if unsafe { vmemcmp(method_node.method_str, method, method_len) } == 0 {
				// return fn (id string) {
					return method_node.handler// or { return }(params['id'])
				// }
			}
		}
		method_node = method_node.next
	}

	return none // Method not found
}

// Example handler function
fn example_handler(id string) {
	println('Handler called!')
}

// Example handler function
fn example_handler1(id string) {
	println('Handler different called!')
}

// Example handler function with id
fn example_handler_with_id(id string) {
	println('Handler called with id ${id}')
}

fn main() {
	println('builded')
	mut root := create_trie_node(unsafe { nil }, 0, false, '')

	// Insert a route with method and path as raw bytes
	methods := ['GET', 'POST', 'PUT', 'DELETE']

	insert_route(mut root, '/api/v1', methods[0].str, methods[0].len, example_handler1)
	insert_route(mut root, '/api/v1/users999', methods[0].str, methods[0].len, example_handler)
	insert_route(mut root, '/api/v1/users/:id', methods[0].str, methods[0].len, example_handler_with_id)

	// Search for the route with method as raw bytes
	// will call example_handler_with_id
	search_route(mut root, '/api/v1/users/999', methods[0].str, methods[0].len) or {
		println('Route not found1')
		return
	}('999')
	search_route(mut root, '/api/v1/users999', methods[0].str, methods[0].len) or {
		println('Route not found1')
		return
	}('')
	search_route(mut root, '/api/v1', methods[0].str, methods[0].len)?('')
	search_route(mut root, '/users999', methods[0].str, methods[0].len)?('')
	search_route(mut root, '/api', methods[0].str, methods[0].len)?('')
}
