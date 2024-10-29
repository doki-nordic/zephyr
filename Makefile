
TARGET1=build/cpu1
TARGET2=build/cpu2

SOURCES=\
	./main.c \
	./ipc_service/ipc_service.c \
	./ipc_service/lib/pbuf.c \
	./linux_mocks/cache.c \
	./ipc_service/lib/icmsg.c \
	./ipc_service/backends/ipc_icmsg.c \

DEFINES=

INCLUDE=\
	-I./linux_mocks \
	-I./include \
	-I./ipc_service/backends \
	-I./ipc_service/lib \

all: $(TARGET1) $(TARGET2)
#all: tmp

$(TARGET1): _do_it_always_ dts
	gcc -m32 -O0 -g -include ./linux_mocks/_config.h $(DEFINES) -I./cpu1 $(INCLUDE) $(SOURCES) -o $(TARGET1)

$(TARGET2): _do_it_always_ dts
	gcc -m32 -O0 -g -include ./linux_mocks/_config.h $(DEFINES) -I./cpu2 $(INCLUDE) $(SOURCES) -o $(TARGET2)

tmp: _do_it_always_ dts
	gcc -m32 -O0 -g -include ./linux_mocks/_config.h $(DEFINES) -I./cpu2 $(INCLUDE) ./ipc_service/backends/ipc_icmsg.c -E -o tmp.c

_do_it_always_:

dts: .venv
	mkdir -p cpu1/zephyr
	mkdir -p cpu2/zephyr
	.venv/bin/jinja2 linux_mocks/devicetree_generated.h.jinja cpu1/dts.json --format=json -o cpu1/zephyr/devicetree_generated.h
	.venv/bin/jinja2 linux_mocks/devicetree_generated.h.jinja cpu2/dts.json --format=json -o cpu2/zephyr/devicetree_generated.h

.venv:
	python -m venv .venv
	.venv/bin/pip install jinja2-cli
