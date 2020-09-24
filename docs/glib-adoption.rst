=====================
Adoption of GLib APIs
=====================

Libvirt has adopted use of the `GLib
library <https://developer.gnome.org/glib/stable/>`__. Due to
libvirt's long history of development, there are many APIs in
libvirt, for which GLib provides an alternative solution. The
general rule to follow is that the standard GLib solution will be
preferred over historical libvirt APIs. Existing code will be
ported over to use GLib APIs over time, but new code should use
the GLib APIs straight away where possible.

The following is a list of libvirt APIs that should no longer be
used in new code, and their suggested GLib replacements:

Memory allocation
   ``VIR_ALLOC``, ``VIR_REALLOC``, ``VIR_RESIZE_N``,
   ``VIR_EXPAND_N``, ``VIR_SHRINK_N``, ``VIR_FREE``

   Prefer the GLib APIs ``g_new0``/``g_renew``/ ``g_free`` in most
   cases.  There should rarely be a need to use
   ``g_malloc``/``g_realloc``.  **NEVER MIX** use of the classic
   libvirt memory allocation APIs and GLib APIs within a single
   method. Keep the style consistent, converting existing code to
   GLib style in a separate, prior commit.

Array operations
   ``VIR_APPEND_ELEMENT``, ``VIR_INSERT_ELEMENT``, ``VIR_DELETE_ELEMENT``

   Instead of using plain C arrays, it is preferrable to use one of
   the GLib types, ``GArray``, ``GPtrArray`` or ``GByteArray``.
   These all use a struct to track the array memory and size
   together and efficiently resize.
