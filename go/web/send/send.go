package send

import (
	"encoding/json"

	"git.akyoto.dev/go/web"
)

// CSS sends the body with the content type set to `text/css`.
func CSS(ctx web.Context, body string) error {
	ctx.Response().SetHeader("Content-Type", "text/css")
	return ctx.String(body)
}

// CSV sends the body with the content type set to `text/csv`.
func CSV(ctx web.Context, body string) error {
	ctx.Response().SetHeader("Content-Type", "text/csv")
	return ctx.String(body)
}

// HTML sends the body with the content type set to `text/html`.
func HTML(ctx web.Context, body string) error {
	ctx.Response().SetHeader("Content-Type", "text/html")
	return ctx.String(body)
}

// JS sends the body with the content type set to `text/javascript`.
func JS(ctx web.Context, body string) error {
	ctx.Response().SetHeader("Content-Type", "text/javascript")
	return ctx.String(body)
}

// JSON encodes the object in JSON format and sends it with the content type set to `application/json`.
func JSON(ctx web.Context, object any) error {
	ctx.Response().SetHeader("Content-Type", "application/json")
	return json.NewEncoder(ctx.Response()).Encode(object)
}

// Text sends the body with the content type set to `text/plain`.
func Text(ctx web.Context, body string) error {
	ctx.Response().SetHeader("Content-Type", "text/plain")
	return ctx.String(body)
}

// XML sends the body with the content type set to `text/xml`.
func XML(ctx web.Context, body string) error {
	ctx.Response().SetHeader("Content-Type", "text/xml")
	return ctx.String(body)
}
