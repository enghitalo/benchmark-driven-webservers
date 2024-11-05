package web_test

import (
	"errors"
	"testing"

	"git.akyoto.dev/go/assert"
	"git.akyoto.dev/go/web"
)

func TestBytes(t *testing.T) {
	s := web.NewServer()

	s.Get("/", func(ctx web.Context) error {
		return ctx.Bytes([]byte("Hello"))
	})

	response := s.Request("GET", "/", nil, nil)
	assert.Equal(t, response.Status(), 200)
	assert.Equal(t, string(response.Body()), "Hello")
}

func TestString(t *testing.T) {
	s := web.NewServer()

	s.Get("/", func(ctx web.Context) error {
		return ctx.String("Hello")
	})

	response := s.Request("GET", "/", nil, nil)
	assert.Equal(t, response.Status(), 200)
	assert.Equal(t, string(response.Body()), "Hello")
}

func TestError(t *testing.T) {
	s := web.NewServer()

	s.Get("/", func(ctx web.Context) error {
		return ctx.Status(401).Error("Not logged in")
	})

	response := s.Request("GET", "/", nil, nil)
	assert.Equal(t, response.Status(), 401)
	assert.Equal(t, string(response.Body()), "")
}

func TestErrorMultiple(t *testing.T) {
	s := web.NewServer()

	s.Get("/", func(ctx web.Context) error {
		return ctx.Status(401).Error("Not logged in", errors.New("Missing auth token"))
	})

	response := s.Request("GET", "/", nil, nil)
	assert.Equal(t, response.Status(), 401)
	assert.Equal(t, string(response.Body()), "")
}

func TestRedirect(t *testing.T) {
	s := web.NewServer()

	s.Get("/", func(ctx web.Context) error {
		return ctx.Redirect(301, "/target")
	})

	response := s.Request("GET", "/", nil, nil)
	assert.Equal(t, response.Status(), 301)
	assert.Equal(t, response.Header("Location"), "/target")
}
