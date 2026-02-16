SUBDIRS = bas2wasm c2wasm sewerpipe

all:
	@for d in $(SUBDIRS); do echo "=== $$d ==="; $(MAKE) -C $$d; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done

test:
	@for d in $(SUBDIRS); do \
		if $(MAKE) -C $$d -n test >/dev/null 2>&1; then \
			echo "=== $$d test ==="; $(MAKE) -C $$d test; \
		fi; \
	done

.PHONY: all clean test
