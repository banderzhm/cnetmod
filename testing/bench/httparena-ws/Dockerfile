# Package a previously verified Release artifact. Use the same immutable
# runtime image for baseline and candidate; its ABI must match the build.
ARG CNETMOD_WS_RUNTIME
FROM ${CNETMOD_WS_RUNTIME}
COPY bench_ws_server /bench_ws_server
EXPOSE 8080
ENTRYPOINT ["/bin/sh", "-c", "exec /bench_ws_server 8080 \"$(nproc)\""]
