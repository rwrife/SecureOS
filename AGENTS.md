# Overview

The goal is to build a operating system where all processes are run in total isolation and will not execute without explicit user consent. This OS should be built in a way to run on multiple architectures, however we're building for QEMU, x86 on the first pass since it'll offer the most compatibility

## Rules

- Any new (or existing) .c file should be properly commented at the top of the file with a description of what the file does, how it is used and what calls it
- Hardware access will always need an HAL layer since we are targeting multiple architectures, additionally the hardware may need to be virtualized in the event the user does not want to grant access to code that requires some level of hardware access
- Add planning docs to /plans with a unique name so we can review and impliment them later

## Output File Structure
- /os contains all of the OS-level binaries
- /lib contains all of the shared libraries used by the OS and user applications (ie file, networking, etc)
- /apps contains all of the user created applications that are not required for OS functionality