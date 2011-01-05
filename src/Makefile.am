install-exec-hook:
	[ -z "$(bindir)" ] || $(MKDIR_P) "$(DESTDIR)$(bindir)"
	$(INSTALL_PROGRAM_ENV) $(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=install $(INSTALL_PROGRAM) xpart-dm "$(DESTDIR)$(bindir)"
	$(INSTALL_PROGRAM_ENV) $(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=install $(INSTALL_PROGRAM) xpart-loop "$(DESTDIR)$(bindir)"
	[ -L $(DESTDIR)$(bindir)/xpart ] || ln -s $(DESTDIR)$(bindir)/xpart-dm $(DESTDIR)$(bindir)/xpart

uninstall-hook:
	-[ -n "$(DESTDIR)$(bindir)/xpart-dm" ] && rm -f $(DESTDIR)$(bindir)/xpart-dm
	-[ -n "$(DESTDIR)$(bindir)/xpart-loop" ] && rm -f $(DESTDIR)$(bindir)/xpart-loop
	-[ -L "$(DESTDIR)$(bindir)/xpart" ] && rm -f $(DESTDIR)$(bindir)/xpart
	