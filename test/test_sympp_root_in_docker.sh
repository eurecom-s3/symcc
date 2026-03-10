#!/usr/bin/env bash
set -euo pipefail

IMAGE="${1:-symcc}"

docker run --rm "$IMAGE" bash -lc '
  rm -rf /tmp/output/*
  sym++ -o /tmp/sample /home/ubuntu/sample.cpp
  echo test | /tmp/sample >/dev/null 2>&1
  grep -R -n -x "root" /tmp/output >/dev/null
'

echo "PASS: sym++ generated a testcase containing root"
