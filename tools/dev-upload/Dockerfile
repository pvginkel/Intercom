FROM alpine:latest

RUN apk add --no-cache \
      openssl \
      curl \
      bash \
      coreutils

WORKDIR /workspace

COPY scripts/upload.sh /usr/local/bin/upload.sh
RUN chmod +x /usr/local/bin/upload.sh

ENTRYPOINT ["/usr/local/bin/upload.sh"]
