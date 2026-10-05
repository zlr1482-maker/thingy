XenV2 source share bundle
=========================

This bundle was assembled from the Xen files available in this ChatGPT workspace.

Main folders
------------
kernel_driver/
    Kernel driver source and Visual Studio driver project.
    Includes driver/src/*.cpp, driver/include/*.h equivalents, driver.inf,
    and the .vcxproj files.

xen-loader/
    Loader source from the archived Xen project.

usermode_core/
    Common usermode Driver/Memory/Process source and headers.

xen-tool/
    Xen overlay/tool source. main.cpp is the newest source available in this
    conversation at bundle creation time:
        Xen HYBRID AH15.12.12.6 ADMIN LIST UPDATE

shared/
    Shared project files.

original_archives/
    Original driver.zip and shared.zip as uploaded to the workspace.

Kernel driver note
------------------
The accessible project inventory shows that the original Windows machine had
compiled XenV2Type2.sys files, but the actual compiled .sys binary was NOT
present in the uploaded driver.zip or the archived full-project ZIP available
to ChatGPT. The kernel driver SOURCE IS included here in full.

The original project inventory recorded paths such as:
    out/driverx64Release/XenV2Type2.sys
    out/usemodex64Release/XenV2Type2.sys
    x64/Debug/XenV2Type2.sys

Those listings are evidence that a compiled driver existed on the original
machine, but they are not the binary itself.

Current admin additions in bundled main.cpp
-------------------------------------------
Includes the recent built-in admin additions:
    76561198423243189
    76561199038360023
    76561198087646913

Sharing
-------
For easiest sharing, send XenV2_SOURCE_SHARE.zip. A separate
XenV2_KERNEL_DRIVER_SOURCE.zip is also provided for people who only need the
kernel-driver source.
