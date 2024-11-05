# web

A fast HTTP/1.1 web server that can sit behind a reverse proxy like `caddy` or `nginx` for HTTP 1/2/3 support.

## Features

- High performance
- Low latency
- Scales incredibly well with the number of routes

## Installation

```shell
go get git.akyoto.dev/go/web
```

## Usage

```go
s := web.NewServer()

// Static route
s.Get("/", func(ctx web.Context) error {
	return ctx.String("Hello")
})

// Parameter route
s.Get("/blog/:post", func(ctx web.Context) error {
	return ctx.String(ctx.Request().Param("post"))
})

// Wildcard route
s.Get("/images/*file", func(ctx web.Context) error {
	return ctx.String(ctx.Request().Param("file"))
})

// Middleware
s.Use(func(ctx web.Context) error {
	start := time.Now()

	defer func() {
		fmt.Println(ctx.Request().Path(), time.Since(start))
	}()

	return ctx.Next()
})

s.Run(":8080")
```

## Tests

```
PASS: TestBytes
PASS: TestString
PASS: TestError
PASS: TestErrorMultiple
PASS: TestRedirect
PASS: TestRequest
PASS: TestRequestHeader
PASS: TestRequestParam
PASS: TestWrite
PASS: TestWriteString
PASS: TestResponseCompression
PASS: TestResponseHeader
PASS: TestResponseHeaderOverwrite
PASS: TestPanic
PASS: TestRun
PASS: TestBadRequest
PASS: TestBadRequestHeader
PASS: TestBadRequestMethod
PASS: TestBadRequestProtocol
PASS: TestEarlyClose
PASS: TestUnavailablePort
coverage: 100.0% of statements
```

## Benchmarks

![wrk Benchmark](https://i.imgur.com/6cDeZVA.png)

## License

Please see the [license documentation](https://akyoto.dev/license).

## Copyright

© 2024 Eduard Urbach
