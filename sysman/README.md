# sysman
Anillo OS's system manager.

This is the first process launched on system startup and it is in charge of managing the system from userspace.

## Naming and Purpose

sysman is named sysman because it is the **sys**tem **man**ager, in charge of managing the entire system (including other managers).

In Anillo OS, all managers should end with the suffix `man`, indicating they are managers. Managers are defined as background processes in charge of a particular aspect of the system. These are called daemons on Linux and most Unix-like OS'es and services on Windows. However, in Anillo OS, services are different from managers: services are short-lived and are typically launched as a result of user interaction (e.g. providing a service for an app or program), while managers run for much longer periods of time, possibly even for as long as the OS is running, like the case of sysman. They are also typically launched as a result of some user-unrelated event occurring.
