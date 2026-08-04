package main

import (
	"crypto/tls"
	"flag"
	"fmt"
	"log"
	"net/http"

	"github.com/quic-go/quic-go/http3"
	"github.com/valyala/fasthttp"
)

func main() {
	port := flag.Int("port", 18082, "listen port")
	mode := flag.String("mode", "http1", "http1, fasthttp, http2, or http3")
	cert := flag.String("cert", "", "TLS certificate")
	key := flag.String("key", "", "TLS private key")
	flag.Parse()

	handler := http.HandlerFunc(func(response http.ResponseWriter, request *http.Request) {
		response.Header().Set("Content-Type", "text/plain")
		_, _ = response.Write([]byte("Hello, World!"))
	})
	address := fmt.Sprintf("127.0.0.1:%d", *port)
	if *mode == "fasthttp" {
		fmt.Printf("ready %d\n", *port)
		handler := func(context *fasthttp.RequestCtx) {
			context.Response.Header.SetContentType("text/plain")
			context.SetBodyString("Hello, World!")
		}
		if *cert != "" {
			log.Fatal(fasthttp.ListenAndServeTLS(address, *cert, *key, handler))
		}
		log.Fatal(fasthttp.ListenAndServe(address, handler))
	}

	if *mode == "http3" {
		server := http3.Server{Addr: address, Handler: handler}
		fmt.Printf("ready %d\n", *port)
		log.Fatal(server.ListenAndServeTLS(*cert, *key))
	}

	protocols := new(http.Protocols)
	switch *mode {
	case "http1":
		protocols.SetHTTP1(true)
	case "http2":
		if *cert == "" {
			protocols.SetUnencryptedHTTP2(true)
		} else {
			protocols.SetHTTP2(true)
		}
	default:
		log.Fatalf("unknown mode %q", *mode)
	}
	server := &http.Server{
		Addr:      address,
		Handler:   handler,
		Protocols: protocols,
		TLSConfig: &tls.Config{MinVersion: tls.VersionTLS13},
	}
	fmt.Printf("ready %d\n", *port)
	if *cert == "" {
		log.Fatal(server.ListenAndServe())
	}
	log.Fatal(server.ListenAndServeTLS(*cert, *key))
}
