# Production image for the docs site, on the shared NPRPC runtime image
# (see docs/DOCKER_RUNTIME_IMAGE.md). deploy.sh stages the build context:
#
#   docs-server   release build, compiled in nprpc-dev
#   templates/    Mustache templates
#   web/          static root (CSS, scripts)
#   api.json      from `just docs-api`
ARG NPRPC_RUNTIME_IMAGE=nprpc-runtime:latest
FROM ${NPRPC_RUNTIME_IMAGE}

COPY docs-server /app/docs-server
COPY templates /app/templates
COPY web /app/web
COPY api.json /app/api.json

ENV DOCS_ROOT=/app
ENV DOCS_API=/app/api.json
ENV DOCS_PORT=443

WORKDIR /app
EXPOSE 443/tcp 443/udp

ENTRYPOINT ["/app/docs-server"]
