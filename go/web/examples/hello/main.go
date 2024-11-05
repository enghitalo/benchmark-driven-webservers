package main

import (
	"git.akyoto.dev/go/web"
)

func main() {
	s := web.NewServer()

	s.Get("/", func(ctx web.Context) error {
		return ctx.String("Hello")
	})

	s.Run(":8080")
}
