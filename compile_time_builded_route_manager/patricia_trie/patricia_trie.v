module main

import benchmark

struct TrieNode {
mut:
	children   map[string]&TrieNode
	handler    ?fn (params map[string]string) !string = unsafe { nil } // evita o uso de opcional
	is_param   bool
	param_name string
}

// Patricia Trie router with parameterized route support
struct Router {
mut:
	root TrieNode
	params map[string]string // reuso de mapa para evitar nova alocação em cada requisição
}

// Adds a route to the Patricia Trie with support for parameters
fn (mut router Router) add_route(method string, path string, handler fn (params map[string]string) !string) {
	segments := path.split('/').filter(it.len > 0)
	mut node := &router.root

	for segment in segments {
		is_param := segment.starts_with(':')
		segment_key := if is_param { ':' } else { segment }

		if segment_key !in node.children {
			node.children[segment_key] = &TrieNode{
				children: map[string]&TrieNode{}
				is_param: is_param
				param_name: if is_param { segment[1..] } else { '' }
			}
		}

		node = node.children[segment_key] or { panic("Unexpected trie error") }
	}
	node.handler = handler
}

// Finds and executes the handler for a given route
fn (mut router Router) handle_request(method string, path string) !string {
	segments := path.split('/').filter(it.len > 0)
	mut node := &router.root
	router.params.clear() // limpa o mapa sem desalocar

	for segment in segments {
		if segment in node.children {
			node = node.children[segment] or { panic("Unexpected trie error") }
		} else if ':' in node.children {
			param_node := node.children[':'] or { panic("Unexpected trie error") }
			router.params[param_node.param_name] = segment
			node = param_node
		} else {
			return error('Route not found')
		}
	}

	if node.handler? == unsafe { nil } {
		return error('Route not found')
	}
	handler := node.handler or { return error('There is not a handler for this route') }

	return handler(router.params)!
}

// Example handlers
fn get_user(params map[string]string) !string {
	if id := params['id'] {
		return 'GET user with ID $id'
	}
	return 'GET all users'
}

fn create_user(params map[string]string) !string {
	return 'POST create user'
}

fn update_user(params map[string]string) !string {
	id := params['id'] or { return error('User ID required') }
	return 'PUT update user with ID $id'
}

fn delete_user(params map[string]string) !string {
	id := params['id'] or { return error('User ID required') }
	return 'DELETE user with ID $id'
}

// New handler for updating user name
fn update_user_name(params map[string]string) !string {
	id := params['id'] or { return error('User ID required') }
	name := params['name'] or { return error('User name required') }
	return 'PUT update user with ID $id to name $name'
}

// Initialize the router and add the routes
fn setup_router() Router {
	mut router := Router{
		root: TrieNode{children: map[string]&TrieNode{}}
	}

	// Adding routes with handler functions
	router.add_route('GET', '/user', get_user)
	router.add_route('GET', '/user/:id', get_user)
	router.add_route('POST', '/user', create_user)
	router.add_route('PUT', '/user/:id', update_user)
	router.add_route('DELETE', '/user/:id', delete_user)
	router.add_route('PUT', '/user/:id/:name', update_user_name)
	router.add_route('PUT', '/user/:id/name/:name', update_user_name) // Add route with two parameters


	return router
}

fn main() {
	mut router := setup_router()

	println(router.handle_request('GET', '/user')!)
	println(router.handle_request('GET', '/user/123')!)
	println(router.handle_request('POST', '/user')!)
	println(router.handle_request('PUT', '/user/123')!)
	println(router.handle_request('DELETE', '/user/123')!)
	println(router.handle_request('PUT', '/user/123/John')!)
	println(router.handle_request('PUT', '/user/324/name/Johne')!)

	bench()!

	println('Patricia Trie router')
}

fn bench() ! {
	mut router := setup_router()

	mut b := benchmark.start()

	for _ in 0..100000 {
		router.handle_request('GET', '/user')!
		router.handle_request('GET', '/user/123')!
		router.handle_request('POST', '/user')!
		router.handle_request('PUT', '/user/123')!
		router.handle_request('DELETE', '/user/123')!
		router.handle_request('PUT', '/user/123/John')!
		router.handle_request('PUT', '/user/123/name/John')!
	}

	b.measure('Patricia Trie')
}