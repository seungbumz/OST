tests: clean compile_tests
all: compile_tests
.PHONY: all tests compile_tests clean

compile_tests: all.iotlab-m3 all.iotlab-a8-m3
clean: distclean.iotlab-m3 distclean.iotlab-a8-m3


IOTLAB = examples/iotlab
EXCLUDED = $(IOTLAB)/00-configuration/

IOTLAB_FOLDERS = $(sort $(dir $(wildcard $(IOTLAB)/*/)))
IOTLAB_TARGETS = $(filter-out $(EXCLUDED),$(IOTLAB_FOLDERS))

CLEAN_M3      = $(addprefix distclean.iotlab-m3.,$(IOTLAB_TARGETS))
CLEAN_A8_M3   = $(addprefix distclean.iotlab-a8-m3.,$(IOTLAB_TARGETS))
COMPILE_M3    = $(addprefix all.iotlab-m3.,$(IOTLAB_TARGETS))
COMPILE_A8_M3 = $(addprefix all.iotlab-a8-m3.,$(IOTLAB_TARGETS))

all.iotlab-m3: $(COMPILE_M3)
all.iotlab-a8-m3: $(COMPILE_A8_M3)

distclean.iotlab-m3: $(CLEAN_M3)
distclean.iotlab-a8-m3: $(CLEAN_A8_M3)


$(COMPILE_M3) $(COMPILE_A8_M3) $(CLEAN_M3) $(CLEAN_A8_M3): %:
	@ # $@ == command.target.directory
	$(eval command=$(word 1, $(subst ., ,$@)))
	$(eval target=$(word 2, $(subst ., ,$@)))
	$(eval directory=$(word 3, $(subst ., ,$@)))
	make -s V=1 $(command) TARGET=$(target) -C $(directory)
