# Display the top-level command index.
print SecureOS Shell Commands
print =======================
print
print   Navigation & Files:
print     ls [dir]              list directory contents
print     cd <dir>              change working directory
print     cat <file>            display file contents
print     write <file> <text>   write text to a file
print     append <file> <text>  append text to a file
print     mkdir <dir>           create a directory
print     rm [-f] [-v] [-d] <path>  remove files or directories
print
print   System:
print     about [file]          show system or file information
print     env [key[=val]]       view or set environment variables
print     session [list|new|switch <id>]  manage sessions
print     storage               show storage information
print     clear                 clear the console
print     exit <pass|fail>      terminate the current session
print     date                  display the current date and time
print
print   Networking:
print     ping <host>           send an ICMP echo request
print     ifconfig              display network interfaces
print     http                  make HTTP requests
print
print   Applications & Libraries:
print     apps                  list installed applications
print     run <app>             launch an application
print     libs [loaded|use|release]  manage shared libraries
print     loadlib <lib>         load a shared library
print     unload <handle>       unload a shared library
print
print   Security:
print     auth-cache [list|reset]  manage authentication cache
print
print   Help:
print     help [command]        show this list or details for a command
print     echo <text>           print text to the console
print
print tip: run "help <command>" for detailed usage of any command.
