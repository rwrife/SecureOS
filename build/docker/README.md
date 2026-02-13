# Toolchain Container

This image is the baseline for deterministic SecureOS builds.

## Build

```bash
docker build -f build/docker/Dockerfile.toolchain -t secureos/toolchain:bookworm-2026-02-12 .
```

## Verify base image digest

```bash
docker image inspect debian:bookworm-slim --format '{{index .RepoDigests 0}}'
```

Expected digest (see `build/toolchain.lock`):

```text
docker.io/library/debian@sha256:98f4b71de414932439ac6ac690d7060df1f27161073c5036a7553723881bffbe
```

## Run toolchain shell

```bash
docker run --rm -it -v "$PWD":/workspace -w /workspace secureos/toolchain:bookworm-2026-02-12 bash
```
