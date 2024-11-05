package web_test

import (
	"fmt"
	"testing"

	"git.akyoto.dev/go/assert"
	"git.akyoto.dev/go/web"
)

func TestRequest(t *testing.T) {
	s := web.NewServer()

	s.Get("/request", func(ctx web.Context) error {
		req := ctx.Request()
		method := req.Method()
		scheme := req.Scheme()
		host := req.Host()
		path := req.Path()
		return ctx.String(fmt.Sprintf("%s %s %s %s", method, scheme, host, path))
	})

	response := s.Request("GET", "http://example.com/request?x=1", []web.Header{{"Accept", "*/*"}}, nil)
	assert.Equal(t, response.Status(), 200)
	assert.Equal(t, string(response.Body()), "GET http example.com /request")
}

func TestRequestHeader(t *testing.T) {
	s := web.NewServer()

	s.Get("/", func(ctx web.Context) error {
		accept := ctx.Request().Header("Accept")
		empty := ctx.Request().Header("")
		return ctx.String(accept + empty)
	})

	response := s.Request("GET", "/", []web.Header{{"Accept", "*/*"}}, nil)
	assert.Equal(t, response.Status(), 200)
	assert.Equal(t, string(response.Body()), "*/*")
}

func TestRequestParam(t *testing.T) {
	s := web.NewServer()

	s.Get("/blog/:article", func(ctx web.Context) error {
		article := ctx.Request().Param("article")
		empty := ctx.Request().Param("")
		return ctx.String(article + empty)
	})

	response := s.Request("GET", "/blog/my-article", nil, nil)
	assert.Equal(t, response.Status(), 200)
	assert.Equal(t, string(response.Body()), "my-article")
}
