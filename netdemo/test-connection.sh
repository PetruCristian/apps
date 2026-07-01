#!/bin/bash
# Test the netdemo HTTP server from the host. Run from apps/netdemo/ in a second
# terminal, while ./run.sh is up and has printed "http: listening on 0.0.0.0:8080".
echo "== curl -v (full request/response, expect HTTP/1.1 200 OK) =="
curl -v http://localhost:8080/
echo
echo "== curl -s (body only) =="
curl -s http://127.0.0.1:8080/
