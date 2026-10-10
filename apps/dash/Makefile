PORT_NAME            := dash
PORT_TYPE            := automake
PORT_VERSION         := 0.5.12
PORT_URL             := http://gondor.apana.org.au/~herbert/dash/files/dash-$(PORT_VERSION).tar.gz

# TODO: job control support
CFLAGS += -DJOBS=0

PORT_CONFIGURE_FLAGS := --disable-nls

include ../port.mk
