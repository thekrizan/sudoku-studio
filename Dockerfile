FROM node:22-alpine AS frontend-build

WORKDIR /src
COPY package.json package-lock.json ./
RUN npm ci
COPY App.svelte index.css index.html index.js vite.config.js ./
RUN npm run build

FROM alpine:3.22 AS backend-build

RUN apk add --no-cache clang musl-dev linux-headers
WORKDIR /src
COPY main.c ./
COPY third_party/civetweb/ ./third_party/civetweb/
RUN mkdir -p /out && \
    clang -std=c11 -O2 -Wall -Wextra -Wpedantic \
      -DNO_SSL -DNO_WEBSOCKET \
      -Ithird_party/civetweb/include \
      main.c third_party/civetweb/src/civetweb.c \
      -o /out/sudoku-studio

FROM alpine:3.22 AS runtime

ENV HOST=0.0.0.0 \
    PORT=10000
WORKDIR /app
COPY --from=frontend-build /src/dist ./dist
COPY --from=backend-build /out/sudoku-studio /usr/local/bin/sudoku-studio
WORKDIR /app/dist
EXPOSE 10000
USER 10001:10001
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget -q -O /dev/null "http://127.0.0.1:${PORT:-10000}/api/health" || exit 1
ENTRYPOINT ["/usr/local/bin/sudoku-studio"]
