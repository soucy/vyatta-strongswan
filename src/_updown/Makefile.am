ipsec_SCRIPTS = _updown
CLEANFILES = _updown
dist_man8_MANS = _updown.8
EXTRA_DIST = _updown.in

_updown : _updown.in
	sed \
	-e "s:\@sbindir\@:$(sbindir):" \
	-e "s:\@routing_table\@:$(routing_table):" \
	-e "s:\@routing_table_prio\@:$(routing_table_prio):" \
	$(srcdir)/$@.in > $@
	chmod +x $@
