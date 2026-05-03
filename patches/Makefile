.PHONY: all clean act friends http miiverse ssl socket

all: act friends http miiverse ssl socket

act:
	@if [ -f act/code.bin ]; then \
		$(MAKE) -C act; \
	else \
		echo "Skipping act patch: act/code.bin missing"; \
	fi

friends:
	@if [ -f friends/code.bin ]; then \
		$(MAKE) -C friends; \
	else \
		echo "Skipping friends patch: friends/code.bin missing"; \
	fi

http:
	@if [ -f http/code.bin ]; then \
		$(MAKE) -C http; \
	else \
		echo "Skipping http patch: http/code.bin missing"; \
	fi

miiverse:
	@if [ -f miiverse/code.bin ]; then \
		$(MAKE) -C miiverse; \
	else \
		echo "Skipping miiverse patch: miiverse/code.bin missing"; \
	fi

ssl:
	@if [ -f ssl/code.bin ]; then \
		$(MAKE) -C ssl; \
	else \
		echo "Skipping ssl patch: ssl/code.bin missing"; \
	fi

socket:
	@if [ -f socket/code.bin ]; then \
		$(MAKE) -C socket; \
	else \
		echo "Skipping socket patch: socket/code.bin missing"; \
	fi

clean:
	@make -C act clean
	@make -C friends clean
	@make -C http clean
	@make -C miiverse clean
	@make -C ssl clean
	@make -C socket clean
