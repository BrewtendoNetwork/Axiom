.PHONY: all clean

ifeq ($(strip $(NASC_URL)),)
	$(warning "Please set NASC_URL in your environment")
endif

all:
	@mkdir -p build out
	@armips src/main.s
	@flips -c code.bin build/patched_code.bin out/code.ips

clean:
	@rm -rf build out
