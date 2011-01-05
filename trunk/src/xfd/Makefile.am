bin_PROGRAMS=xfd
xfd_SOURCES=xfd.c
xfd_LDADD=../libfatx/libfatx.la
xfd_CFLAGS=$(AM_CFLAGS) -D_FILE_OFFSET_BITS=64 -I../include
xfd_LDFLAGS=$(AM_LDFLAGS) -static

install-exec-hook:
	[ -d $(sbindir) ] || mkdir -p $(sbindir)
	[ -L $(sbindir)/mount.fatx ] || ln -s $(bindir)/xfd $(sbindir)/mount.fatx

uninstall-hook:
	[ -L $(sbindir)/mount.fatx ] && rm $(sbindir)/mount.fatx
