# Overview

The goal is to build a operating system where all processes are run in total isolation and will not execute without explicit user consent. This OS should be built in a way to run on multiple architectures, however we're building for QEMU, x86 on the first pass since it'll offer the most compatibility

## Guidlines

- Any new (or existing) .c file should be properly commented at the top of the file with a description of what the file does, how it is used and what calls it
- Hardware access will always need an HAL layer since we are targeting multiple architectures, additionally the hardware may need to be virtualized in the event the user does not want to grant access to code that requires some level of hardware access
- Add planning docs to /docs/plans with a unique name so we can review and impliment them later
- Whenever a new cli commands is created, make sure to add a help command resource file for the user to reference
- Keep ps1 and sh scripts in sync, specifically the build commands so we can build on windows, linux and macos
- non-kernel essential commands and libraries should be built in isolation and standalone binaries and copied to disk and the kernel should not have any specific knowledge of these commands and load them dynamically
- any command that offers functionality that would be use useful to another application should pull the functionality out into a standalone library that can be imported

## Kernel

- the kernel should be extremely minimal and only contain logic for managing sessions, launching processes and providing hardware abstraction layers with drivers to physical hardware
- the kernel will have the gatekeeper logic for the capabilities and use confirmation of those capabilities
- the kernel will manage the console
- the kernel will manage the event bus
- the process manager, process.c, is what is used to spawn new processes from external binaries and load dependency libraries for those processes to use, it should not have any knowledge of the specific process other than the filename of the binary

## Libraries

- the libraries should be build as standalong binaries that can be loaded by process.c in a dynamic fashion
- the libraries should contain any share logic that would be useful by multiple user-facing binaries

## Output File Structure

- /os contains all of the OS-level binaries
- /lib contains all of the shared libraries used by the OS and user applications (ie file, networking, etc)
- /apps contains all of the user created applications that are not required for OS functionality