# Global build/upload/clean targets for all miniMQ PlatformIO projects.
# Each target simply runs `pio run -d <project>` against the corresponding
# sub-project, so every project keeps its own platformio.ini as the source
# of truth.
#
# Targets:
#   make                 -> help
#   make build           -> build all projects
#   make build-<proj>    -> build one project (artnet | encoder | fader)
#   make upload          -> upload all projects
#   make upload-<proj>   -> upload one project
#   make clean           -> clean all projects
#   make clean-<proj>    -> clean one project
#   make help            -> this text

PIO ?= pio

# short name -> project directory
PROJECTS := artnet encoder fader

define PROJECT_DEF
PROJECT_DIR_$(1) := $(2)
endef
$(eval $(call PROJECT_DEF,artnet,ArtNet-USB))
$(eval $(call PROJECT_DEF,encoder,Enncoder))
$(eval $(call PROJECT_DEF,fader,Fader))

PROJECT_GROUPS := build upload clean

# Note: the per-project `build-<proj>` targets are NOT listed in .PHONY -
# listing a bare name in .PHONY disables implicit-rule search in GNU make 3.81,
# which would leave the pattern rules below without a recipe.
.PHONY: all help build upload clean

all: help

## build: compile all three project firmwares
build: $(addprefix build-,$(PROJECTS))

## upload: flash all three project firmwares
upload: $(addprefix upload-,$(PROJECTS))

## clean: remove build artifacts of all three projects
clean: $(addprefix clean-,$(PROJECTS))

build-%:
	@echo ">> Building $(PROJECT_DIR_$*) ..."
	$(PIO) run -d $(PROJECT_DIR_$*)

upload-%:
	@echo ">> Uploading $(PROJECT_DIR_$*) ..."
	$(PIO) run -d $(PROJECT_DIR_$*) -t upload

clean-%:
	@echo ">> Cleaning $(PROJECT_DIR_$*) ..."
	$(PIO) run -d $(PROJECT_DIR_$*) -t clean

help:
	@echo "miniMQ global PlatformIO targets:"
	@echo ""
	@echo "  Projects: artnet, encoder, fader"
	@echo ""
	@echo "  make build            build all projects"
	@echo "  make build-<proj>     build one project"
	@echo "  make upload           upload all projects"
	@echo "  make upload-<proj>    upload one project"
	@echo "  make clean            clean all projects"
	@echo "  make clean-<proj>     clean one project"
	@echo ""
	@echo "  example: make build-fader"