EXTRA_DIST = libzorpll.rc libzorpll.def win32_reg.cc

SUBDIRS = zorp

lib_LTLIBRARIES = libzorpll-@LIBZORPLL_LT_RELEASE@.la

libzorpll_@LIBZORPLL_LT_RELEASE@_la_SOURCES = \
	cap.cc \
	connect.cc \
	crypt.cc \
	error.cc \
	inetaddress.cc \
	io.cc \
	listen.cc \
	log.cc \
	memtrace.cc \
	misc.cc \
	packetbuf.cc \
	poll.cc \
	process.cc \
	random.cc \
	registry.cc \
	sockaddr.cc \
	socket.cc \
	socketsource.cc \
	source.cc \
	ssl.cc \
	stackdump.cc \
	stream.cc \
	streambuf.cc \
	streamfd.cc \
	streamgzip.cc \
	streamline.cc \
	streamssl.cc \
	string.cc \
	thread.cc \
	zobject.cc \
	blob.cc \
	streamblob.cc \
	streamtee.cc \
	code_base64.cc \
	code.cc \
	code_gzip.cc \
	headerset.cc \
	zcp.cc \
	zurlparse.cc
	

libzorpll_@LIBZORPLL_LT_RELEASE@_la_LDFLAGS = \
	$(REGULAR_LDFLAGS) \
	-version-info $(LIBZORPLL_LT_CURRENT):0:$(LIBZORPLL_LT_AGE) \
	-export-dynamic

libzorpll_@LIBZORPLL_LT_RELEASE@_la_CPPFLAGS = -DCOMPILING_LIBZORPLL

libzorpll_@LIBZORPLL_LT_RELEASE@_la_CXXFLAGS = $(REGULAR_CXXFLAGS)

install-exec-local:
	$(MKDIR_P) ${DESTDIR}/${ZORPLIB_TEMP_DIR}
	$(MKDIR_P) ${DESTDIR}/${ZORPLIB_PIDFILE_DIR}
