<!-- This issue describes a bug in the qemu driver of libvirt -->

<!--
  Note: Sections like this one will be stripped out once you submit the issue.

  Please describe the bug as much as possible and add any relevant information
  you have. It allows us to deal with issues quicker.

  Refer to [https://libvirt.org/bugs.html#quality] for further information regarding
  reporting a bug.
-->

## Versions and environment
<!--
  Provide versions of libvirtd, qemu and possibly other relevant components if you
  are able to narrow down the problem. Also describe any specialties in your setup.

  Important:
    Please make sure that the bug you are reporting can be reproduced with a
    recent libvirt version (ideally upstream) to ensure that it wasn't fixed
    already.
-->
 - libvirt:
 - qemu:

## Description of the bug
<!-- Describe what happened and what's supposed to happen if that isn't obvious  -->

## Steps to reproduce the bug
<!-- Describe the steps necessary to reproduce the bug -->
1.
2.
3.
4.

## Additional information
<!--
  Attach any relevant information:
  - XML configuration of the VM
  - config files:
    - /etc/libvirt/qemu.conf (If you've modified the settings)
  - log files:
    - libvirtd debug log file [https://www.libvirt.org/kbase/debuglogs.html]
    - qemu VM log (/var/log/libvirt/qemu/$VMNAME.log)
    - please attach compressed archive if you can't find the relevant section

  If reporting a crash:
  - stack trace of the crashed binary

  Please don't post screenshots of text.
-->



<!-- The line below ensures that proper tags are added to the issue. -- >
/label ~bug ~driver-qemu
