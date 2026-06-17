# Godrej AC / universal IR remote — ESPHome via Docker.
# Everything runs in the official ESPHome container (no local toolchain).
#
# Usage:
#   make compile            # compile the default config
#   make flash              # compile + upload over USB
#   make logs               # stream serial logs
#   make run                # compile + upload + logs (the all-in-one)
#   make config             # validate YAML
#   make clean              # remove build artifacts for the config
#   make dashboard          # start the ESPHome web UI at http://localhost:6052
#   make ports              # list connected serial devices
#
# Override the config or port per-invocation:
#   make flash YAML=capture.yaml
#   make logs DEVICE=/dev/ttyACM1
#
# universal-ir.yaml = single-file builder config (pulls everything from GitHub).
# universal-ir-local.yaml = same node, built from the local tree (the default).

YAML   ?= universal-ir-local.yaml   # default config (local build of the node)
DEVICE ?= /dev/ttyACM0            # ESP32-C3 native USB serial
CLI     = docker compose run --rm cli

.DEFAULT_GOAL := help

.PHONY: help compile config flash upload logs run clean dashboard down ports

help: ## Show this help
	@echo "Targets (override with YAML=... DEVICE=...):"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
		| awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Current: YAML=$(YAML)  DEVICE=$(DEVICE)"

compile: ## Compile firmware
	$(CLI) compile $(YAML)

config: ## Validate the YAML config
	$(CLI) config $(YAML)

flash: ## Compile + upload over USB
	$(CLI) upload $(YAML) --device $(DEVICE)

upload: flash ## Alias for flash

logs: ## Stream serial logs (Ctrl-C to stop)
	$(CLI) logs $(YAML) --device $(DEVICE)

run: ## Compile + upload + logs in one go
	$(CLI) run $(YAML) --device $(DEVICE)

clean: ## Remove build artifacts for the config
	$(CLI) clean $(YAML)

dashboard: ## Start the ESPHome web dashboard (http://localhost:6052)
	docker compose up -d esphome

down: ## Stop the dashboard / all services
	docker compose down

ports: ## List connected serial devices
	@ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "no serial devices found"
