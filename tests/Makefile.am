AM_CXXFLAGS=$(REGULAR_CXXFLAGS) $(DEPS_CXXFLAGS) -DBOOST_TEST_DYN_LINK=1
AM_LDFLAGS=$(REGULAR_LDFLAGS) -lboost_unit_test_framework ../src/libzorpll-@LIBZORPLL_LT_RELEASE@.la -lm

check_PROGRAMS = \
	test_zcrypt \
	test_readline \
	test_registry \
	test_conns \
	test_ssl \
	test_streams \
	test_random \
	test_valid_chars \
	test_sockaddr \
	test_blob \
	test_base64 \
	test_codegzip \
	test_portrandom \
	test_pktbuf \
	test_inetaddress

test_zcrypt_SOURCES = test_zcrypt.cc

test_readline_SOURCES = test_readline.cc

test_registry_SOURCES = test_registry.cc

test_conns_SOURCES = test_conns.cc

test_ssl_CXXFLAGS = $(AM_CXXFLAGS) -DTEST_SRC_DIR=\"${srcdir}\"
test_ssl_SOURCES = test_ssl.cc

test_random_SOURCES = test_random.cc

test_streams_SOURCES = test_streams.cc

test_valid_chars_SOURCES = test_valid_chars.cc

test_sockaddr_SOURCES = test_sockaddr.cc

test_blob_SOURCES = test_blob.cc

test_base64_SOURCES = test_base64.cc

test_codegzip_SOURCES = test_codegzip.cc

test_portrandom_SOURCES = test_portrandom.cc randtest.cc randtest.h

test_pktbuf_SOURCES = test_pktbuf.cc

test_inetaddress_SOURCES = test_inetaddress.cc

TESTS = \
	test_registry \
	test_readline \
	test_zcrypt \
	test_conns \
	test_ssl \
	test_random \
	test_streams \
	test_valid_chars \
	test_sockaddr \
	test_blob \
	test_base64 \
	test_codegzip \
	test_portrandom \
	test_pktbuf \
	test_inetaddress

EXTRA_DIST = testx509.crt testx509.key
