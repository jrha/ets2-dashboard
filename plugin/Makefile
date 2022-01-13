SDK_HEADERS=\
	./*.h \
	../../scs_sdk_current/include/*.h \
	../../scs_sdk_current/include/common/*.h \
	../../scs_sdk_current/include/amtrucks/*.h \
	../../scs_sdk_current/include/eurotrucks2/*.h

SDK_INCLUDES=\
	-I. \
	-I../../scs_sdk_current/include \
	-I../../scs_sdk_current/include/common/ \
	-I../../scs_sdk_current/include/amtrucks/ \
	-I../../scs_sdk_current/include/eurotrucks2

UNAME:= $(shell uname -s)

ifeq ($(UNAME),Darwin)
LIB_NAME_OPTION=-install_name
else
LIB_NAME_OPTION=-soname
endif

telemetry.so:  *.cpp $(SDK_HEADERS)
	gcc -o $@ -fPIC -Wall --shared -Wl,$(LIB_NAME_OPTION),$@ $(SDK_INCLUDES) -std=c++11 *.cpp

.PHONY: clean
clean:
	@rm -f -- *.so
