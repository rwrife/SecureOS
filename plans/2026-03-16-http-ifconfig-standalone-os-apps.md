# HTTP And Ifconfig Standalone OS Apps

## Goal

Move `http` and `ifconfig` from script-backed `/os` commands to standalone
user app sources under `user/apps/os/`.

## Scope

- Add `user/apps/os/http/main.c`
- Add `user/apps/os/ifconfig/main.c`
- Package the built binaries into `/os/http.bin` and `/os/ifconfig.bin`
- Stop sourcing those two commands from `user/os_commands/*.cmd`

## Notes

- Keep the existing console command names unchanged.
- Preserve the current disk image layout so `process_run()` still resolves the
  commands from `/os/`.
- Follow the existing `user/apps/os/<command>/main.c` command layout.