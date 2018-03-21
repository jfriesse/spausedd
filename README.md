# Scheduler Pause Detection Daemon

## Overview
The spausedd utility is used for detecting and logging scheduler pause.
This means, when process should have been scheduled, but it was not. It's
also able to use steal time (time spent in other operating systems when
running in a virtualized environment) so it is (to some extend) able to
detect if problem is on the VM or host side.  spausedd is able to read
information about steal time ether from kernel or (if compiled in) also
use VMGuestLib.

## Installation
### RPM
[Copr](https://copr.fedorainfracloud.org/coprs/honzaf/spausedd/) service is
used for automatically create RPM packages right after new version is released.
Usage is very simple:
```
# dnf copr enable honzaf/spausedd
```
or for older distribution:
```
# yum copr enable honzaf/spausedd
```
or it's possible to manually add repo file.

### Source code
For stable version use GitHub releases. For newest git, use
```
$ git clone git://github.com/jfriesse/spausedd.git
$ cd spausedd
$ make
```
Makefile is able to detect if VMGuestLib is installed and if so, support
for VMGuestLib is compiled in.

### Support
Please use GitHub issues.

### Defects
If a defect is found in spausedd, please create new issue. You can also
try to find and fix bug and create pull request.
