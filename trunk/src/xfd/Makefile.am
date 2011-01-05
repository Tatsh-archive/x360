bin_PROGRAMS=xfd-mount
xfd_mount_SOURCES=xfd.c
xfd_mount_LDADD=../libfatx/libfatx.la
xfd_mount_CFLAGS=$(AM_CFLAGS) -D_FILE_OFFSET_BITS=64 -I../include
xfd_mount_LDFLAGS=$(AM_LDFLAGS) -static

install-exec-hook:
	[ -z "$(sbindir)" ] || $(MKDIR_P) "$(DESTDIR)$(sbindir)"
	[ -L $(DESTDIR)$(sbindir)/mount.fatx ] || ln -s $(bindir)/xfd-mount $(sbindir)/mount.fatx

uninstall-hook:
	[ -L $(DESTDIR)$(sbindir)/mount.fatx ] && rm -f $(DESTDIR)$(sbindir)/mount.fatx
