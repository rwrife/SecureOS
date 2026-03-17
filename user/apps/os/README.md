# OS Command App Sources

This folder contains standalone source files for shell-level OS commands.

Current command app sources:
- `help/main.c`
- `ping/main.c`
- `http/main.c`
- `ifconfig/main.c`
- `echo/main.c`
- `ls/main.c`
- `cat/main.c`
- `write/main.c`
- `append/main.c`
- `mkdir/main.c`
- `cd/main.c`
- `apps/main.c`
- `storage/main.c`
- `vgahello/main.c`

Note:
- `http` and `ifconfig` are packaged into `/os/*.bin` from these standalone user app sources.
- These `.c` files establish the one-file-per-command layout and are the target for full user-app build/deploy wiring.
