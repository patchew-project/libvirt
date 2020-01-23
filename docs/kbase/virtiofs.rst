============================
Sharing files with Virtio-FS
============================

=== 8< delete before merging 8< ===
NOTE: if you're looking at this note, this is just a proposal.
See the up-to-date version on: https://libvirt.org/kbase/virtiofs.html
=== 8< --------------------- 8< ===

.. contents::

=========
Virtio-FS
=========

Virtio-FS is a delicious delicacy aiming to provide an easy-to-configure
way of sharing filesystems between the host and the virtual machine.

See https://virtio-fs.gitlab.io/

==========
Host setup
==========

The host-side virtiofsd daemon, like other vhost-user backed devices,
requres shared memory between the host and the guest. As of QEMU 4.2, this
requires specifying a NUMA topology for the guest and explicitly specifying
a memory backend. Multiple options are available:

Either of the following:

1. Use file-backed memory

Configure the directory where the files backing the memory will be stored
with the ``memory_backing_dir`` option in ``/etc/libvirt/qemu.conf``

::

   # This directory is used for memoryBacking source if configured as file.
   # NOTE: big files will be stored here
   memory_backing_dir = "/dev/shm/"

2. Use hugepage-backed memory

Make sure there are enough huge pages allocated for the requested guest memory.
For exmaple, for one guest with 2 GiB of RAM backed by 2 MiB hugepages:

::

   # virsh allocpages 2M 1024

===========
Guest setup
===========

1. Specify the NUMA topology

in the domain XML of the guest.
For the simplest one-node topology for a guest with 2GiB of RAM and 8 vCPUs:

::

   <domain>
     ...
     <cpu ...>
       <numa>
         <cell id='0' cpus='0-7' memory='2' unit='GiB' memAccess='shared'/>
       </numa>
     </cpu>
     ...
   </domain>

Note that the CPU element might already be specified and only one is allowed.

2. Specify the memory backend

Either of the following:

2.1. File-backed memory

::

   <domain>
     ...
     <memoryBacking>
       <access mode='shared'/>
     </memoryBacking>
     ...
   </domain>

This will create a file in the directory specified in ``qemu.conf``

2.2. Hugepage-backed memory

::

   <domain>
     ...
     <memoryBacking>
       <hugepages>
         <page size='2' unit='M'/>
       </hugepages>
       <access mode='shared'/>
     </memoryBacking>
     ...
   </domain>

3. Add the ``vhost-user-fs`` QEMU device via the ``filesystem`` element

::

   <domain>
     ...
     <devices>
       ...
       <filesystem type='mount' accessmode='passthrough'>
         <driver type='virtiofs'>
           <binary>/usr/libexec/virtiofsd</binary>
         </driver>
         <source dir='/path'/>
         <target dir='mount_tag'/>
       </filesystem>
       ...
     </devices>
   </domain>

Note that despite its name, the ``target dir`` is actually a mount tag and does
not have to correspond to the desired mount point in the guest.

So far, ``passthrough`` is the only supported access mode and it requires
running the ``virtiofsd`` daemon as root.

4. Boot the guest and mount the filesystem

::

   guest# mount -t virtiofs mount_tag /mnt/mount/path

Note: this requires virtiofs support in the guest kernel (Linux v5.4 or later)

===================
Optional parameters
===================

More optional elements can be specified

::

  <driver type='virtiofs' queue='1024' xattr='on'>
    <binary>/usr/libexec/virtiofsd</binary>
    <cache mode='always' size='1024' unit='GiB'/>
    <lock posix_lock='on' flock='on'/>
  </driver>
