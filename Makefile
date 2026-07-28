# === Configuration ===
SPHAIRA_ROOT_DIR := $(CURDIR)
SPHAIRA_BUILD_PRESET := ReleaseWithInstall
SPHAIRA_BUILD_JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
SPHAIRA_FAN_TITLE_ID := 00FF46554E43544C

# Шлях до зібраного NRO
SOURCE_NRO_DIR := $(SPHAIRA_ROOT_DIR)/build/$(SPHAIRA_BUILD_PRESET)/switch/kefir-hub
SOURCE_NRO_FILE := $(SOURCE_NRO_DIR)/kefir-hub.nro
SOURCE_SYSMODULE_DIR := $(SPHAIRA_ROOT_DIR)/build/$(SPHAIRA_BUILD_PRESET)/atmosphere/contents/$(SPHAIRA_FAN_TITLE_ID)

# Куди копіювати NRO
DEST_DIR := $(abspath $(SPHAIRA_ROOT_DIR)/../_kefir/kefir)
DEST_NRO_FILE := $(DEST_DIR)/hbmenu.nro
DEST_CONTENTS_DIR := $(DEST_DIR)/atmosphere/contents
DEST_SYSMODULE_DIR := $(DEST_CONTENTS_DIR)/$(SPHAIRA_FAN_TITLE_ID)

# Налаштування nxlink
NXLINK_IP_DEFAULT := 192.168.50.69

# Адресу консолі можна дописати позиційно: `make nxlink 192.168.50.11`.
# make сприймає такий аргумент як ще одну ціль, тож усе, що не є відомою
# ціллю, забираємо як адресу і гасимо порожнім правилом -- інакше збірка
# впаде на "No rule to make target". Робиться лише для мережевих цілей, щоб
# не перехопити щось на кшталт `make nxlink clean`.
KNOWN_GOALS := all build copy nxlink ftp pftp clean help
NET_GOALS := nxlink ftp pftp

ifneq ($(filter $(NET_GOALS),$(MAKECMDGOALS)),)
  NET_ADDR_ARG := $(firstword $(filter-out $(KNOWN_GOALS),$(MAKECMDGOALS)))
endif

# Пріоритет: NXLINK_IP=... у командному рядку > позиційний аргумент > типова.
NXLINK_IP ?= $(if $(NET_ADDR_ARG),$(NET_ADDR_ARG),$(NXLINK_IP_DEFAULT))

# Налаштування FTP
FTP_IP ?= $(NXLINK_IP)
FTP_PORT ?= 5000

# === Targets ===
.PHONY: all build copy nxlink clean help ftp pftp

ifneq ($(NET_ADDR_ARG),)
.PHONY: $(NET_ADDR_ARG)
$(NET_ADDR_ARG):
	@:
endif

# Команда за замовчуванням (make або make all)
all: copy
	@echo "-------------------------------------------------------"
	@echo "Kefir Hub built and copied to $(DEST_NRO_FILE)"
	@echo "-------------------------------------------------------"

# Тільки збірка
build:
	@echo ">>> Configuring Kefir Hub with preset: $(SPHAIRA_BUILD_PRESET)..."
	@cd $(SPHAIRA_ROOT_DIR) && cmake --preset $(SPHAIRA_BUILD_PRESET)
	@echo ">>> Building Kefir Hub with preset: $(SPHAIRA_BUILD_PRESET) using $(SPHAIRA_BUILD_JOBS) jobs..."
	@cd $(SPHAIRA_ROOT_DIR) && cmake --build --preset $(SPHAIRA_BUILD_PRESET) --parallel $(SPHAIRA_BUILD_JOBS)
	@echo ">>> Build complete. Artifact: $(SOURCE_NRO_FILE)"

# Копіювання результату збірки
copy: build
	@echo ">>> Copying $(SOURCE_NRO_FILE) to $(DEST_NRO_FILE)..."
	@mkdir -p $(DEST_DIR)
	@cp $(SOURCE_NRO_FILE) $(DEST_NRO_FILE)
	@echo ">>> Copying Kefir fan sysmodule to $(DEST_CONTENTS_DIR)..."
	@mkdir -p $(DEST_CONTENTS_DIR)
	@rm -rf $(DEST_SYSMODULE_DIR)
	@cp -r $(SOURCE_SYSMODULE_DIR) $(DEST_CONTENTS_DIR)/
	@echo ">>> Copied successfully."

# Збірка та відправка через nxlink
nxlink:
	@echo ">>> Sending $(SOURCE_NRO_FILE) to $(NXLINK_IP) via nxlink..."
	@nxlink -a $(NXLINK_IP) $(SOURCE_NRO_FILE)
	@echo ">>> Sent via nxlink."

# Відправка через FTP
ftp pftp:
	@echo ">>> Uploading NRO and sysmodule to $(FTP_IP):$(FTP_PORT) via FTP..."
	curl --ftp-create-dirs -T $(SOURCE_NRO_FILE) ftp://$(FTP_IP):$(FTP_PORT)/switch/kefir-hub/kefir-hub.nro
	curl --ftp-create-dirs -T $(SOURCE_SYSMODULE_DIR)/exefs.nsp ftp://$(FTP_IP):$(FTP_PORT)/atmosphere/contents/$(SPHAIRA_FAN_TITLE_ID)/exefs.nsp
	curl --ftp-create-dirs -T $(SOURCE_SYSMODULE_DIR)/toolbox.json ftp://$(FTP_IP):$(FTP_PORT)/atmosphere/contents/$(SPHAIRA_FAN_TITLE_ID)/toolbox.json
	@echo ">>> FTP upload complete."

# Очищення
clean:
	@echo ">>> Cleaning Kefir Hub build directory: $(SPHAIRA_ROOT_DIR)/build/$(SPHAIRA_BUILD_PRESET)..."
	@rm -rf $(SPHAIRA_ROOT_DIR)/build/$(SPHAIRA_BUILD_PRESET)
	@echo ">>> Cleaning copied NRO: $(DEST_NRO_FILE)..."
	@rm -f $(DEST_NRO_FILE)
	@echo ">>> Cleaning copied Sphaira fan sysmodule..."
	@rm -rf $(DEST_SYSMODULE_DIR)
	@rm -rf $(DEST_CONTENTS_DIR)/00FF000053504846
	@echo ">>> Clean complete."

# Допомога
help:
	@echo "Available targets:"
	@echo "  make / make all    - Build Sphaira and copy result to $(DEST_NRO_FILE)"
	@echo "  make build         - Only build Sphaira (override jobs with SPHAIRA_BUILD_JOBS=N)"
	@echo "  make copy          - Build (if needed) and copy result"
	@echo "  make nxlink        - Send the built NRO via nxlink to $(NXLINK_IP)"
	@echo "  make nxlink <ip>   - Same, but to the given address (e.g. make nxlink 192.168.50.11)"
	@echo "  make ftp / pftp    - Send the built NRO and sysmodule via FTP to $(FTP_IP):$(FTP_PORT)"
	@echo "  make ftp <ip>      - Same, but to the given address"
	@echo "  make clean         - Remove build artifacts and copied NRO"
	@echo "  make help          - Show this help message"
	@echo ""
	@echo "Default console address: $(NXLINK_IP_DEFAULT)"
	@echo "Override it positionally as above, or with NXLINK_IP=<ip> / FTP_IP=<ip>."
