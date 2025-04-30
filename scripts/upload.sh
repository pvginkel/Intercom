#!/bin/sh

set -e

TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
SIGNATURE=$(echo -n $TIMESTAMP | openssl dgst -sha256 -sign "$1" | base64)
HOST=${3:-iotsupport.iotsupport.svc.cluster.local}

curl \
    --output - \
    -F "file=@$2" \
    -F "timestamp=$TIMESTAMP" \
    -F "signature=$SIGNATURE" \
    http://$HOST/assetctl/upload.php
