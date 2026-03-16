# Help Command Build-Time Embedded Resources

## Goal
Embed command help content into `help.elf` at build time from editable text files.

## Plan
1. Store command help docs in `user/apps/os/help/resources/*.txt`.
2. Generate a C source file during user-app build that maps `<file stem>` to help text.
3. Compile and link generated resources into the help app.
4. Resolve `help` to `index` and `help <command>` to matching embedded entry.

## Notes
- This removes runtime dependency on `/os/help.txt` for help content.
- Adding or editing command help becomes a text-file-only change plus rebuild.
